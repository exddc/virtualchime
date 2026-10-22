# Raspberry Pi Zero W with LSM-104F-8

Pin assignments and operating requirements for Chime. Parts and harness lengths are in the [BOM](../../bom.md).

## Power

The official Raspberry Pi 12.5 W supply (5.1 V, 2.5 A) feeds the Pi through PWR_IN (micro-USB). The amplifier takes 5 V from J8 pin 4; the Pi generates 3.3 V.

| Rail | Source | Consumers |
| --- | --- | --- |
| 5 V | PSU1 through PWR_IN | Pi, U1 amplifier through J8 pins 4 and 6 |
| 3.3 V | Pi onboard regulator | I2S logic levels, bench UART |

C1 and C2 sit across U1 VIN and GND and buffer speaker transients on the shared rail.

## J8 interface

J8 uses physical pin numbers; BCM is the Broadcom GPIO number. Pin 1 is nearest microSD on the inner row, marked by a square pad underneath.

| J8 | BCM | Net | Endpoint |
| --- | --- | --- | --- |
| 4 | n/a | 5V_AMP | U1 VIN, with C1 and C2 |
| 6 | n/a | GND_AMP | U1 GND |
| 8 | 14 | UART_TX | Bench serial adapter RX |
| 10 | 15 | UART_RX | Bench serial adapter TX |
| 12 | 18 | I2S_BCLK | U1 BCLK |
| 35 | 19 | I2S_LRCLK | U1 LRC |
| 40 | 21 | I2S_DIN | U1 DIN |

Reserve pin 7 (GPIO4), the default SD pin of the `max98357a` overlay, so amplifier shutdown control can be added without moving a signal. Leave pins 27 and 28 unwired; they are the HAT ID EEPROM bus. Pin 38 (GPIO20) is the I2S input and stays free for a later capture path.

Free pins: 3, 5, 11, 13, 15, 16, 18, 19, 21, 22, 23, 24, 26, 29, 31, 32, 33, 36, 37, 38.

Required `config.txt` settings:

```
enable_uart=1
dtoverlay=disable-bt
dtparam=i2s=on
dtoverlay=max98357a,no-sdmode
```

## Audio

| Device | Required configuration |
| --- | --- |
| U1, Adafruit MAX98357A 3006 | VIN to SD selects left playback; GAIN open selects 9 dB; C1 and C2 at VIN |
| SP1, EKULIT LSM-104F/SQ | 8 Ω ±15%, 3 W nominal; twisted speaker pair, maximum 100 mm |

SPK+ and SPK- are bridge outputs; neither speaker terminal connects to ground. Route the speaker pair away from the I2S leads ([amplifier pinouts](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts)).

GAIN stays open: into 8 Ω, 9 dB reaches about 1 W at the default `volume_ring` without clipping.

For a sinusoidal PCM signal, the [MAX98357A datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf) gives output level as input dBFS + 2.1 dB + gain. Chime scales samples linearly by volume / 100 before playback. The table assumes a full-scale sine in the WAV.

| Setting, 9 dB gain | Input level | Calculated speaker voltage | Calculated power into 8 Ω |
| --- | --- | --- | --- |
| `volume_notifications` 70, default | -3.1 dBFS | 2.51 V RMS / 3.55 V peak | 0.79 W |
| `volume_ring` 80, default | -1.9 dBFS | 2.87 V RMS / 4.06 V peak | 1.03 W |
| Volume 90 | -0.9 dBFS | 3.23 V RMS / 4.56 V peak | 1.30 W |
| Volume 100 | 0 dBFS | 3.59 V RMS / 5.08 V peak | 1.61 W; clips at the 5 V rail |

The datasheet rates 1.4 W at 1% THD+N into 8 Ω at 5 V. Settings above about 90 distort a full-scale sine; qualify loudness by listening in the closed housing.

## Firmware interface

| Interface | Required behavior |
| --- | --- |
| Platform | `openchime_rpi0w_defconfig`; A/B OTA layout; 2 GB minimum card size |
| Console | PL011 on pins 8 and 10 with Bluetooth disabled |
| Audio | Playback-only I2S card through the `max98357a` overlay; no SD GPIO |
| Volume | `volume_ring` and `volume_notifications` scale PCM; amplifier gain is fixed by the GAIN strap |
| Ring subscription | Ring publishes `ring/<unit>/pressed/<n>`. The shipped `mqtt_topics` and `ring_topic` defaults match it through `ring/+/pressed/#`. |
| Heartbeat | `heartbeat_topic`, default `chime/heartbeat`, reports `alive` or `degraded` |
| Web | `chime-webd` on port 8443; see [chime/README.md](../../../../chime/README.md) |

## Enclosure

Housing: [Body.step](../../cad/fusion/exports/Body.step), 110 × 110 × 47 mm, and [Front_Cover.step](../../cad/fusion/exports/Front_Cover.step), 110 × 110 × 12 mm. Print files are in [prints/](prints/).

| Part | Constraint |
| --- | --- |
| [Pi Zero W](https://www.raspberrypi.com/products/raspberry-pi-zero-w/) | 65 × 30 mm; PWR_IN reachable with the cover on |
| [MAX98357A 3006](https://www.adafruit.com/product/3006) | 19.4 × 17.8 × 3 mm; within 100 mm cable route of the speaker; C1 fixed against vibration |
| SP1 | Four M4 × 5 mm screws |

## Assembly

1. Solder the VIN–SD strap on U1. Leave GAIN open.
2. Solder C1 and C2 across U1 VIN and GND. C1 negative lead to GND.
3. Solder the J8 loom per the J8 table. No friction-fit housings on J8.
4. Twist the speaker pair and connect U1 + to speaker + and U1 − to speaker −.
5. Fix the Pi with four M2 × 4 mm screws and the speaker with four M4 × 5 mm screws.
6. Press the front cover onto the body.

## Datasheets

- [Raspberry Pi Zero W](https://www.raspberrypi.com/products/raspberry-pi-zero-w/)
- [Raspberry Pi 12.5 W micro-USB PSU](https://www.raspberrypi.com/products/micro-usb-power-supply/) (5.1 V, 2.5 A)
- [MAX98357A](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)
- [Adafruit MAX98357A breakout](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts)
- [EKULIT LSM-104F/SQ](https://cdn-reichelt.de/documents/datenblatt/I200/EKULIT-130050.pdf) (8 Ω ±15%, 3 W nominal)
