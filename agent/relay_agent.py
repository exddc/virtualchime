"""Relay agent that listens to messages from the broker and switches the relays on or off."""

# pylint: disable=import-error
import datetime
import json
import os
import logger
import base
import gpiozero

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


# pylint: disable=too-few-public-methods
class RelayAgent(base.BaseAgent):
    """Relay agent that listens to messages from the broker and switches the relays on or off."""

    def run(self):
        """Subscribe to the mqtt topic and start listening for relay messages."""
        self._mqtt.subscribe(f"relay/{self._agent_location}")
        LOGGER.debug("Subscribed to topic: relay/%s", self._agent_location)
        self._mqtt.message_callback_add(
            f"relay/{self._agent_location}", self._on_relay_message
        )
        LOGGER.debug("Added callback for topic: relay/%s", self._agent_location)
        LOGGER.info("Relay Agent %s listening", self._agent_location)

    # pylint: disable=unused-argument
    def _on_relay_message(self, client, userdata, msg):
        LOGGER.info("Mqtt message received: %s", msg.payload)
        self._toggle_relay(msg.payload.decode("utf-8"))

    def _toggle_relay(self, payload):
        pin_map_relay = json.loads(os.environ.get("PIN_MAP_RELAYS"))
        target = json.loads(payload)
        target_relay = target["name"]
        target_state = target["state"]
        for relay in pin_map_relay:
            relay_name = relay["name"]
            pin = relay["pin"]
            if relay_name == target_relay:
                relay = gpiozero.OutputDevice(
                    pin, active_high=False, initial_value=False
                )
                try:
                    if target_state == "on":
                        relay.on()
                        LOGGER.info("Relay %s turned on", relay_name)
                    elif target_state == "off":
                        relay.off()
                        LOGGER.info("Relay %s turned off", relay_name)
                    elif target_state == "toggle":
                        relay.toggle()
                        LOGGER.info("Relay %s toggled", relay_name)
                    elif target_state == "burst":
                        relay.blink(on_time=0.5, off_time=0.5, n=5)
                        LOGGER.info("Relay %s burst", relay_name)
                    elif target_state == "status":
                        LOGGER.info("Relay %s state: %s", relay_name, relay.value)
                    else:
                        LOGGER.error("Unknown state: %s", target_state)
                # pylint: disable=broad-except
                except Exception as e:
                    LOGGER.error("Failed to acces relay %s: %s", relay_name, str(e))

                self._mqtt.publish(
                    f"relay/{self._agent_location}/{relay_name}",
                    json.dumps(
                        {
                            "state": "status",
                            "timestamp": str(datetime.datetime.now()),
                            "location": f"{self._agent_location}",
                            "value": relay.value,
                        },
                    ),
                )
                return

        LOGGER.error("Relay %s not found", target_relay)
        return

    def stop(self):
        """Stop the agent."""
        self._mqtt.stop()
        self._mqtt.unsubscribe(f"relay/{self._agent_location}")
        self._mqtt.message_callback_remove(f"relay/{self._agent_location}")
        LOGGER.info("Relay agent stopped")
