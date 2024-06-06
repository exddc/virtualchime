"""RFID agent module."""

# pylint: disable=import-error
import threading
import time
import json
import mfrc522
import logger
import base

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class RfidAgent(base.BaseAgent):
    """RFID agent module."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client.

        param mqtt_client: The MQTT client to use.
        """
        super().__init__(mqtt_client)
        self._rfid = mfrc522.MFRC522()
        self._rfid_thread = threading.Thread(target=self._read_rfid, daemon=True)

    def run(self):
        """Run the agent and start listening for RFID tags."""
        self._rfid_thread.start()
        LOGGER.info("RFID: %s listening", self._agent_location)

    def _read_rfid(self):
        """Read RFID tags and publish the tag to the broker."""
        while True:
            # pylint: disable=unused-variable
            (status, tag_type) = self._rfid.MFRC522_Request(self._rfid.PICC_REQIDL)
            if status == self._rfid.MI_OK:
                (status, uid) = self._rfid.MFRC522_Anticoll()
                if status == self._rfid.MI_OK:
                    uid_str = str(uid[0]) + str(uid[1]) + str(uid[2]) + str(uid[3])
                    LOGGER.info("RFID tag detected: %s", uid_str)
                    self._mqtt.publish(f"rfid/{self._agent_location}/try", uid_str)
                    self._on_rfid_detected(uid_str)
                    time.sleep(0.5)

    def _on_rfid_detected(self, uid_str):
        """Callback for when an RFID tag is detected.

        param uid_str: The UID of the RFID tag.
        """
        try:
            if uid_str == "3565216":
                LOGGER.info("Test tag detected")
                self._mqtt.publish(
                    f"rfid/{self._agent_location}/ack", "Test tag detected"
                )
                self._mqtt.publish(
                    f"relay/{self._agent_location}",
                    json.dumps({"name": "relay1", "state": "toggle"}),
                )
                return

        # pylint: disable=broad-except
        except Exception as e:
            LOGGER.error("Failed to send RFID tag to server: %s", str(e))
