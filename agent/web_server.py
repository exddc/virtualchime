"""Webserver for the doorbell."""

# pylint: disable=import-error
import os
import dotenv
from flask import Flask, render_template
from flask_assets import Environment, Bundle
import logger

# Load environment variables
dotenv.load_dotenv()

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class WebServer:
    """Provides a webpage for the doorbell."""

    def __init__(self) -> None:
        """Initialize the webserver."""
        LOGGER.info("Initializing webserver.")
        self._port = int(os.environ.get("WEBSERVER_PORT"))
        self.app = Flask(__name__)

        # Initialize assets
        self._assets = Environment(self.app)
        self.__css = Bundle("src/style.css", output="dist/style.css")
        self.__js = Bundle("src/*.js", output="dist/main.js")

        self._assets.register("css", self.__css)
        self._assets.register("js", self.__js)

        self.__css.build()
        self.__js.build()

        # Register routes
        self._setup_routes()

    def _setup_routes(self) -> None:
        """Setup routes for the webserver."""

        @self.app.route("/")
        def index():
            return render_template("index.html")

        @self.app.route("/get_stream")
        def get_stream_url():
            stream_url = (
                f"http://localhost:{os.environ.get('VIDEO_STREAM_PORT')}/video_stream"
            )
            return render_template("video_stream.html", stream_url=stream_url)

    def run(self) -> None:
        """Run the webserver."""
        self.app.run(host="0.0.0.0", port=self._port)
        LOGGER.info("Webserver started on port %i.", self._port)


if __name__ == "__main__":
    web_server = WebServer()
    web_server.run()
