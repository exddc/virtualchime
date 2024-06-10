"""General Agent that creates a thread for each agent and module and runs them"""

# pylint: disable=import-error, consider-using-from-import
import time
import os
import logger
import dotenv
import mqtt_agent
import doorbell_agent
import indoor_unit_agent
import RPi.GPIO as GPIO

# Load environment variables
dotenv.load_dotenv()

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


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

        self._mqtt.start()
        self._select_agent()
        self._select_modules()

    def run(self):
        """Run the agent and all modules"""

        LOGGER.info("%s agent started.", self.__agent_type)
        self._agent.run()
        for module in self._modules:
            module.run()

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

    def stop(self):
        """Stop the agent and all modules"""


if __name__ == "__main__":
    agent = Agent()
    agent.run()

    # Keep main thread alive
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        agent.stop()
    # pylint: disable=broad-except
    except Exception as e:
        LOGGER.error("Agent failed: %s", str(e))
        agent.stop()
