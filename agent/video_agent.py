"""Video Stream Agent."""

# pylint: disable=import-error, broad-except
import os
import threading
import time
import socket
import logger
import base
from picamera2 import Picamera2
from picamera2.encoders import H264Encoder, Quality
from picamera2.outputs import FileOutput

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

        recording_folder = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "recordings",
        )

        if not os.path.exists(recording_folder):
            os.makedirs(recording_folder)
            LOGGER.info("Created recording folder: %s", recording_folder)

        self._output = FileOutput(recording_folder + "/video_recording.h264")

        self._stream_thread = None
        self._sock = None

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
        self._toggle_video(msg.payload.decode("utf-8"))

    def _toggle_video(self, payload):
        """Toggle the video stream on or off."""
        if payload == "on":
            self._start_video_stream()
        elif payload == "off":
            self._stop_video_stream()
        else:
            LOGGER.error("Unknown video command: %s", payload)

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
            self._picamera.start_recording(
                self._encoder, self._output, quality=Quality.HIGH
            )
            self._streaming = True

            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._sock.bind(("0.0.0.0", self._port))

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
                if self._sock:
                    self._sock.close()
            except Exception as e:
                LOGGER.error("Failed to stop video stream: %s", e)

    def _stream_video(self):
        """Stream video frames to the UDP socket."""
        try:
            conn, addr = self._sock.accept()
            with conn.makefile("wb") as stream:
                while self._streaming:
                    self._picamera.start_recording(self._encoder, FileOutput(stream))
                    time.sleep(0.01)
        except Exception as e:
            LOGGER.error("Error streaming video: %s", e)

    def stop(self):
        """Stop the agent."""
        self._stop_video_stream()
        self._picamera.close()
        self._mqtt.stop()
        self._mqtt.unsubscribe(f"video/{self._agent_location}")
        self._mqtt.message_callback_remove(f"video/{self._agent_location}")
        LOGGER.info("Video agent stopped.")
