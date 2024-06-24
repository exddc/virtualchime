"""Web server for the doorbell agent."""

# pylint: disable=import-error
import os
import dotenv
import waitress
from flask import Flask, render_template, request, redirect, url_for
from flask_assets import Environment, Bundle
import base
import logger


# Load environment variables
dotenv.load_dotenv()

# Initialize logger
LOGGER = logger.get_module_logger(__name__)


class WebServer(base.BaseAgent):
    """Provides a webpage for the doorbell."""

    def __init__(self, mqtt_client) -> None:
        """Initialize the webserver."""
        LOGGER.info("Initializing webserver.")
        super().__init__(mqtt_client)
        self._location_topic = f"{self._mqtt_topic}/{self._agent_location}"

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

        self._web_server = None

    def _setup_routes(self) -> None:
        """Setup routes for the webserver."""

        @self.app.route("/")
        def dashboard():
             LOGGER.info("Dashboard requested by %s", request.remote_addr)
             # pylint: disable=line-too-long
             stream_url = f"http://{os.popen('hostname -I').read().split()[0]}:{os.environ.get('VIDEO_STREAM_PORT')}/stream.mjpg"
             return render_template("dashboard.html", stream_url=stream_url)

        @self.app.route("/settings", methods=["GET", "POST"])
        def settings():
            LOGGER.info("Settings requested by %s", request.remote_addr)
            env_file_path = ".env"
            if request.method == "POST":
                LOGGER.info("Updating settings.")
                form_data = request.form.to_dict()
                self.update_env_file(env_file_path, form_data)
                dotenv.load_dotenv(env_file_path, override=True)
                return redirect(url_for("settings"))

            sections = self.read_env_file(env_file_path)
            return render_template("settings.html", sections=sections)

        @self.app.route("/logs", methods=["GET", "POST"])
        def logs():
            LOGGER.info("Logs requested by %s", request.remote_addr)
            lines = int(request.form.get("lines", 25))
            log_file_path = "./logs/agent.log"
            logs = self.tail(log_file_path, lines)
            if request.headers.get("HX-Request"):
                LOGGER.debug("Returning partial log view.")
                return render_template("partials/log_view.html", logs=logs)
            return render_template("logs.html", logs=logs, selected_lines=lines)

    def run(self) -> None:
        """Run the webserver."""
        self._mqtt.subscribe(self._location_topic)
        self._mqtt.message_callback_add(
            self._location_topic,
            self._on_doorbell_message,
        )
        LOGGER.info("Webserver subscribed to MQTT topic: %s", self._location_topic)
        self._web_server = waitress.serve(self.app, host="0.0.0.0", port=self._port)
        LOGGER.info("Webserver started on port %i.", self._port)

    def stop(self) -> None:
        """Stop the webserver."""
        self._web_server.close()
        self._mqtt.unsubscribe(self._location_topic)
        LOGGER.info("Webserver unsubscribed from MQTT topic: %s", self._location_topic)
        self._mqtt.message_callback_remove(self._location_topic)
        LOGGER.info("Webserver stopped.")


    # pylint: disable=unused-argument
    def _on_doorbell_message(self, client, userdata, msg):
        """Process the doorbell message.

        param client: The client instance for this callback.
        param userdata: The private user data as set in Client() or userdata_set().
        param msg: An instance of MQTTMessage.
        """
        LOGGER.info("Doorbell message received. %s", msg.payload.decode("utf-8"))

    @staticmethod
    def read_env_file(file_path):
        """Reads the .env file and returns a dictionary of sections and their key-value pairs."""
        with open(file_path, "r", encoding="utf-8") as file:
            lines = file.readlines()

        sections = {}
        current_section = None

        for line in lines[4:]:
            line = line.strip()
            if line.startswith("# "):
                current_section = line[2:].strip()
                sections[current_section] = []
            elif line and not line.startswith("#"):
                key_value, *description = line.split("#")
                key, value = key_value.split("=")
                key = key.strip()
                value = value.strip()
                description = description[0].strip() if description else ""
                options = description.split("|") if "|" in description else None

                if options:
                    description = ""

                sections[current_section].append(
                    {
                        "key": key,
                        "value": value,
                        "description": description,
                        "options": (
                            [opt.strip() for opt in options] if options else None
                        ),
                    }
                )

        LOGGER.info("Read .env file successfully.")
        return sections

    @staticmethod
    def update_env_file(file_path, form_data):
        """Updates the .env file with new values from the form data."""
        with open(file_path, "r", encoding="utf-8") as file:
            lines = file.readlines()

        with open(file_path, "w", encoding="utf-8") as file:
            section = None
            for line in lines:
                if lines.index(line) < 4:
                    file.write(line)
                    continue
                if line.startswith("# "):
                    section = line[2:].strip()
                    if lines[lines.index(line) - 1].strip() != "":
                        file.write("\n" + line if section else line)
                    else:
                        file.write(line if section else line)
                elif "=" in line and section:
                    key = line.split("=")[0].strip()
                    if key in form_data:
                        value = form_data[key]
                        comment = ""
                        if "#" in line:
                            comment = "#" + line.split("#")[1]
                            comment = comment.strip()

                        file.write(f"{key}={value} {comment}\n")
                    else:
                        file.write(line)
                else:
                    file.write(line)

        LOGGER.info("Updated .env file successfully.")

    @staticmethod
    def tail(file_path, lines=25):
        """Read the last N lines from a file."""
        if not os.path.exists(file_path):
            LOGGER.error("Log file not found.")
            return ["Log file not found."]
        if os.stat(file_path).st_size == 0:
            LOGGER.error("Log file is empty.")
            return ["Log file is empty."]

        with open(file_path, "r", encoding="utf-8") as file:
            lines = file.readlines()[-lines:]
            return [line.strip() for line in lines]

if __name__ == "__main__":
     import mqtt_agent

     web_server = WebServer(
         mqtt_agent.MqttAgent(
             f"{os.environ.get('AGENT_LOCATION')}_{os.environ.get('AGENT_TYPE')}",
             [],
         )
     )
     web_server.run()