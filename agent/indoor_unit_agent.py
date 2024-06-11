"""Indoor Unit Agent that plays a sound on doorbell ring."""

# pylint: disable=import-error
import os
import time
import logger
import dotenv


# Load environment variables
dotenv.load_dotenv()

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


# pylint: disable=too-few-public-methods
class IndoorUnitAgent:
    """Indoor Unit Agent that plays a sound on doorbell ring."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client."""
        self._mqtt = mqtt_client
        self.__sound_bell_file = os.environ.get("SOUND_BELL_FILE")
        self.__sound_startup_file = os.environ.get("SOUND_STARTUP_FILE")
        self.__location = os.environ.get("AGENT_LOCATION")

    def run(self):
        """Subscribe to the mqtt topic and start listening for doorbell rings."""
        self._mqtt.subscribe(f"doorbell/{self.__location}")
        LOGGER.debug("Subscribed to topic: doorbell/%s", self.__location)
        self._mqtt.message_callback_add(
            f"doorbell/{self.__location}", self._on_doorbell_message
        )
        LOGGER.debug("Added callback for topic: doorbell/%s", self.__location)
        self._sound_startup()
        LOGGER.info("%s listening", self.__location)

    # pylint: disable=unused-argument
    def _on_doorbell_message(self, client, userdata, msg):
        """Callback for doorbell ring message.

        param client: The client instance for this callback
        param userdata: The private user data as set in Client() or userdata_set()
        param msg: The message received from the broker
        """
        LOGGER.info("Doorbell ring received")
        self._play_doorbell_ring()

    def _sound_startup(self):
        """Play the startup sound."""
        if os.environ.get("SOUND_PLAY_STARTUP_SOUND") == "True":
            if os.environ.get("SOUND_TYPE") == "ALSA":
                LOGGER.info("Playing startup sound")
                os.system("/usr/bin/aplay " + self.__sound_startup_file)
                LOGGER.info("Startup sound played")
            elif os.environ.get("SOUND_TYPE") == "MOCK":
                LOGGER.info("Playing startup sound")
                time.sleep(1)
                LOGGER.info("Startup sound played")
            else:
                LOGGER.error("Unknown sound type")
                # add mqtt error message

    def _play_doorbell_ring(self):
        """Play the doorbell ring sound."""
        if os.environ.get("SOUND_TYPE") == "ALSA":
            LOGGER.info("Playing doorbell ring")
            os.system("/usr/bin/aplay " + self.__sound_bell_file)
            LOGGER.info("Doorbell ring played")
        elif os.environ.get("SOUND_TYPE") == "MOCK":
            LOGGER.info("Playing doorbell ring")
            time.sleep(1)
            LOGGER.info("Doorbell ring played")
        else:
            LOGGER.error("Unknown sound type")
            # add mqtt error message

    def stop(self):
        """Stop the agent."""
        self._mqtt.stop()
        self._mqtt.unsubscribe(f"doorbell/{self.__location}")
        self._mqtt.message_callback_remove(f"doorbell/{self.__location}")
        os.system("/usr/bin/killall aplay")
        LOGGER.info("Agent stopped")
