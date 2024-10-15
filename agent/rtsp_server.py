"""RTSP and HLS Stream Server."""

# pylint: disable=import-error, broad-except
import os
import logger
import base
from gi.repository import Gst, GLib
from threading import Thread

# Initialize logger
LOGGER = logger.get_module_logger(__name__)

class StreamServer(base.BaseAgent):
    """Stream Server Class that outputs both RTSP and HLS."""

    def __init__(self):
        """Initialize the stream server with settings from environment."""
        super().__init__(None)  # Since stream server doesn't need MQTT
        Gst.init(None)

        self.hls_output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hls")
        os.makedirs(self.hls_output_path, exist_ok=True)

        LOGGER.info(f"HLS output will be saved to {self.hls_output_path}")

        # GStreamer pipeline to capture from camera and produce both RTSP and HLS streams
        self.pipeline = (
            f'libcamerasrc ! video/x-raw,width=1280,height=720,framerate=30/1 ! '
            'tee name=t t. ! queue ! videoconvert ! x264enc bitrate=2000 speed-preset=ultrafast tune=zerolatency ! '
            'h264parse ! rtph264pay pt=96 name=pay0 t. ! queue ! videoconvert ! x264enc tune=zerolatency ! '
            f'splitmuxsink location={self.hls_output_path}/segment_%05d.ts max-size-time=1000000000'
        )
        LOGGER.info(f"Initialized GStreamer pipeline for RTSP and HLS: {self.pipeline}")

    def run(self):
        """Run the stream server and start both RTSP and HLS streaming."""
        LOGGER.info("Starting RTSP and HLS streams...")
        try:
            # Start the GStreamer process
            self.gst_process = Gst.parse_launch(self.pipeline)
            self.gst_process.set_state(Gst.State.PLAYING)
            
            # Start a thread to generate the HLS playlist
            self.hls_thread = Thread(target=self._generate_hls_playlist, daemon=True)
            self.hls_thread.start()

            # Run the GLib main loop
            self.loop = GLib.MainLoop()
            self.loop.run()
        except Exception as e:
            LOGGER.error(f"Error running the RTSP and HLS streams: {e}")
        finally:
            self.stop()

    def stop(self):
        """Stop both RTSP and HLS streams."""
        if hasattr(self, 'gst_process') and self.gst_process:
            self.gst_process.set_state(Gst.State.NULL)
            LOGGER.info("RTSP and HLS streams stopped.")
        if hasattr(self, 'loop') and self.loop:
            self.loop.quit()

    def _generate_hls_playlist(self):
        """Monitor the HLS output folder and generate the .m3u8 playlist."""
        LOGGER.info("Starting to generate HLS playlist...")

        playlist_path = os.path.join(self.hls_output_path, "stream.m3u8")
        segment_list = []

        try:
            while True:
                # Get the list of .ts files
                ts_files = sorted([f for f in os.listdir(self.hls_output_path) if f.endswith('.ts')])

                # Only update the playlist if new segments are detected
                if ts_files != segment_list:
                    segment_list = ts_files

                    with open(playlist_path, 'w') as playlist:
                        playlist.write("#EXTM3U\n")
                        playlist.write("#EXT-X-VERSION:3\n")
                        playlist.write("#EXT-X-TARGETDURATION:1\n")
                        playlist.write("#EXT-X-MEDIA-SEQUENCE:0\n")

                        for ts_file in segment_list:
                            playlist.write("#EXTINF:1.000,\n")
                            playlist.write(f"{ts_file}\n")

                    LOGGER.info(f"Updated HLS playlist with {len(segment_list)} segments.")
        except Exception as e:
            LOGGER.error(f"Error generating HLS playlist: {e}")