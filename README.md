# Virtual Chime Open Smart Doorbell System

Virtual Chime is an open source smart doorbell system that allows you to see and talk to visitors at your front door from anywhere in the world. The system is built using a Raspberry Pi and a few other components. The system is designed to be easy to set up and use, and it can be easliy customized.

It's designed to be a simple and affordable alternative to expensive designer doorbells. There should not be a compromise between security, privacy and good design. Virtual Chime aims to be as beautiful as designer doorbells, as secure as the most expensive security systems, and as private as your own home.

## Features

The system is still in the early stages of development, but here are some of the features that are already implemented:

-   Button press detection
-   Extensive user configuration
-   Live video streaming
-   Relay control
-   Video recording
-   3D printable enclosure
-   Support for multiple doorbells
-   Web interface for configuration and monitoring

Here are some of the features that are planned for the future:

-   RFID card reader
-   Motion detection
-   Facial recognition
-   Two-way audio communication
-   Integration with smart home systems (e.g. Home Assistant, OpenHAB, Homekit)
-   Integration with cloud services (e.g. Google Drive, Dropbox, AWS)
-   Integration with messaging services (e.g. Telegram, WhatsApp, Signal)
-   Integration with voice assistants (e.g. Google Assistant, Alexa, Siri)
-   Native mobile apps for iOS and Android
-   Self-hosted version for maximum privacy
-   Integration with other open source projects (e.g. Homebridge, OpenCV, TensorFlow)

## Hardware

The system is built using a Raspberry Pi, a Raspberry Pi Camera Module, one or more push buttons, a relay module, and a few other components. The system is powered by a USB power supply and connected to the internet via Wi-Fi.

Here is a list of the components that are currently used in the system:

-   Raspberry Pi (any model with Wi-Fi and GPIO pins)
-   Raspberry Pi Camera Module (NoIR is recommended for night vision)
-   Push button
-   Relay module
-   USB power supply
-   MicroSD card
-   3D printed enclosure
-   Various cables and connectors

Here is a list of the components that are planned for the future:

-   RFID card reader
-   Motion sensor
-   Speaker
-   Microphone
-   Display
-   Battery
-   Various sensors (e.g. temperature, humidity, light, sound)

Files for 3D printing the enclosure and the plans to build the hardware are available at the hardware repository at [virtualchime-hardware](https://github.com/exddc/virtualchime-hardware).

## Software

The system is built in Python to make it easy to customize and extend. A simple web server is used to serve the live video stream and the configuration interface. The system is designed to be modular and extensible, so you can easily add new features and integrate with other systems.

### Installation

Before you can install the software, you need to be able to checkout the code from GitHub.

If you haven't already installed Git, you can do so by running the following command in the terminal:

```bash
sudo apt-get install git
```

Once you have installed Git and added your ssh-credentials to GitHub, you can checkout the code by running the following command in the terminal:

```bash
git clone git@github.com:exddc/virtualchime.git
cd virtualchime
```

Once you have checked out the code, you can install the software by running the following command in the terminal and following the instructions:

```bash
sudo ./agent/setup_agent.sh
```

### Configuration

The setup script will guide you through the initial configuration of the system. You can also configure the system by editing the configuration file directly. The configuration file is located at `agent/.env`.

Most of the configuration options are self-explanatory, but should only be changed if you know what you are doing. If you are not sure about a particular option, you can leave it at the default value. The configuration file is well commented, so you should be able to figure out what each option does.

### Usage

Once you have installed and configured the software, the agent is running as a service in the background. You can check the status of the service by running the following command in the terminal:

```bash
sudo systemctl status doorbell.service
```

You can also start, stop, and restart the service by running the following commands in the terminal:

```bash
sudo systemctl start doorbell.service
sudo systemctl stop doorbell.service
sudo systemctl restart doorbell.service
```

### Troubleshooting

If you encounter any issues during the installation or configuration process, you can check the log file for error messages. The log file is located at `agent/logs/agent.log`.

## Contributing

If you would like to contribute to the project, you can do so by submitting a pull request. You can also open an issue if you have any questions or suggestions.

Before you submit a pull request, please make sure that your changes are in line with the project's goals and coding style. You should also make sure that your changes are well tested and documented.

## License

The project is licensed under the GNU GPL-3 License. You can find the full text of the license in the `LICENSE` file.
