from gpiozero import Button
from signal import pause

# Assign your GPIO pins for the buttons
BUTTON_1_PIN = 17  # Replace with your actual GPIO pin number
BUTTON_2_PIN = 27  # Replace with your actual GPIO pin number
BUTTON_3_PIN = 22  # Replace with your actual GPIO pin number

# Create button objects
button1 = Button(BUTTON_1_PIN)
button2 = Button(BUTTON_2_PIN)
button3 = Button(BUTTON_3_PIN)

# Define button press callback functions
def on_button_1_pressed():
    print("Button 1 pressed")

def on_button_2_pressed():
    print("Button 2 pressed")

def on_button_3_pressed():
    print("Button 3 pressed")

# Attach the button press events
button1.when_pressed = on_button_1_pressed
button2.when_pressed = on_button_2_pressed
button3.when_pressed = on_button_3_pressed

# Keep the script running to listen for button presses
pause()
