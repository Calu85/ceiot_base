!/bin/bash

# Function to start all services
start_services() {
    # First tab - MongoDB
    gnome-terminal --tab -- bash -c "cd ~/ceiot_base && echo 'Starting MongoDB...' && docker run -p 27017:27017 mongo:4.0.4; exec bash"

    # Second tab - Node.js API
    gnome-terminal --tab -- bash -c "cd ~/ceiot_base/api && echo 'Starting Node.js API...' && node index.js; exec bash"

    # Third tab - SPA rebuild
    gnome-terminal --tab -- bash -c "cd ~/ceiot_base/api/spa && echo 'Running rebuild script...' && ./rebuild.sh; exec bash"
}

# Run the function
start_services

# Change directory
cd perception/esp32c3-bmp280
. ~/esp/esp-idf/export.sh
