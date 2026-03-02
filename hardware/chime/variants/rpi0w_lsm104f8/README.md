# Raspberry Pi Zero W LSM-104F-8

## Description

This variant is built for the Raspberry Pi Zero W and uses the LSM-104F-8 speaker in combination with the MAX98357A amplifier.

## BOM

- 1x Raspberry Pi Zero W
- 1x LSM-104F-8 speaker
- 1x MAX98357A amplifier
- 4x M4x5mm screws
- 4x M2x4mm screws
- 1x 3D printed body
- 1x 3D printed face cover

## Assembly

1. Attach the MAX98357A amplifier to the speaker with the + and - terminals facing the correct way.
2. Attach the MAX98357A amplifier to the Raspberry Pi Zero W to the GPIO pins.
3. Fix the Raspberry Pi Zero W to the enclosure with the 4x M2x4mm screws. Check that the PWR_IN USB port is accessible from the outside of the enclosure.
4. Screw the Speaker to the enclosure with the 4x M4x5mm screws.
5. Press the face cover onto the body.

## Wiring

| Raspberry Pi Zero W | MAX98357A |
|--------------------|-----------|
| Pin 4 (5V)         | VIN       |
| Pin 6 (GND)        | GND       |
| Pin 12 (GPIO18)    | BCLK      |
| Pin 35 (GPIO19)    | LRC       |
| Pin 40 (GPIO21)    | DIN       |
| 5V                 | SD        |

## Volume control note

MAX98357A does not expose runtime digital volume control over I2S. On common breakout boards, gain is selected by the SD/GAIN pin strapping and is therefore hardware-defined unless that pin is connected to a controllable GPIO. In this project, volume is handled in software via ALSA mixer level before playback.
