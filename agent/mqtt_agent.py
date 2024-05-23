# A MQTT client class that gets called by the agent.py. It runs in a separate thread. It subscribes to topics and publishes to the broker.
# It also publishes status messages to the broker. It is used by all other agents.

import paho.mqtt.client
import json
import logger
import os
import dotenv
import time
import datetime
import threading

# Load environment variables
dotenv.load_dotenv()

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class MqttAgent(paho.mqtt.client.Client):
    # Recive, process and send mqtt messages

    def __init__(self, client_id: str, topics: list) -> None:
        # Create an Mqtt Client
        # Define the topics that needs to be subscribed to

        self._topics = [] if topics is None else topics
        # super().__init__(paho.mqtt.client.CallbackAPIVersion.VERSION2, client_id)
        super().__init__(client_id)

    def __enter__(self) -> "MqttAgent":
        # Connect and start MQtt loop when entering with block
        self.start()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        # Disconnect and stop Mqtt loop when exiting with block
        self.stop()

    def start(self) -> None:
        # Connect to the broker and start the loop
        self.username_pw_set(
            os.environ.get("MQTT_USERNAME"), os.environ.get("MQTT_PASSWORD")
        )
        # self.on_connect = self._subscribe_on_connect
        self.on_connect = LOGGER.info("Connected to MQTT Broker")
        LOGGER.debug(
            f"Connection Info: {os.environ.get('MQTT_BROKER_IP')}:{int(os.environ.get('MQTT_BROKER_PORT'))}"
        )
        try:
            self.connect(
                os.environ.get("MQTT_BROKER_IP"),
                int(os.environ.get("MQTT_BROKER_PORT")),
            )
        except Exception as e:
            LOGGER.error("Failed to connect to MQTT Broker: %s", str(e))
            while True:
                try:
                    self.connect(
                        os.environ.get("MQTT_BROKER_IP"),
                        int(os.environ.get("MQTT_BROKER_PORT")),
                    )
                    break
                except Exception as e:
                    LOGGER.error("Failed to connect to MQTT Broker: %s", str(e))
                    time.sleep(5)

        self._start_heartbeat()
        self.loop_start()

    def stop(self) -> None:
        # Disconnect from the broker and stop the loop
        self.publish(
            f"status/{os.environ.get('AGENT_LOCATION')}",
            json.dumps(
                {"state": "offline", "date": datetime.datetime.now().isoformat()}
            ),
        )
        self._stop_heartbeat
        self.loop_stop()
        self.disconnect()

    def _subscribe_on_connect(self, client, userdata, flags, rc) -> None:
        # Subscribe to topics on connect
        if rc == 0:
            LOGGER.info("Connected to MQTT Broker!")
        else:
            LOGGER.error("Failed to connect, return code %d", rc)

        self.subscribe_to_topics(self._topics)
        self.on_message = self._on_message_callback

    def subscribe_to_topics(self, topics: list) -> None:
        # Subscribe to topics
        for topic in topics:
            self.subscribe(topic)
            LOGGER.info(f"Subscribed to topic {topic}")

    def _on_message_callback(self, client, userdata, msg) -> None:
        # Process messages
        LOGGER.info(f"Received message from topic {msg.topic}: {msg.payload.decode()}")
        self._process_message(msg.topic, msg.payload.decode())

    def _on_message(self, client, userdata, msg) -> None:
        # Process messages
        LOGGER.info(f"Received message from topic {msg.topic}: {msg.payload.decode()}")
        self._process_message(msg.topic, msg.payload.decode())

    def _process_message(self, topic: str, payload: str) -> None:
        # Process messages
        LOGGER.info(f"Processed message from topic {topic}: {payload}")

    def publish(self, topic: str, payload: str, log_info: bool = True) -> None:
        # Publish a message to the broker
        super().publish(topic, payload)
        if log_info:
            LOGGER.info(f"Published message {payload} to topic {topic}")

    def __del__(self) -> None:
        # Disconnect from the broker and stop the loop when object is deleted
        self.stop()
        super().__del__()

    def _start_heartbeat(self) -> None:
        # Publish a heartbeat message to the broker
        LOGGER.info("Starting heartbeat")
        self._hearbeat_thread = threading.Thread(target=self._publish_heartbeat)
        self._hearbeat_thread.start()

    def _publish_heartbeat(self) -> None:
        # Publish a heartbeat message to the broker
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
            except Exception as e:
                LOGGER.error("Failed to publish heartbeat message: %s", str(e))

    def _stop_heartbeat(self) -> None:
        # Stop publishing heartbeat messages to the broker
        self._hearbeat_thread.stop()
