""" Test script for button press event detection and mapping to floors. """

import RPi.GPIO as GPIO
from signal import pause
import dotenv
import os
import json

dotenv.load_dotenv()
GPIO.setmode(GPIO.BCM)

_pin_pullup_mode = GPIO.PUD_UP if os.environ.get("PIN_PULL_UP_MODE") == "True" else GPIO.PUD_DOWN
_pin_map_floors = json.loads(os.environ.get("PIN_MAP_FLOORS"))

try:
    for floor in _pin_map_floors:
        __floor_name = floor["name"]
        __pin = floor["pin"]
        print(f"Initializing button for floor: {__floor_name}")

        GPIO.setup(__pin, GPIO.IN, pull_up_down=_pin_pullup_mode)

        GPIO.add_event_detect(
            __pin,
            GPIO.FALLING,
            callback=lambda channel, floor=__floor_name: print(f"Button pressed on floor: {floor}")
        )
    pause()
except KeyboardInterrupt:
    GPIO.cleanup()
    print("Exiting...")
    exit(0)