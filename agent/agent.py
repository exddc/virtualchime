"""General Agent that creates a thread for each agent and module and runs them"""

# pylint: disable=import-error, consider-using-from-import
import time
import os
import threading
from pathlib import Path

import RPi.GPIO as GPIO
import logger
import dotenv
import mqtt_agent
import doorbell_agent
import indoor_unit_agent
import web_server


# Load environment variables
dotenv.load_dotenv()
env_path = Path(".") / ".env"

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


def get_version():
    """Get the version from the VERSION file."""
    version_file = Path(__file__).resolve().parent.parent / "VERSION"
    with open(version_file) as f:
        return f.read().strip()


__version__ = get_version()


# pylint: disable=too-many-instance-attributes
class Agent:
    """General Agent that creates a thread for each agent and module and runs them."""

    def __init__(self):
        """Initialize the agent and all modules."""
        GPIO.cleanup()
        self.__agent_location = os.environ.get("AGENT_LOCATION")
        self.__agent_type = os.environ.get("AGENT_TYPE")
        self._mqtt = mqtt_agent.MqttAgent(
            f"{self.__agent_type}_{self.__agent_location}",
            [],
        )
        self._agent = None
        self._modules = []
        self._has_internet = False

        self._mqtt.start()
        self._select_agent()
        self._select_modules()

        self._web_server = web_server.WebServer(self._mqtt)
        self._internet_connection = threading.Thread(
            target=self._check_internet_connection, daemon=True
        )

    def run(self):
        """Run the agent and all modules"""

        LOGGER.info(
            "%s agent starting with version: %s", self.__agent_type, __version__
        )
        self._agent.run()
        for module in self._modules:
            module.run()
        self._internet_connection.start()
        self._web_server.run()

    def _select_agent(self):
        """Select the agent based on the agent type."""
        if self.__agent_type == "doorbell":
            self._agent = doorbell_agent.DoorbellAgent(self._mqtt)
        elif self.__agent_type == "indoor_unit":
            self._agent = indoor_unit_agent.IndoorUnitAgent(self._mqtt)
        else:
            LOGGER.error(msg := "Unknown agent type")
            raise NameError(msg)

    def _select_modules(self):
        """Select the modules based on the modules provided in the environment variables."""
        self.__modules = list(os.environ.get("MODULES").split(","))
        LOGGER.debug("Modules selected: %s", self.__modules)

        for module in self.__modules:
            if module == "relay":
                # pylint: disable=import-outside-toplevel
                import relay_agent

                self._modules.append(relay_agent.RelayAgent(self._mqtt))
            elif module == "rfid" and os.environ.get("PIN_TYPE") == "GPIO":
                # pylint: disable=import-outside-toplevel
                import rfid_agent

                self._modules.append(rfid_agent.RfidAgent(self._mqtt))
            elif module == "video":
                # pylint: disable=import-outside-toplevel
                import video_agent

                self._modules.append(video_agent.VideoAgent(self._mqtt))
            else:
                LOGGER.error(msg := "Unknown module")
                raise NameError(msg)
        LOGGER.debug("Modules loaded: %s", self._modules)

    def stop(self):
        """Stop the agent and all modules"""
        for module in self._modules:
            module.stop()
        self._agent.stop()
        self._mqtt.stop()
        self._web_server.stop()
        self._internet_connection.join()
        GPIO.cleanup()
        LOGGER.info("%s %s agent stopped.", self.__agent_type, self.__agent_location)

    def _check_internet_connection(self):
        """Thread to check the internet connection and publish the status to the MQTT broker."""
        while True:
            if os.system("ping -c 1 www.google.com") == 0:
                if not self._has_internet:
                    LOGGER.info("Internet connection established.")
                self._has_internet = True
            else:
                self._has_internet = False
                LOGGER.warning("No internet connection. Retrying in 60 seconds.")
            time.sleep(60)


def watch_env_file(agent_instance):
    """Watch the .env file for changes and reload the environment variables."""
    last_mod_time = os.path.getmtime(env_path)

    while True:
        try:
            current_mod_time = os.path.getmtime(env_path)
            if current_mod_time != last_mod_time:
                LOGGER.info(
                    "Changes detected in .env file. Reloading and restarting agent..."
                )
                dotenv.load_dotenv(dotenv_path=env_path, override=True)
                agent_instance.stop()
                LOGGER.info("Agent stopped due to .env changes. Restarting...")
                agent_instance = Agent()
                agent_instance.run()
                last_mod_time = current_mod_time
        # pylint: disable=broad-except
        except Exception as ex:
            LOGGER.error("Error watching .env file: %s", str(ex))
        time.sleep(10)


if __name__ == "__main__":
    while True:
        try:
            agent = Agent()
            agent.run()

            watcher_thread = threading.Thread(target=watch_env_file, args=(agent,))
            watcher_thread.daemon = True
            watcher_thread.start()

            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            LOGGER.info("KeyboardInterrupt received. Stopping agent.")
            agent.stop()
            break
        # pylint: disable=broad-except
        except Exception as e:
            LOGGER.error("Agent failed: %s", str(e))
            agent.stop()
            LOGGER.info("Restarting agent...")
            time.sleep(0.5)
