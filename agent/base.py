"""Base class for all agents."""

# pylint: disable=import-error
import os
import dotenv
import logger
import gpiozero

# Load environment variables
dotenv.load_dotenv()

# Initialize logger
LOGGER = logger.get_module_logger(__name__)

if os.environ.get("PIN_TYPE") == "MOCK":
    from gpiozero.pins.mock import MockFactory

    gpiozero.Device.pin_factory = MockFactory()
else:
    from gpiozero.pins.rpigpio import RPiGPIOFactory

    gpiozero.Device.pin_factory = RPiGPIOFactory()


# pylint: disable=too-few-public-methods
class BaseAgent:
    """Base class for all agents."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client."""
        self._mqtt = mqtt_client
        self._agent_location = os.environ.get("AGENT_LOCATION")
        self._test_mode = os.environ.get("TEST_MODE") == "True"
        self.__set_mqtt_topic()

    def __set_mqtt_topic(self):
        """Set the MQTT topic based on the test mode."""
        __test_mode_topic = "test/" if self._test_mode else ""
        self._mqtt_topic = f"{__test_mode_topic}doorbell"
