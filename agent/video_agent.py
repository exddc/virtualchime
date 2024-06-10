"""Video Stream Agent."""

# pylint: disable=import-error, broad-except
import os
import threading
import time
import io
from http.server import BaseHTTPRequestHandler, HTTPServer
import logger
import base
from picamera2 import Picamera2
from picamera2.encoders import H264Encoder, Quality
from picamera2.outputs import FileOutput
from PIL import Image

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class VideoStreamHandler(BaseHTTPRequestHandler):
    """HTTP request handler for video streaming."""

    # pylint: disable=invalid-name
    def do_GET(self):
        """Handle GET requests."""
        if self.path == "/video_stream":
            self.send_response(200)
            self.send_header(
                "Content-type", "multipart/x-mixed-replace; boundary=frame"
            )
            self.end_headers()
            while True:
                try:
                    frame = self.server.video_stream.get_frame()
                    if frame is not None:
                        img = Image.fromarray(frame).convert("RGB")
                        with io.BytesIO() as output:
                            img.save(output, format="JPEG")
                            frame_bytes = output.getvalue()
                            self.wfile.write(b"--frame\r\n")
                            self.send_header("Content-Type", "image/jpeg")
                            self.send_header("Content-Length", len(frame_bytes))
                            self.end_headers()
                            self.wfile.write(frame_bytes)
                            self.wfile.write(b"\r\n")
                    time.sleep(0.1)
                except Exception as e:
                    LOGGER.error("Error streaming video: %s", e)
                    break


class VideoStreamServer(threading.Thread):
    """HTTP server for video streaming."""

    def __init__(self, port, video_stream):
        super().__init__()
        self.port = port
        self.video_stream = video_stream
        self.server = HTTPServer(("0.0.0.0", self.port), VideoStreamHandler)
        self.server.video_stream = video_stream

    def run(self):
        LOGGER.info("Starting HTTP server on port %d", self.port)
        self.server.serve_forever()

    def stop(self):
        """Stop the HTTP server."""
        LOGGER.info("Stopping HTTP server")
        self.server.shutdown()
        self.server.server_close()


class VideoAgent(base.BaseAgent):
    """Video Stream Agent class."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client and settings."""
        super().__init__(mqtt_client)
        self._streaming = False
        self._port = int(os.environ.get("VIDEO_STREAM_PORT"))
        self._record_time = int(os.environ.get("VIDEO_RECORDING_DURATION"))
        self._picamera = Picamera2()
        self._video_server = None
        self._encoder = H264Encoder(bitrate=int(os.environ.get("VIDEO_BITRATE")))

        recording_folder = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "recordings",
        )

        if not os.path.exists(recording_folder):
            os.makedirs(recording_folder)

        self._output = FileOutput(recording_folder + "/video_recording.h264")

    def run(self):
        """Subscribe to the mqtt topic and start listening for video messages."""
        self._mqtt.subscribe(f"video/{self._agent_location}")
        LOGGER.debug("Subscribed to topic: video/%s", self._agent_location)
        self._mqtt.message_callback_add(
            f"video/{self._agent_location}", self._on_video_message
        )
        LOGGER.debug("Added callback for topic: video/%s", self._agent_location)
        LOGGER.info("%s listening", self._agent_location)
        if os.environ.get("VIDEO_AUTOSTART") == "True":
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
            self._video_server = VideoStreamServer(self._port, self)
            self._video_server.start()
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
                if self._video_server:
                    self._video_server.stop()
                    self._video_server.join()
            except Exception as e:
                LOGGER.error("Failed to stop video stream: %s", e)

    def get_frame(self):
        """Get the current frame from the video stream."""
        try:
            frame = self._picamera.capture_array()
            return frame
        except Exception as e:
            LOGGER.error("Failed to capture frame: %s", e)
            return None
