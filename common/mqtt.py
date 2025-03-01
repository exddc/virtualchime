"""MQTT Module for the Virtual Chime System"""

import os
import time
import datetime
import threading
import orjson
from loguru import logger
import paho.mqtt.client

# Load configuration file
CONFIG_FILE = os.path.join(os.path.dirname(__file__), "..", "..", "config", "general.json")
with open(CONFIG_FILE, "r") as file:
    config = orjson.load(file)

# Extract configuration values
MQTT_BROKER = config['mqtt']['broker']
MQTT_PORT = config['mqtt']['port']
MQTT_USERNAME = config['mqtt']['username']
MQTT_PASSWORD = config['mqtt']['password']


class MQTT_Agent(paho.mqtt.client.Client):
    """MQTT Agent Module"""

    def __init__(self, client_id: str, topics: list, version: str) -> None:
        """Initialize the agent with the client ID and topics to subscribe to.

        param client_id: The client ID for the MQTT client.
        param topics: The topics to subscribe to.
        """

        self._topics = [] if topics is None else topics
        self._version = version
        super().__init__(client_id)

    def __enter__(self) -> "MQTT_Agent":
        """Start the MQTT client when entering with block."""
        self.start()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        """Stop the MQTT client when exiting with block.

        param exc_type: The exception type.
        param exc_value: The exception value.
        param traceback: The traceback.
        """
        self.stop()

    def start(self) -> None:
        """Start the MQTT client and connect to the broker."""
        self.username_pw_set(
            username=MQTT_USERNAME, password=MQTT_PASSWORD
        )
        self.on_connect = logger.info("Connected to MQTT Broker")
        self.on_disconnect = logger.warning("Disconnected from MQTT Broker")
        logger.debug(
            "Connection Info: %s:%d",
            MQTT_BROKER,
            MQTT_PORT,
        )
        try:
            self.connect(
                    MQTT_BROKER,
                    MQTT_PORT,
            )
        except Exception as e:
            logger.error("Failed to connect to MQTT Broker: %s", str(e))
            while True:
                try:
                    self.connect(
                        MQTT_BROKER,
                        MQTT_PORT,
                    )
                    break
                except Exception as ex:
                    logger.error("Failed to connect to MQTT Broker: %s", str(ex))
                    time.sleep(5)

        self._start_heartbeat()
        self.loop_start()

    def stop(self) -> None:
        """Stop the MQTT client and disconnect from the broker."""
        self.publish(
            f"status/{os.environ.get('AGENT_LOCATION')}",
            orjson.dumps(
                {"state": "offline", "date": datetime.datetime.now().isoformat()}
            ),
        )
        # pylint: disable=pointless-statement
        self._stop_heartbeat
        self.loop_stop()
        self.disconnect()

    # pylint: disable=unused-argument
    def _subscribe_on_connect(self, client, userdata, flags, rc) -> None:
        """Subscribe to topics when connected to the broker.

        param client: The client instance for this callback.
        param userdata: The private user data as set in Client() or userdata_set().
        param flags: Response flags sent by the broker.
        """
        if rc == 0:
            logger.info("Connected to MQTT Broker!")
        else:
            logger.error("Failed to connect, return code %d", rc)

        self.subscribe_to_topics(self._topics)
        # pylint: disable=attribute-defined-outside-init
        self.on_message = self._on_message_callback

    def subscribe_to_topics(self, topics: list) -> None:
        """Subscribe to a list of topics.

        param topics: The topics to subscribe to.
        """
        for topic in topics:
            self.subscribe(topic)
            logger.info("Subscribed to topic %s", topic)

    # pylint: disable=unused-argument
    def _on_message_callback(self, client, userdata, msg) -> None:
        """Callback for message received from the broker.

        param client: The client instance for this callback.
        param userdata: The private user data as set in Client() or userdata_set().
        param msg: An instance of MQTTMessage.
        """
        logger.info(
            "Received message from topic %s: %s", msg.topic, msg.payload.decode()
        )
        self._process_message(msg.topic, msg.payload.decode())

    # pylint: disable=unused-argument
    def _on_message(self, client, userdata, msg) -> None:
        """Callback for message received from the broker.

        param client: The client instance for this callback.
        param userdata: The private user data as set in Client() or userdata_set().
        param msg: An instance of MQTTMessage.
        """
        logger.info(
            "Received message from topic %s: %s", msg.topic, msg.payload.decode()
        )
        self._process_message(msg.topic, msg.payload.decode())

    # pylint: disable=unused-argument
    def _process_message(self, topic: str, payload: str) -> None:
        """Process the message received from the broker.

        param topic: The topic the message was received on.
        param payload: The message payload.
        """

        logger.info("Processed message from topic %s: %s", topic, payload)

    def publish(self, topic: str, payload: str, log_info: bool = True) -> None:
        """Publish a message to the broker.

        param topic: The topic to publish the message to.
        param payload: The message payload.
        param log_info: Whether to log the message.
        """
        super().publish(topic, payload)
        if log_info:
            logger.info("Published message %s to topic %s", payload, topic)

    def __del__(self) -> None:
        """Stop the MQTT client when the object is deleted."""
        self.stop()
        super().__del__()

    def _start_heartbeat(self) -> None:
        """Start publishing heartbeat messages to the broker."""
        logger.info("Starting heartbeat")
        # pylint: disable=attribute-defined-outside-init
        self._hearbeat_thread = threading.Thread(target=self._publish_heartbeat)
        self._hearbeat_thread.start()

    def _publish_heartbeat(self) -> None:
        """Publish heartbeat messages to the broker."""
        while True:
            try:
                self.publish(
                    f"status/{os.environ.get('AGENT_LOCATION')}",
                    json.dumps(
                        {
                            "state": "online",
                            "date": datetime.datetime.now().isoformat(),
                            "version": self._version,
                        }
                    ),
                    False,
                )
                time.sleep(int(os.environ.get("MQTT_ALIVE_INTERVAL")))
            except KeyboardInterrupt:
                logger.info("Received Ctrl+C, stopping heartbeat...")
                break
            # pylint: disable=broad-except
            except Exception as e:
                logger.error("Failed to publish heartbeat message: %s", str(e))

    def _stop_heartbeat(self) -> None:
        """Stop publishing heartbeat messages to the broker."""
        # pylint: disable=no-member
        self._hearbeat_thread.stop()