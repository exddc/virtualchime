#!/bin/bash
# Setup script to automate the initial setup of the agent

echo "Welcome to the agent setup script"
echo "This script will guide you through the setup of the agent"

# Install needed packages
echo "Installing needed apt packages"
sudo apt-get update
sudo apt-get install -y python3 python3-venv python3-pip

# Install python packages
echo "Installing python packages"
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
deactivate

# Create a config file from user inputs
# Check if .env already exists
if [ -f .env ]; then
    echo ".env file already exists. Do you want to overwrite it?"
    read -rn1 -p "Overwrite .env? (y/n): " OVERWRITE
    if [ "$OVERWRITE" == "n" ]; then
        echo "Exiting setup"
        exit 0
    fi

    # Remove the existing .env file
    rm .env
fi

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

# Create a systemd service
echo "Creating the systemd service"
# Check if the service already exists
if [ -f /etc/systemd/system/doorbell.service ]; then
    echo "doorbell.service already exists. Do you want to overwrite it?"
    read -rn1 -p "Overwrite doorbell.service? (y/n): " OVERWRITE_SERVICE
    if [ "$OVERWRITE_SERVICE" == "n" ]; then
        echo "Exiting setup"
        exit 0
    fi

    # Remove the existing service file
    sudo rm /etc/systemd/system/doorbell.service
fi

cp doorbell.service /etc/systemd/system/doorbell.service
sed -i "s|ExecStart=/path/to/your/venv/bin/python /path/to/your/script.py|ExecStart=$(pwd)/.venv/bin/python $(pwd)/doorbell.py|g" /etc/systemd/system/doorbell.service
sed -i "s|WorkingDirectory=/path/to/your/script|WorkingDirectory=$(pwd)|g" /etc/systemd/system/doorbell.service
sed -i "s|/path/to/doorbell|$(pwd)|g" /etc/systemd/system/doorbell.service
sed -i "s|User=your_user|User=$(whoami)|g" /etc/systemd/system/doorbell.service

# Enable and start the service
sudo systemctl enable doorbell
sudo systemctl start doorbell

echo "Setup complete"
