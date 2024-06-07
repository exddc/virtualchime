"""Video Stream Agent."""

# pylint: disable=import-error
import threading
import socketserver
import http.server
import picamera2
import logger
import base

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class VideoAgent(base.BaseAgent):
    """Video Stream Agent class."""

    def __init__(self, mqtt_client):
        """Initialize the agent with the MQTT client."""
        super().__init__(mqtt_client)
        self._stream_server = None
        self._camera = None
        self._streaming = False

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
        if not self._streaming:
            LOGGER.info("Starting video stream")
            self._camera = picamera2.Picamera2()
            self._camera.configure(self._camera.create_preview_configuration())
            self._camera.start()
            self._stream_server = threading.Thread(
                target=self._video_stream_server, daemon=True
            )
            self._stream_server.start()
            self._streaming = True

    def _stop_video_stream(self):
        """Stop the video stream."""
        if self._streaming:
            LOGGER.info("Stopping video stream")
            self._camera.stop()
            self._camera.close()
            self._streaming = False
            self._stream_server.join()

    def _video_stream_server(self):
        """Creates a video stream server."""

        class StreamingHandler(http.server.BaseHTTPRequestHandler):
            """Video stream handler."""

            # pylint: disable=invalid-name
            def do_GET(self):
                """Handle the GET request."""
                LOGGER.info("Stream request received")
                self.send_response(200)
                self.send_header(
                    "Content-type", "multipart/x-mixed-replace; boundary=FRAME"
                )
                self.end_headers()
                try:
                    for frame in self._stream_video():
                        self.wfile.write(b"--FRAME\r\n")
                        self.send_header("Content-Type", "image/jpeg")
                        self.send_header("Content-Length", len(frame))
                        self.end_headers()
                        self.wfile.write(frame)
                        self.wfile.write(b"\r\n")
                # pylint: disable=broad-except
                except Exception as e:
                    LOGGER.error("Streaming error: %s", e)

            def _stream_video(self):
                while True:
                    frame = self.server.camera.capture_buffer("jpeg")
                    yield frame

        class StreamingServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
            """Video stream server."""

            allow_reuse_address = True
            daemon_threads = True

        address = ("", 8000)
        server = StreamingServer(address, StreamingHandler)
        # pylint: disable=attribute-defined-outside-init
        server.camera = self._camera
        LOGGER.info("Starting HTTP server on port %s", address[1])
        server.serve_forever()
