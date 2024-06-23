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
    """Class to handle streaming output."""
    def __init__(self):
        self.frame = None
        self.condition = Condition()

    def write(self, buf):
        with self.condition:
            self.frame = buf
            self.condition.notify_all()

OUTPUT = StreamingOutput()

class StreamingHandler(server.BaseHTTPRequestHandler):
    """Class to handle streaming requests."""
    def do_GET(self):
        """Handle GET requests."""
        if self.path == '/stream.mjpg':
            self.send_response(200)
            self.send_header('Age', 0)
            self.send_header('Cache-Control', 'no-cache, private')
            self.send_header('Pragma', 'no-cache')
            self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=FRAME')
            self.end_headers()
            try:
                while True:
                    with OUTPUT.condition:
                        OUTPUT.condition.wait()
                        frame = OUTPUT.frame
                    self.wfile.write(b'--FRAME\r\n')
                    self.send_header('Content-Type', 'image/jpeg')
                    self.send_header('Content-Length', len(frame))
                    self.end_headers()
                    self.wfile.write(frame)
                    self.wfile.write(b'\r\n')
            except Exception as e:
                LOGGER.warning(
                    'Removed streaming client %s: %s',
                    self.client_address, str(e))
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
        self._port =  int(os.environ.get("VIDEO_STREAM_PORT"))
        self._video_width = int(os.environ.get("VIDEO_WIDTH"))
        self._video_height = int(os.environ.get("VIDEO_HEIGHT"))
        self._address = ("", self._port)
        self._record_time = int(os.environ.get("VIDEO_RECORDING_DURATION"))
        self._picamera = Picamera2()
        self._picamera.configure(self._picam.create_video_configuration(main={"size": (self._video_width, self._video_height)}))

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
        self._picamera.start_recording(MJPEGEncoder(), FileOutput(OUTPUT))
        try:
            server = StreamingServer(self._address, StreamingHandler)
            server.serve_forever()
        # pylint: disable=broad-except
        except Exception:
            LOGGER.error("Error starting video stream")
            self._stop_video_stream()
            

    def _stop_video_stream(self):
        """Stop the video stream."""
        LOGGER.info("Stopping video stream")
        try:
            self._picamera.stop_recording()   
        # pylint: disable=broad-except
        except Exception as e:
                LOGGER.error("Failed to stop video stream: %s", e)
        finally:
            self._streaming = False

    def stop(self):
        """Stop the agent."""
        self._stop_video_stream()
        self._mqtt.stop()
        self._mqtt.unsubscribe(f"video/{self._agent_location}")
        self._mqtt.message_callback_remove(f"video/{self._agent_location}")
        LOGGER.info("Video agent stopped.")
