"""Video Stream Agent."""

# pylint: disable=import-error, broad-except
import os
import threading
import logger
import base
import rtsp_server

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class VideoAgent(base.BaseAgent):
    """Video Stream Agent class."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client and settings."""
        super().__init__(mqtt_client)
        self._location_topic = f"{self._mqtt_topic}/{self._agent_location}"
        self._streaming = False
        self._port = int(os.environ.get("VIDEO_STREAM_PORT"))
        self._video_width = int(os.environ.get("VIDEO_WIDTH"))
        self._video_height = int(os.environ.get("VIDEO_HEIGHT"))
        self._address = ("", self._port)
        self._record_time = int(os.environ.get("VIDEO_RECORDING_DURATION"))
        self._rtsp_thread = None
        self._rtsp_server = None

    def run(self):
        """Subscribe to the mqtt topic and start listening for video messages."""
        self._mqtt.subscribe(f"video/{self._agent_location}")
        self._mqtt.message_callback_add(
            f"video/{self._agent_location}", self._on_video_message
        )
        LOGGER.info("Video Agent subscribed to MQTT topic: video/%s", self._agent_location)

        self._mqtt.subscribe(self._location_topic)
        self._mqtt.message_callback_add(
            self._location_topic,
            self._on_doorbell_message,
        )
        LOGGER.info("Video Agent subscribed to MQTT topic: %s", self._location_topic)

        if os.environ.get("VIDEO_AUTOSTART") == "True" and not self._streaming:
            LOGGER.info("Autostarting video stream")
            self._start_video_stream()

    # pylint: disable=unused-argument
    def _on_video_message(self, client, userdata, msg):
        """Handle incoming video commands via MQTT."""
        LOGGER.info("MQTT message received: %s", msg.payload.decode("utf-8"))
        payload = msg.payload.decode("utf-8")
        self._toggle_video(payload)

    # pylint: disable=unused-argument
    def _on_doorbell_message(self, client, userdata, msg):
        """Process the doorbell message."""
        LOGGER.info("Doorbell message received: %s", msg.payload.decode("utf-8"))

    def _toggle_video(self, command):
        """Toggle the video stream on or off based on the MQTT command."""
        if command == "on":
            if self._streaming:
                LOGGER.warning("Video stream already running.")
                return
            self._start_video_stream()
        elif command == "off":
            if not self._streaming:
                LOGGER.warning("Video stream already stopped.")
                return
            self._stop_video_stream()
        else:
            LOGGER.error("Unknown video command received: %s", command)

    def _start_video_stream(self):
        """Start the video stream (RTSP in this case)."""
        LOGGER.info("Attempting to start video stream")
        try:
            self._streaming = True
            self._start_rtsp_server()
        except Exception as e:
            LOGGER.error(f"Error starting video stream: {e}")
            self._stop_video_stream()

    def _stop_video_stream(self):
        """Stop the video stream (RTSP in this case)."""
        LOGGER.info("Attempting to stop video stream")
        try:
            self._stop_rtsp_server()
        except Exception as e:
            LOGGER.error(f"Failed to stop video stream: {e}")
        finally:
            self._streaming = False

    def stop(self):
        """Stop the agent gracefully."""
        self._stop_video_stream()
        self._mqtt.stop()
        self._mqtt.unsubscribe(f"video/{self._agent_location}")
        self._mqtt.message_callback_remove(f"video/{self._agent_location}")
        LOGGER.info("Video agent stopped.")

    def _start_rtsp_server(self):
        """Start the RTSP server in a separate thread."""
        if self._rtsp_thread and self._rtsp_thread.is_alive():
            LOGGER.warning("RTSP server thread already running.")
            return

        LOGGER.info("Starting RTSP server thread...")
        self._rtsp_server = rtsp_server.StreamServer()
        self._rtsp_thread = threading.Thread(target=self._rtsp_server.run, daemon=True)
        self._rtsp_thread.start()
        LOGGER.info("RTSP server thread started.")

    def _stop_rtsp_server(self):
        """Stop the RTSP server if running."""
        if self._rtsp_server:
            LOGGER.info("Stopping RTSP server...")
            self._rtsp_server.stop()
            self._rtsp_server = None
            LOGGER.info("RTSP server stopped.")
