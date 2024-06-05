"""MQTT Agent Module"""

# pylint: disable=import-error
import os
import time
import datetime
import threading
import json
import logger
import dotenv
import paho.mqtt.client

# Load environment variables
dotenv.load_dotenv()

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class MqttAgent(paho.mqtt.client.Client):
    """MQTT Agent Module"""

    def __init__(self, client_id: str, topics: list) -> None:
        """Initialize the agent with the client ID and topics to subscribe to.

        param client_id: The client ID for the MQTT client.
        param topics: The topics to subscribe to.
        """

        self._topics = [] if topics is None else topics
        # super().__init__(paho.mqtt.client.CallbackAPIVersion.VERSION2, client_id)
        super().__init__(client_id)

    def __enter__(self) -> "MqttAgent":
        """Start the MQTT client when entering with block."""
        self.start()
        return self

    # pylint: disable=unused-argument
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
            os.environ.get("MQTT_USERNAME"), os.environ.get("MQTT_PASSWORD")
        )
        # self.on_connect = self._subscribe_on_connect
        # pylint: disable=attribute-defined-outside-init
        self.on_connect = LOGGER.info("Connected to MQTT Broker")
        LOGGER.debug(
            "Connection Info: %s{}:%d",
            os.environ.get("MQTT_BROKER_IP"),
            int(os.environ.get("MQTT_BROKER_PORT")),
        )
        try:
            self.connect(
                os.environ.get("MQTT_BROKER_IP"),
                int(os.environ.get("MQTT_BROKER_PORT")),
            )
        # pylint: disable=broad-except
        except Exception as e:
            LOGGER.error("Failed to connect to MQTT Broker: %s", str(e))
            while True:
                try:
                    self.connect(
                        os.environ.get("MQTT_BROKER_IP"),
                        int(os.environ.get("MQTT_BROKER_PORT")),
                    )
                    break
                # pylint: disable=broad-except
                except Exception as ex:
                    LOGGER.error("Failed to connect to MQTT Broker: %s", str(ex))
                    time.sleep(5)

        self._start_heartbeat()
        self.loop_start()

    def stop(self) -> None:
        """Stop the MQTT client and disconnect from the broker."""
        self.publish(
            f"status/{os.environ.get('AGENT_LOCATION')}",
            json.dumps(
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
            LOGGER.info("Connected to MQTT Broker!")
        else:
            LOGGER.error("Failed to connect, return code %d", rc)

        self.subscribe_to_topics(self._topics)
        # pylint: disable=attribute-defined-outside-init
        self.on_message = self._on_message_callback

    def subscribe_to_topics(self, topics: list) -> None:
        """Subscribe to a list of topics.

        param topics: The topics to subscribe to.
        """
        for topic in topics:
            self.subscribe(topic)
            LOGGER.info("Subscribed to topic %s", topic)

    # pylint: disable=unused-argument
    def _on_message_callback(self, client, userdata, msg) -> None:
        """Callback for message received from the broker.

        param client: The client instance for this callback.
        param userdata: The private user data as set in Client() or userdata_set().
        param msg: An instance of MQTTMessage.
        """
        LOGGER.info(
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
        LOGGER.info(
            "Received message from topic %s: %s", msg.topic, msg.payload.decode()
        )
        self._process_message(msg.topic, msg.payload.decode())

    # pylint: disable=unused-argument
    def _process_message(self, topic: str, payload: str) -> None:
        """Process the message received from the broker.

        param topic: The topic the message was received on.
        param payload: The message payload.
        """

        LOGGER.info("Processed message from topic %s: %s", topic, payload)

    def publish(self, topic: str, payload: str, log_info: bool = True) -> None:
        """Publish a message to the broker.

        param topic: The topic to publish the message to.
        param payload: The message payload.
        param log_info: Whether to log the message.
        """
        super().publish(topic, payload)
        if log_info:
            LOGGER.info("Published message %s to topic %s", payload, topic)

    def __del__(self) -> None:
        """Stop the MQTT client when the object is deleted."""
        self.stop()
        super().__del__()

    def _start_heartbeat(self) -> None:
        """Start publishing heartbeat messages to the broker."""
        LOGGER.info("Starting heartbeat")
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
                        {"state": "online", "date": datetime.datetime.now().isoformat()}
                    ),
                    False,
                )
                time.sleep(int(os.environ.get("MQTT_ALIVE_INTERVAL")))
            except KeyboardInterrupt:
                LOGGER.info("Received Ctrl+C, stopping heartbeat...")
                break
            # pylint: disable=broad-except
            except Exception as e:
                LOGGER.error("Failed to publish heartbeat message: %s", str(e))

    def _stop_heartbeat(self) -> None:
        """Stop publishing heartbeat messages to the broker."""
        # pylint: disable=no-member
        self._hearbeat_thread.stop()
