"""Video Stream Agent."""

# pylint: disable=import-error
import threading
import socketserver
import http
import cv2
import logger
import base

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class VideoAgent(base.BaseAgent):
    """Video Stream Agent class."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client."""
        super().__init__(mqtt_client)
        self._camera = cv2.VideoCapture(0)
        self._stream_server = None

    def run(self):
        """Subscribe to the mqtt topic and start listening for video messages."""
        self._mqtt.subscribe(f"video/{self._agent_location}")
        LOGGER.debug("Subscribed to topic: video/%s", self._agent_location)
        self._mqtt.message_callback_add(
            f"video/{self._agent_location}", self._on_video_message
        )
        LOGGER.debug("Added callback for topic: video/%s", self._agent_location)
        LOGGER.info("%s listening", self._agent_location)

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
        LOGGER.info("Starting video stream")
        self._stream_server = threading.Thread(
            target=self._video_stream_server, daemon=True
        )
        self._stream_server.start()

    def _stop_video_stream(self):
        """Stop the video stream."""
        LOGGER.info("Stopping video stream")
        self._camera.release()
        self._stream_server.join()

    def _video_stream_server(self):
        """Creates a video stream server."""

        class VideoStreamHandler(http.server.SimpleHTTPRequestHandler):
            """Video Stream Handler class."""

            def do_GET(self):
                """Handle GET requests."""
                if self.path == "/":
                    self.send_response(200)
                    self.send_header(
                        "Content-type", "multipart/x-mixed-replace; boundary=frame"
                    )
                    self.end_headers()
                    while True:
                        ret, frame = self.server.video_agent.get_camera.read()
                        if not ret:
                            break
                        _, img = cv2.imencode(".jpg", frame)
                        self.wfile.write(b"--frame\r\n")
                        self.send_header("Content-type", "image/jpeg")
                        self.send_header("Content-length", len(img))
                        self.end_headers()
                        self.wfile.write(img)
                        self.wfile.write(b"\r\n")
                else:
                    self.send_response(404)
                    self.end_headers()

        with socketserver.TCPServer(("", 8000), VideoStreamHandler) as httpd:
            httpd.video_agent = self
            httpd.serve_forever()

    def get_camera(self):
        """Get the camera object."""
        return self._camera
