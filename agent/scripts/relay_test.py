""" Test script for relay triggers and pin mapping. """

# pylint: disable=import-error,consider-using-from-import
import os
import sys
import json
import time
import RPi.GPIO as GPIO
import dotenv

dotenv.load_dotenv()
GPIO.setmode(GPIO.BCM)

_pin_map_relay = json.loads(os.environ.get("PIN_MAP_RELAYS"))
_relays = []

try:
    for relay in _pin_map_relay:
        __relay_name = relay["name"]
        __pin = relay["pin"]
        print(f"Initializing relay: {__relay_name}")

        GPIO.setup(__pin, GPIO.OUT)
        GPIO.output(__pin, GPIO.HIGH)
        _relays.append({"name": __relay_name, "pin": __pin})

    while True:
        # User input to trigger relay
        __relay_name = input("Enter relay name to trigger: ")
        __relay = next((relay for relay in _relays if relay["name"] == __relay_name), None)

        if __relay:
            print(f"Triggering relay: {__relay_name}")
            GPIO.output(__relay["pin"], GPIO.LOW)
            print("Relay activated")
            time.sleep(1)
            GPIO.output(__relay["pin"], GPIO.HIGH)
            print("Relay deactivated")
        else:
            print(f"Relay not found: {__relay_name}")

except KeyError as e:
    print(f"Error: {e}")
    sys.exit(1)

except KeyboardInterrupt:
    GPIO.cleanup()
    print("Exiting...")
    sys.exit(0)
