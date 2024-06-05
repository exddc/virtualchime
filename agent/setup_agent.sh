#!/bin/bash
# Setup scrpit to automate the initial setup of the agent

echo "Welcome to the agent setup script"
echo "This script will guide you through the setup of the agent"

# Install needed packages
echto "Install needed apt packages"
sudo apt-get update
sudo apt-get install -y dialog
sudo apt-get install -y python3 python3-venv python3-pip

# Install python packages
echo "Installing python packages"
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
deactivate

# Create a config file from user inputs
# Language
HEIGHT=15
WIDTH=40
CHOICE_HEIGHT=4
BACKTITLE="Language Selection"
TITLE="Select Language"
MENU="Choose one of the following options:"

OPTIONS=(1 "en"
         2 "de")

CHOICE=$(dialog --clear \
                --backtitle "$BACKTITLE" \
                --title "$TITLE" \
                --menu "$MENU" \
                $HEIGHT $WIDTH $CHOICE_HEIGHT \
                "${OPTIONS[@]}" \
                2>&1 >/dev/tty)

case $CHOICE in
    1)
        LANGUAGE="en"
        ;;
    2)
        LANGUAGE="de"
        ;;
esac

echo "You selected: $LANGUAGE"

# Property name
echo "The property name is the name of the property the doorbell is installed at. I.e. 'Home', 'Office' or 'Street name 123'"
PROPERTY_NAME=$(dialog --inputbox "Enter the property name:" 8 40 2>&1 >/dev/tty)
echo "Property name: $PROPERTY_NAME"

# Agent location
echo "The doorbell location is the physical location. I.e. 'Front door' or 'Gate'"
AGENT_LOCATION=$(dialog --inputbox "Enter the location:" 8 40 2>&1 >/dev/tty)

# MQTT broker
BACKTITLE="Custom MQTT Broker"
TITLE="Custom MQTT Broker"
MENU="Do you want to use a custom MQTT broker?:"
OPTIONS=(1 "Yes"
         2 "No")

CHOICE=$(dialog --clear \
                --backtitle "$BACKTITLE" \
                --title "$TITLE" \
                --menu "$MENU" \
                $HEIGHT $WIDTH $CHOICE_HEIGHT \
                "${OPTIONS[@]}" \
                2>&1 >/dev/tty)

case $CHOICE in
    1)
        CUSTOM_BROKER=true
        ;;
    2)
        CUSTOM_BROKER=false
        ;;
esac

if [ "$CUSTOM_BROKER" = true ]; then
    MQTT_BROKER_IP=$(dialog --inputbox "Enter the MQTT broker IP address:" 8 40 2>&1 >/dev/tty)
    MQTT_BROKER_PORT=$(dialog --inputbox "Enter the MQTT broker port:" 8 40 2>&1 >/dev/tty)
    MQTT_USERNAME=$(dialog --inputbox "Enter the MQTT username:" 8 40 2>&1 >/dev/tty)
    MQTT_PASSWORD=$(dialog --inputbox "Enter the MQTT password:" 8 40 2>&1 >/dev/tty)
fi

# Create the config file from the .env.sample file
echo "Creating the .env file"
cp .env.sample .env
sed -i "s/LANGUAGE=en/LANGUAGE=$LANGUAGE/g" .env
sed -i "s/PROPERTY_NAME=MyHouse/PROPERTY_NAME=$PROPERTY_NAME/g" .env
sed -i "s/AGENT_LOCATION=door/AGENT_LOCATION=$AGENT_LOCATION/g" .env

if [ "$CUSTOM_BROKER" = true ]; then
    sed -i "s/MQTT_BROKER_IP=192.168.178.1/MQTT_BROKER_IP=$MQTT_BROKER_IP/g" .env
    sed -i "s/MQTT_BROKER_PORT=1883/MQTT_BROKER_PORT=$MQTT_BROKER_PORT/g" .env
    sed -i "s/MQTT_USERNAME=mqtt_user/MQTT_USERNAME=$MQTT_USERNAME/g" .env
    sed -i "s/MQTT_PASSWORD=mqtt_password/MQTT_PASSWORD=$MQTT_PASSWORD/g" .env
fi

# Create a systemd service
echo "Creating the systemd service"
cp doorbell.service /etc/systemd/system/doorbell.service
sed -i "s|ExecStart=/path/to/your/venv/bin/python /path/to/your/script.py|ExecStart=$(pwd)/.venv/bin/python $(pwd)/doorbell.py|g" /etc/systemd/system/doorbell.service
sed -i "s|/path/to/doorbell|$(pwd)|g" /etc/systemd/system/doorbell.service
sed -i "s|User=your_user/User=$(whoami)|g" /etc/systemd/system/doorbell.service

# Enable and start the service
sudo systemctl enable doorbell
sudo systemctl start doorbell

echo "Setup complete"
