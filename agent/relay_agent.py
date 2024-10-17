"""Relay agent that listens to messages from the broker and switches the relays on or off."""

# pylint: disable=import-error
import datetime
import time
import json
import os
import RPi.GPIO as GPIO
import logger
import base

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


# pylint: disable=too-few-public-methods
class RelayAgent(base.BaseAgent):
    """Relay agent that listens to messages from the broker and switches the relays on or off."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client."""
        super().__init__(mqtt_client)
        self._location_topic = f"{self._mqtt_topic}/{self._agent_location}"
        self._relays = []

        # Button Configuration
        GPIO.setmode(GPIO.BCM)


    def run(self):
        """Subscribe to the mqtt topic and start listening for relay messages."""
        self._mqtt.subscribe(f"relay/{self._agent_location}")
        LOGGER.debug("Subscribed to topic: relay/%s", self._agent_location)
        self._mqtt.message_callback_add(
            f"relay/{self._agent_location}", self._on_relay_message
        )
        LOGGER.debug("Added callback for topic: relay/%s", self._agent_location)
        LOGGER.info("Relay Agent %s listening", self._agent_location)

        __pin_map_relay = json.loads(os.environ.get("PIN_MAP_RELAYS"))
        try:
            for relay in __pin_map_relay:
                __relay_name = relay["name"]
                __pin = relay["pin"]
                LOGGER.debug("Initializing relay: %s", __relay_name)

                GPIO.setup(__pin, GPIO.OUT)
                GPIO.output(__pin, GPIO.HIGH)
                self._relays.append({"name": __relay_name, "pin": __pin})
        except KeyError as e:
            LOGGER.error("Error initialising relays: %s", e)
            return

    # pylint: disable=unused-argument
    def _on_relay_message(self, client, userdata, msg):
        LOGGER.info("Mqtt message received: %s", msg.payload)
        if msg.payload is not None:
            __payload = json.loads(msg.payload)
            __action = __payload["action"]
            __target_relay = __payload["name"]
            __target_state = __payload["state"]

            if __action == "toggle":
                self._toggle_relay(__target_relay)
            elif __action == "set":
                self._set_relay(__target_relay, __target_state)
            elif __action == "status":
                self._get_relay_status(__target_relay)
            elif __action == "list":
                self._mqtt.publish(
                    f"relay/{self._agent_location}",
                    json.dumps(
                        {
                            "state": "list",
                            "timestamp": str(datetime.datetime.now()),
                            "location": f"{self._agent_location}",
                            "relays": [relay["name"] for relay in self._relays],
                        },
                    ),
                )
            else:
                LOGGER.error("Unknown action recieved: %s", __action)
        else:
            LOGGER.error("Empty relay message received")

    def _toggle_relay(self, relay):
        """Toggle the relay."""
        for relay in self._relays:
            __relay_name = relay["name"]
            __pin = relay["pin"]
            if __relay_name == relay:
                try:
                    GPIO.output(__pin, not GPIO.input(__pin))
                    LOGGER.info("Relay %s toggled", __relay_name)
                # pylint: disable=broad-except
                except Exception as e:
                    LOGGER.error("Failed to access relay %s: %s", __relay_name, str(e))

    def _set_relay(self, relay_name, state):
        """Set the relay to the given state."""
        for relay in self._relays:
            __relay_name = relay["name"]
            __pin = relay["pin"]
            if __relay_name == relay_name:
                try:
                    if state == "on":
                        GPIO.output(__pin, GPIO.LOW)
                        LOGGER.info("Relay %s turned on", __relay_name)
                    elif state == "off":
                        GPIO.output(__pin, GPIO.HIGH)
                        LOGGER.info("Relay %s turned off", __relay_name)
                    elif state.startswith("burst"):
                        LOGGER.info("Relay %s burst", __relay_name)
                        __amount = int(state.split("burst")[1]) if state.split("burst")[1] else 1
                        for _ in range(__amount):
                            GPIO.output(__pin, GPIO.LOW)
                            LOGGER.info("Relay %s burst", __relay_name)
                            time.sleep(0.5)
                            GPIO.output(__pin, GPIO.HIGH)
                            time.sleep(0.5)
                    else:
                        LOGGER.error("Unknown state: %s", state)
                # pylint: disable=broad-except
                except Exception as e:
                    LOGGER.error("Failed to access relay %s: %s", __relay_name, str(e))

    def _get_relay_status(self, relay):
        """Get the status of the relay."""
        for relay in self._relays:
            __relay_name = relay["name"]
            __pin = relay["pin"]
            if __relay_name == relay:
                try:
                    self._mqtt.publish(
                        f"relay/{self._agent_location}/{__relay_name}",
                        json.dumps(
                            {
                                "state": "status",
                                "timestamp": str(datetime.datetime.now()),
                                "location": f"{self._agent_location}",
                                "value": "on" if GPIO.input(__pin) == GPIO.LOW else "off",
                            },
                        ),
                    )
                # pylint: disable=broad-except
                except Exception as e:
                    LOGGER.error("Failed to access relay %s: %s", __relay_name, str(e))

    def stop(self):
        """Stop the agent."""
        self._mqtt.stop()
        self._mqtt.unsubscribe(f"relay/{self._agent_location}")
        self._mqtt.message_callback_remove(f"relay/{self._agent_location}")
        GPIO.cleanup()
        LOGGER.info("Relay agent stopped")
