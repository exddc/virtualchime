""" Test script for button press event detection and mapping to floors using gpiozero. """

# pylint: disable=import-error,consider-using-from-import
import os
import json
from gpiozero import Button
from signal import pause
import dotenv

# Load environment variables from .env file
dotenv.load_dotenv()

# Read configurations from environment variables
USE_PULL_UP = os.environ.get("PIN_PULL_UP_MODE", "False").lower() == "true"
PIN_MAP_FLOORS = json.loads(
    os.environ.get("PIN_MAP_FLOORS", '[{"name": "floor1", "pin": 17}]')
)

# Dictionary to store Button instances for each floor
buttons = {}

# Set up each button based on the floor configuration
try:
    for floor in PIN_MAP_FLOORS:
        floor_name = floor["name"]
        pin = floor["pin"]
        print(f"Initializing button for floor: {floor_name}")

        # Create a Button instance with pull-up/down and debouncing
        button = Button(pin, pull_up=USE_PULL_UP)

        # Define and assign the callback function for the button press
        button.when_pressed = lambda floor_name=floor_name: print(
            f"Button pressed on floor: {floor_name}"
        )

        # Store the button instance in the dictionary for future reference
        buttons[floor_name] = button

    # Wait indefinitely for button press events
    pause()
except KeyboardInterrupt:
    print("Exiting...")
