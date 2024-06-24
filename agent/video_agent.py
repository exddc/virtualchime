"""Video Stream Agent."""

# pylint: disable=import-error, broad-except
import os
import io
from http import server
from threading import Condition
import socketserver
from picamera2 import Picamera2
from picamera2.encoders import MJPEGEncoder
from picamera2.outputs import FileOutput
import logger
import base

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class StreamingOutput(io.BufferedIOBase):
    """Class to handle streaming output with file size limit."""

    def __init__(self):
        self.frame = None
        self.condition = Condition()
        self.file_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recordings")
        self.recording_duration = int(os.environ.get("VIDEO_RECORDING_DURATION"))
        self.max_file_size = self.recording_duration * 1024 * 1024
        self.current_file_size = 0
        self.output_file = None

        if not os.path.exists(self.file_path):
            os.makedirs(self.file_path)
            LOGGER.info("Created recordings folder: %s", self.file_path)

        try: 
            self.output_file = open(self.file_path + "/stream.mjpg", "wb")
        except FileNotFoundError as e:
            LOGGER.error("Error opening file: %s", e)

    def write(self, buf):
        with self.condition:
            self.frame = buf
            self.condition.notify_all()

            if self.output_file:
                self.output_file.write(buf)
                self.output_file.flush()
                self.current_file_size += len(buf)

                if self.current_file_size >= self.max_file_size:
                    self.output_file.seek(0)
                    self.current_file_size = 0

    def close(self):
        if self.output_file:
            self.output_file.close()


OUTPUT = StreamingOutput()


class StreamingHandler(server.BaseHTTPRequestHandler):
    """Class to handle streaming requests."""

    # pylint: disable=invalid-name
    def do_GET(self):
        """Handle GET requests."""
        if self.path == "/":
            LOGGER.info("Request for /. Redirecting to /stream.mjpg")
            self.send_response(301)
            self.send_header("Location", "/stream.mjpg")
            self.end_headers()
        elif self.path == "/stream.mjpg":
            LOGGER.info("Request for /stream.mjpg")
            self.send_response(200)
            self.send_header("Age", 0)
            self.send_header("Cache-Control", "no-cache, private")
            self.send_header("Pragma", "no-cache")
            self.send_header(
                "Content-Type", "multipart/x-mixed-replace; boundary=FRAME"
            )
            self.end_headers()
            try:
                while True:
                    with OUTPUT.condition:
                        OUTPUT.condition.wait()
                        frame = OUTPUT.frame
                    self.wfile.write(b"--FRAME\r\n")
                    self.send_header("Content-Type", "image/jpeg")
                    self.send_header("Content-Length", len(frame))
                    self.end_headers()
                    self.wfile.write(frame)
                    self.wfile.write(b"\r\n")
            except Exception as e:
                LOGGER.warning(
                    "Removed streaming client %s: %s", self.client_address, str(e)
                )
        else:
            self.send_error(404)
            self.end_headers()


class StreamingServer(socketserver.ThreadingMixIn, server.HTTPServer):
    """Class to handle streaming server."""

    allow_reuse_address = True
    daemon_threads = True


class VideoAgent(base.BaseAgent):
    """Video Stream Agent class."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client and settings."""
        super().__init__(mqtt_client)
        self._streaming = False
        self._port = int(os.environ.get("VIDEO_STREAM_PORT"))
        self._video_width = int(os.environ.get("VIDEO_WIDTH"))
        self._video_height = int(os.environ.get("VIDEO_HEIGHT"))
        self._address = ("", self._port)
        self._record_time = int(os.environ.get("VIDEO_RECORDING_DURATION"))
        self._picamera = Picamera2()
        self._picamera.configure(
            self._picamera.create_video_configuration(
                main={"size": (self._video_width, self._video_height)}
            )
        )

    def run(self):
        """Subscribe to the mqtt topic and start listening for video messages."""
        self._mqtt.subscribe(f"video/{self._agent_location}")
        LOGGER.debug("Subscribed to topic: video/%s", self._agent_location)
        self._mqtt.message_callback_add(
            f"video/{self._agent_location}", self._on_video_message
        )
        LOGGER.debug("Added callback for topic: video/%s", self._agent_location)
        LOGGER.info("Video Agent %s listening", self._agent_location)
        if os.environ.get("VIDEO_AUTOSTART") == "True" and not self._streaming:
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
            LOGGER.error("Unknown video command: %s", command)

    def _start_video_stream(self):
        """Start the video stream."""
        LOGGER.info("Starting video stream")
        if not self._check_camera_status():
            LOGGER.error("Cannot start video stream.")
            return

        self._picamera.start_recording(MJPEGEncoder(), FileOutput(OUTPUT))
        try:
            self._streaming = True
            __server = StreamingServer(self._address, StreamingHandler)
            __server.serve_forever()
        # pylint: disable=broad-except
        except Exception:
            LOGGER.error("Error starting video stream")
            self._stop_video_stream()

    def _stop_video_stream(self):
        """Stop the video stream."""
        LOGGER.info("Stopping video stream")
        try:
            self._picamera.stop_recording()
            OUTPUT.close()
        # pylint: disable=broad-except
        except Exception as e:
            LOGGER.error("Failed to stop video stream: %s", e)
        finally:
            self._streaming = False

    def _check_camera_status(self):
        """Check the camera status."""
        try:
            self._picamera.start()
            self._picamera.capture_file("test_image.jpg")
            self._picamera.stop()

            if os.path.exists("test_image.jpg"):
                os.remove("test_image.jpg")
                LOGGER.debug("Camera connected and working.")
                return True

            LOGGER.error("Camera connected, but failed to capture image.")
            return False
        except Picamera2.PiCameraError as e:
            LOGGER.error("Camera not connected: %s", e)
            return False

    def stop(self):
        """Stop the agent."""
        self._stop_video_stream()
        self._mqtt.stop()
        self._mqtt.unsubscribe(f"video/{self._agent_location}")
        self._mqtt.message_callback_remove(f"video/{self._agent_location}")
        LOGGER.info("Video agent stopped.")
