# Button Agent that listens to button presses and publishes messages to the broker
# Uses the mqtt client provied by the agent.py

import datetime
import json
import logger
import os
import dotenv
import time
import subprocess

# Load environment variables
dotenv.load_dotenv()

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class IndoorUnitAgent:
    def __init__(self, mqtt_client):
        self._mqtt = mqtt_client
        self.__sound_bell_file = os.environ.get("SOUND_BELL_FILE")
        self.__sound_startup_file = os.environ.get("SOUND_STARTUP_FILE")
        self.__location = os.environ.get("AGENT_LOCATION")

    def run(self):
        self._mqtt.subscribe(f"doorbell/{self.__location}")
        LOGGER.debug(f"Subscribed to topic: doorbell/{self.__location}")
        self._mqtt.message_callback_add(
            f"doorbell/{self.__location}", self._on_doorbell_message
        )
        LOGGER.debug(f"Added callback for topic: doorbell/{self.__location}")
        self._sound_startup()
        LOGGER.info(f"{self.__location} listening")

    def _on_doorbell_message(self, client, userdata, msg):
        LOGGER.info("Doorbell ring received")
        self._play_doorbell_ring()

    def _sound_startup(self):
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
