/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include <bmp280.h>
#include "../config.h"

/* HTTP constants that aren't configurable in menuconfig */
#define MEASUREMENT_ENDP "/measurement"
#define DEVICE_ENDP "/device"

static const char *TAG = "bicho";

static char *BODY_MEASURE = "id=%s&t=%0.2f&h=%0.2f&p=%0.2f";
static char *BODY_DEVICE = "id=%s&n="DEVICE_NAME"&k="DEVICE_KEY"";
static char *REQUEST_POST = "POST %s HTTP/1.0\r\n"
    "Host: "API_IP_PORT"\r\n"
    "User-Agent: "USER_AGENT"\r\n"
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
    "%s";

static char *MAC_FORMAT ="%02x:%02x:%02x:%02x:%02x:%02x";

static void http_get_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    //struct in_addr *addr;
    
    int s, r;
    char body[64];
    char recv_buf[64];
    char send_buf[256];
    bool registered = 0;

    char mac_dir[17];    
    bmp280_params_t params;
    bmp280_init_default_params(&params);
    bmp280_t dev;
    memset(&dev, 0, sizeof(bmp280_t));

    ESP_ERROR_CHECK(bmp280_init_desc(&dev, BMP280_I2C_ADDRESS_0, 0, SDA_GPIO, SCL_GPIO));
    ESP_ERROR_CHECK(bmp280_init(&dev, &params));

    bool bme280p = dev.id == BME280_CHIP_ID;
    ESP_LOGI(TAG, "BMP280: found %s\n", bme280p ? "BME280" : "BMP280");

    float pressure, temperature, humidity;

    uint8_t baseMac[6];
    esp_wifi_get_mac(WIFI_IF_STA, baseMac);
    sprintf(mac_dir,MAC_FORMAT,
        baseMac[0], baseMac[1], baseMac[2],
        baseMac[3], baseMac[4], baseMac[5]);
    ESP_LOGI(TAG,"MAC: %s\n",mac_dir);

    while(1) {
        if (registered) {        
            if (bmp280_read_float(&dev, &temperature, &pressure, &humidity) != ESP_OK) {
                ESP_LOGI(TAG, "Temperature/pressure reading failed\n");
            } else {
                //ESP_LOGI(TAG, "Pressure: %.2f Pa, Temperature: %.2f C", pressure, temperature);
                //ESP_LOGI(TAG,", Humidity: %.2f\n", humidity);
                sprintf(body, BODY_MEASURE, mac_dir, temperature , humidity , pressure);
                sprintf(send_buf, REQUEST_POST, MEASUREMENT_ENDP,(int)strlen(body),body );
                //ESP_LOGI(TAG,"body: \n%s\n",body);
                //ESP_LOGI(TAG,"Enviando: \n%s\n",send_buf);
            }
        }
        else { //Si no está registrado, registrar
            sprintf(body, BODY_DEVICE, mac_dir);
            sprintf(send_buf, REQUEST_POST, DEVICE_ENDP, (int)strlen(body),body );
            registered = 1;
        } 

        int err = getaddrinfo(API_IP, API_PORT, &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.

           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        //addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        //ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        //ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        //ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, send_buf, strlen(send_buf)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        //ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        //ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */

        char* payload = NULL;        // acá está la papa
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            //ESP_LOGI(TAG,"%s\n",recv_buf);
            char* header_end = strstr(recv_buf, "\r\n\r\n");
            if (header_end) {
                payload = header_end + 4;
                r=0; //para salir del loop con el valor encontrado
            }        
            // for(int i = 0; i < r; i++) {
            //     putchar(recv_buf[i]);
            // }
        } while(r > 0);
        if (payload) {
            printf("%s\n", payload);
        }
        /*
        char* full_response = NULL;  // Will hold our complete response
        size_t response_size = 0;    // Current size of accumulated response
        bool headers_processed = false;
        char* payload = NULL;        // Will point to the payload portion

        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            
            if (r > 0) {
                // Allocate or resize our full response buffer
                char* new_buf = realloc(full_response, response_size + r + 1);
                if (new_buf == NULL) {
                    printf("Memory allocation failed\n");
                    free(full_response);
                    close(s);
                    return; // Handle error appropriately
                }
                
                full_response = new_buf;
                
                // Copy new data into our buffer
                memcpy(full_response + response_size, recv_buf, r);
                response_size += r;
                full_response[response_size] = '\0'; // Null terminate
                
                // If we haven't found headers end yet, check if it's in buffer now
                if (!headers_processed) {
                    char* header_end = strstr(full_response, "\r\n\r\n");
                    if (header_end) {
                        headers_processed = true;
                        
                        // Payload starts right after the \r\n\r\n
                        payload = header_end + 4;
                        
                        // Optional: Print header portion for debugging
                        *header_end = '\0'; // Temporarily terminate at header end
                        printf("Headers:\n%s\n\n", full_response);
                        *header_end = '\r'; // Restore the \r character
                    }
                }
                
                // Print received data for debugging (optional)
                for(int i = 0; i < r; i++) {
                    putchar(recv_buf[i]);
                }
            }
        } while(r > 0);

        // Now payload points to the start of the payload within full_response
        if (payload) {
            printf("\n\nPayload:\n%s\n", payload);
            
            // If you need the payload as a separate string
            char* extracted_payload = strdup(payload);
            if (extracted_payload) {
                // Use extracted_payload as needed
                // Process your payload here (JSON parsing, etc.)
                
                // Don't forget to free when done
                free(extracted_payload);
            }
        } else {
            printf("No payload found or incomplete HTTP response\n");
        }

        // Clean up
        free(full_response);
        */

        //ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);

        for(int countdown = 10; countdown >= 0; countdown--) {
            //ESP_LOGI(TAG, "%d... ", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        //ESP_LOGI(TAG, "Otra vez");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(i2cdev_init());

    ESP_ERROR_CHECK(example_connect());

    xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);
}

