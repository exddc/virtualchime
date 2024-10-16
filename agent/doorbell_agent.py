"""Button Agent that listens to button presses and publishes messages to the broker."""

# pylint: disable=import-error,consider-using-from-import
import os
import time
import datetime
import json
import logger
import base
import RPi.GPIO as GPIO

# Initialize logger
LOGGER = logger.get_module_logger(__name__)

class DoorbellAgent(base.BaseAgent):
    """Button Agent that listens to button presses and publishes messages to the broker."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client."""
        super().__init__(mqtt_client)
        self._location_topic = f"{self._mqtt_topic}/{self._agent_location}"

        # Button Configuration
        GPIO.setmode(GPIO.BCM)

        self._pin_pullup_mode = GPIO.PUD_UP if os.environ.get("PIN_PULL_UP_MODE") == "True" else GPIO.PUD_DOWN
        self._debounce_time = int(os.environ.get("PIN_DOORBELL_DEBOUNCE_TIME", 1000))

        self._last_press_time = {}
        self._stuck_button_count = {}

    def run(self):
        """Subscribe to the mqtt topic and start listening for button presses."""
        self._mqtt.subscribe(self._location_topic)
        LOGGER.debug("Subscribed to topic: %s", self._location_topic)
        self._mqtt.message_callback_add(
            self._location_topic,
            self._on_doorbell_message,
        )
        LOGGER.debug("Added callback for topic: %s", self._location_topic)
        LOGGER.info("Doorbell Agent %s listening", self._agent_location)

        pin_map_floors = json.loads(os.environ.get("PIN_MAP_FLOORS"))

        try:
            for floor in pin_map_floors:
                floor_name = floor["name"]
                pin = floor["pin"]
                LOGGER.debug(
                    "Initializing button-listener for floor: %s", floor["name"]
                )

                GPIO.setup(pin, GPIO.IN, pull_up_down=self._pin_pullup_mode)

                GPIO.add_event_detect(
                    pin,
                    GPIO.FALLING,
                    callback=lambda channel, floor=floor_name: self._on_button_pressed(
                        floor
                    )
                )

                # Initialize press tracking for each floor
                self._last_press_time[floor_name] = datetime.datetime.now()
                self._stuck_button_count[floor_name] = 0

        except Exception as e:
            LOGGER.error("Failed to initialize button-listeners: %s", str(e))

    def _on_button_pressed(self, floor_name):
        """Handle button press events with detection of stuck buttons.

        param floor_name: The name of the floor the button is located on.
        """

        LOGGER.info(
            "%s: Button %s pressed at %s",
            self._agent_location,
            floor_name,
            datetime.datetime.now(),
        )

        if self.check_press_trigger(self._last_press_time[floor_name]):
            self._mqtt.publish(
                    f"{self._mqtt_topic}/{floor_name}/{self._agent_location}",
                    json.dumps(
                        {
                            "state": "pressed",
                            "timestamp": str(datetime.datetime.now()),
                            "location": self._agent_location,
                            "floor": floor_name,
                        }
                    ),
                )
            self._stuck_button_count[floor_name] = 0
        else:
            self._stuck_button_count[floor_name] += 1
            if self._stuck_button_count[floor_name] > 50:
                LOGGER.error(
                    "%s: Button %s is stuck",
                    self._agent_location,
                    floor_name,
                )
                self._stuck_button_count[floor_name] = 0

        self._last_press_time[floor_name] = datetime.datetime.now()

    def check_press_trigger(self, last_pressed):
        """Check if the button press is a trigger or a bounce.
        param last_pressed: The last time the button was pressed.
        """
        if (datetime.datetime.now() - last_pressed).total_seconds() > self._debounce_time:
            return True
        return False
    
    # pylint: disable=unused-argument
    def _on_doorbell_message(self, client, userdata, msg):
        """Process the doorbell message.

        param client: The client instance for this callback.
        param userdata: The private user data as set in Client() or userdata_set().
        param msg: An instance of MQTTMessage.
        """
        LOGGER.info("Mqtt message received: %s", msg.payload.decode("utf-8"))

    def stop(self):
        """Stop the agent."""
        self._mqtt.unsubscribe(self._location_topic)
        self._mqtt.message_callback_remove(self._location_topic)
        GPIO.cleanup()
        LOGGER.info("Agent stopped")
