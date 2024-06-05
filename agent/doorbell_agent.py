"""Button Agent that listens to button presses and publishes messages to the broker."""

# pylint: disable=import-error
import os
import time
import threading
import datetime
import json
import logger
import base
import gpiozero


# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class DoorbellAgent(base.BaseAgent):
    """Button Agent that listens to button presses and publishes messages to the broker."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client."""
        super().__init__(mqtt_client)
        self._location_topic = f"{self._mqtt_topic}/{self._agent_location}"

    def run(self):
        """Subscribe to the mqtt topic and start listening for button presses."""
        self._mqtt.subscribe(self._location_topic)
        LOGGER.debug("Subscribed to topic: %s", self._location_topic)
        self._mqtt.message_callback_add(
            self._location_topic,
            self._on_doorbell_message,
        )
        LOGGER.debug("Added callback for topic: %s", self._location_topic)
        LOGGER.info("%s listening", self._agent_location)

        pin_map_floors = json.loads(os.environ.get("PIN_MAP_FLOORS"))

        try:
            for floor in pin_map_floors:
                floor_name = floor["name"]
                pin = floor["pin"]
                LOGGER.debug(
                    "Initializing button-listener for floor: %s", floor["name"]
                )
                threading.Thread(
                    target=self.button_listener, args=(floor_name, pin), daemon=True
                ).start()
        # pylint: disable=broad-except
        except Exception as e:
            LOGGER.error("Failed to initialize button-listeners: %s", str(e))

    def button_listener(self, floor_name, pin):
        """Listen for button presses and publish a message to the broker when a button is pressed.

        param floor_name: The name of the floor the button is located on.
        param pin: The GPIO pin the button is connected to.
        """
        button = gpiozero.Button(
            pin, pull_up=os.environ.get("PIN_PULL_UP_MODE") == "True"
        )
        last_pressed = datetime.datetime(1970, 1, 1)

        while True:
            if button.is_pressed:
                LOGGER.debug("Button %s pressed", floor_name)
                if self.check_press_trigger(last_pressed):
                    continue

                last_pressed = datetime.datetime.now()

                self._on_button_pressed(floor_name)
                time.sleep(0.5)

    # pylint: disable=unused-argument
    def _on_doorbell_message(self, client, userdata, msg):
        """Process the doorbell message.

        param client: The client instance for this callback.
        param userdata: The private user data as set in Client() or userdata_set().
        param msg: An instance of MQTTMessage.
        """
        LOGGER.info("Mqtt message received: %s", msg.payload.decode("utf-8"))

    def _on_button_pressed(self, floor_name):
        """Publish a message to the broker when a button is pressed.

        param floor_name: The name of the floor the button is located on.
        """
        LOGGER.info(
            "%s: Button %s pressed at %s",
            self._agent_location,
            floor_name,
            datetime.datetime.now(),
        )
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

    @staticmethod
    def check_press_trigger(last_pressed):
        """Check if the button press is a trigger or a bounce.

        param last_pressed: The last time the button was pressed.
        """
        if (datetime.datetime.now() - last_pressed).total_seconds() < 5:
            return True
        return False
