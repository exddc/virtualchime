#!/bin/bash
# Setup script to automate the initial setup of the agent

# Change the current directory to the agent directory
cd "$(dirname "$0")" || exit

echo "Welcome to the agent setup script"
echo "This script will guide you through the setup of the agent"

# Install needed packages
echo "Installing needed apt packages"
sudo apt-get update
sudo apt-get install -y python3 python3-venv python3-pip python3-libcamera python3-kms++
sudo apt-get install -y python3-prctl libatlas-base-dev ffmpeg libopenjp2-7 python3-pip

# Install python packages
echo "Installing python packages"
pip3 install -r requirements.txt --break-system-packages

# Download htmx and save it to the static folder
echo "Downloading htmx"
if [ -f static/src/htmx.min.js ]; then
    echo "htmx.min.js already exists. Do you want to overwrite it?"
    read -rn1 -p "Overwrite htmx.min.js? (y/n): " OVERWRITE_HTMX
    echo ""
    if [ "$OVERWRITE_HTMX" == "y" ]; then
        rm static/src/htmx.min.js
        wget -O static/src/htmx.min.js https://unpkg.com/htmx.org@1.9.12/dist/htmx.min.js
    fi
fi


# Create a config file from user inputs
# Check if .env already exists
if [ -f .env ]; then
    echo ".env file already exists. Do you want to overwrite it?"
    read -rn1 -p "Overwrite .env? (y/n): " OVERWRITE
    echo ""
    if [ "$OVERWRITE" == "y" ]; then
        rm .env

        # Language
        while true; do
            read -rp "Enter the language (en/de): " LANGUAGE
            if [[ "$LANGUAGE" == "en" || "$LANGUAGE" == "de" ]]; then
                break
            else
                echo "Invalid selection. Please enter 'en' or 'de'."
            fi
        done

        # Property name
        echo "The property name is the name of the property the doorbell is installed at. I.e. 'Home', 'Office' or 'Street name 123'"
        read -rp "Enter the property name: " PROPERTY_NAME
        echo "Property name: $PROPERTY_NAME"

        # Agent location
        echo "The doorbell location is the physical location. I.e. 'Front door' or 'Gate'"
        read -rp "Enter the location: " AGENT_LOCATION

        # MQTT broker
        while true; do
            read -rn1 -rp "Do you want to use a custom MQTT broker? (y/n): " CUSTOM_BROKER
            echo # move to a new line after reading single character
            if [[ "$CUSTOM_BROKER" == "y" || "$CUSTOM_BROKER" == "n" ]]; then
                break
            else
                echo "Invalid selection. Please enter 'y' or 'n'."
            fi
        done

        if [ "$CUSTOM_BROKER" == "y" ]; then
            read -rp "Enter the MQTT broker IP address: " MQTT_BROKER_IP
            read -rp "Enter the MQTT broker port: " MQTT_BROKER_PORT
            read -rp "Enter the MQTT username: " MQTT_USERNAME
            read -rp "Enter the MQTT password: " MQTT_PASSWORD
        fi

        # Create the config file from the .env.sample file
        echo "Creating the .env file"
        cp .env.sample .env
        sed -i "s/LANGUAGE=en/LANGUAGE=$LANGUAGE/g" .env
        sed -i "s/PROPERTY_NAME=MyHouse/PROPERTY_NAME=$PROPERTY_NAME/g" .env
        sed -i "s/AGENT_LOCATION=door/AGENT_LOCATION=$AGENT_LOCATION/g" .env

        if [ "$CUSTOM_BROKER" == "y" ]; then
            sed -i "s/MQTT_BROKER_IP=192.168.178.1/MQTT_BROKER_IP=$MQTT_BROKER_IP/g" .env
            sed -i "s/MQTT_BROKER_PORT=1883/MQTT_BROKER_PORT=$MQTT_BROKER_PORT/g" .env
            sed -i "s/MQTT_USERNAME=mqtt_user/MQTT_USERNAME=$MQTT_USERNAME/g" .env
            sed -i "s/MQTT_PASSWORD=mqtt_password/MQTT_PASSWORD=$MQTT_PASSWORD/g" .env
        fi

        echo "Config file created"
    fi   
fi

# Create a systemd service
echo "Creating the systemd service"
# Check if the service already exists
if [ -f /etc/systemd/system/doorbell.service ]; then
    echo "doorbell.service already exists. Do you want to overwrite it?"
    read -rn1 -p "Overwrite doorbell.service? (y/n): " OVERWRITE_SERVICE
    echo ""
    if [ "$OVERWRITE_SERVICE" == "y" ]; then
        # Remove the existing service file
        sudo rm /etc/systemd/system/doorbell.service

        # Get the python3 path
        PYTHON_PATH=$(which python3)

        cp doorbell.service /etc/systemd/system/doorbell.service
        sed -i "s|ExecStart=/path/to/python /path/to/your/script.py|ExecStart=tailwindcss -i $(pwd)/static/src/style.css -o $(pwd)/static/dist/style.css --minify && $PYTHON_PATH $(pwd)/agent.py|g" /etc/systemd/system/doorbell.service
        sed -i "s|WorkingDirectory=/path/to/your/script|WorkingDirectory=$(pwd)|g" /etc/systemd/system/doorbell.service
        sed -i "s|/path/to/doorbell|$(pwd)|g" /etc/systemd/system/doorbell.service
        sed -i "s|User=your_user|User=$(whoami)|g" /etc/systemd/system/doorbell.service

        # Enable and start the service
        sudo systemctl enable doorbell
        sudo systemctl start doorbell

        else
            echo "Restarting the service"
            sudo systemctl restart doorbell
    fi  
fi

echo "Setup complete"
