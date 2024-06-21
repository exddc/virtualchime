"""Video Stream Agent."""

# pylint: disable=import-error, broad-except
import os
import threading
import socket
import logger
import base
from picamera2 import Picamera2
from picamera2.encoders import H264Encoder
from picamera2.outputs import CircularOutput

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class VideoAgent(base.BaseAgent):
    """Video Stream Agent class."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client and settings."""
        super().__init__(mqtt_client)
        self._streaming = False
        self._port = int(os.environ.get("VIDEO_STREAM_PORT"))
        self._record_time = int(os.environ.get("VIDEO_RECORDING_DURATION"))
        self._picamera = Picamera2()
        self._encoder = H264Encoder(bitrate=int(os.environ.get("VIDEO_BITRATE")))
        self._output = CircularOutput(buffersize=10 * 1024 * 1024)

        self._stream_thread = None
        self._sock = None
        self._conn = None

    def run(self):
        """Subscribe to the mqtt topic and start listening for video messages."""
        self._mqtt.subscribe(f"video/{self._agent_location}")
        LOGGER.debug("Subscribed to topic: video/%s", self._agent_location)
        self._mqtt.message_callback_add(
            f"video/{self._agent_location}", self._on_video_message
        )
        LOGGER.debug("Added callback for topic: video/%s", self._agent_location)
        LOGGER.info("Video Agent %s listening", self._agent_location)
        if os.environ.get("VIDEO_AUTOSTART") == "True":
            LOGGER.info("Autostarting video stream")
            self._start_video_stream()

    # pylint: disable=unused-argument
    def _on_video_message(self, client, userdata, msg):
        LOGGER.info("Mqtt message received: %s", msg.payload)
        payload = msg.payload.decode("utf-8")
        self._toggle_video(payload)

    def _toggle_video(self, command):
        """Toggle the video stream on or off."""
        if command == "on":
            self._start_video_stream()
        elif command == "off":
            self._stop_video_stream()
        else:
            LOGGER.error("Unknown video command: %s", command)

    def _start_video_stream(self):
        """Start the video stream."""
        if self._streaming:
            LOGGER.warning("Video stream already running.")
            return

        LOGGER.info("Starting video stream")
        try:
            self._picamera.configure(
                self._picamera.create_video_configuration(
                    main={
                        "size": (
                            int(os.environ.get("VIDEO_WIDTH")),
                            int(os.environ.get("VIDEO_HEIGHT")),
                        ),
                        "format": "RGB888",
                    },
                    controls={"FrameRate": int(os.environ.get("VIDEO_FPS"))},
                )
            )
            self._encoder.output = self._output
            self._picamera.start_recording(self._encoder)

            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._sock.bind(("0.0.0.0", self._port))
            self._sock.listen(1)
            LOGGER.info(f"Waiting for a connection on port {self._port}...")

            self._conn, addr = self._sock.accept()
            LOGGER.info(f"Connection accepted from {addr}")

            self._streaming = True
            self._stream_thread = threading.Thread(target=self._stream_video)
            self._stream_thread.start()
        except Exception as e:
            LOGGER.error("Failed to start video stream: %s", e)
            self._streaming = False

    def _stop_video_stream(self):
        """Stop the video stream."""
        if self._streaming:
            LOGGER.info("Stopping video stream")
            try:
                self._picamera.stop_recording()
                self._streaming = False
                if self._stream_thread:
                    self._stream_thread.join()
                if self._conn:
                    self._conn.close()
                if self._sock:
                    self._sock.close()
            except Exception as e:
                LOGGER.error("Failed to stop video stream: %s", e)

    def _stream_video(self):
        """Stream video frames to the TCP client."""
        try:
            while self._streaming:
                data = self._output.read()
                if data:
                    self._conn.sendall(data)
        except Exception as e:
            LOGGER.error("Error streaming video: %s", e)
        finally:
            self._conn.close()

    def stop(self):
        """Stop the agent."""
        self._stop_video_stream()
        self._picamera.close()
        self._mqtt.stop()
        self._mqtt.unsubscribe(f"video/{self._agent_location}")
        self._mqtt.message_callback_remove(f"video/{self._agent_location}")
        LOGGER.info("Video agent stopped.")
