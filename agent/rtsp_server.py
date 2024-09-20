"""RTSP Stream Server."""

# pylint: disable=import-error, broad-except
import os
import logger
import base
from gi.repository import Gst, GstRtspServer, GLib

# Initialize logger
LOGGER = logger.get_module_logger(__name__)

class RTSPServer(base.BaseAgent):
    """RTSP Stream Server Class."""

    def __init__(self):
        """Initialize the RTSP server with settings from environment."""
        super().__init__(None)  # Since RTSP doesn't need MQTT
        Gst.init(None)

        self.port = int(os.environ.get("RTSP_PORT", 8554))  # Default port 8554
        self.server = GstRtspServer.RTSPServer()
        self.server.set_service(str(self.port))
        
        self.factory = GstRtspServer.RTSPMediaFactory()
        self.factory.set_launch((
            'libcamerasrc ! video/x-raw,width=1280,height=720,framerate=30/1 ! '
            'videoconvert ! x264enc bitrate=2000 speed-preset=ultrafast tune=zerolatency ! '
            'rtph264pay config-interval=1 name=pay0 pt=96'
        ))
        self.factory.set_shared(True)

        # Attach the factory to the server at the /stream endpoint
        self.mounts = self.server.get_mount_points()
        self.mounts.add_factory("/stream", self.factory)

        LOGGER.info(f"RTSP server initialized on port {self.port}")
        self.loop = None  # Store reference to the GLib.MainLoop

    def run(self):
        """Run the RTSP server and start streaming."""
        LOGGER.info(f"RTSP server starting on rtsp://0.0.0.0:{self.port}/stream")
        self.server.attach(None)
        self.loop = GLib.MainLoop()

        try:
            self.loop.run()  # Start the main loop
        except Exception as e:
            LOGGER.error(f"RTSP server encountered an error: {e}")
        finally:
            LOGGER.info("RTSP server stopped.")

    def stop(self):
        """Stop the RTSP server."""
        if self.loop:
            LOGGER.info("Stopping the RTSP server.")
            self.loop.quit()  # Stop the GLib.MainLoop
            self.loop = None
        else:
            LOGGER.warning("RTSP server is not running.")
