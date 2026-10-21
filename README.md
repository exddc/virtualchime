# Open Chime Smart Doorbell System

Open Chime is an open-source smart doorbell system built for people who want control over their home security without sacrificing privacy or design. Instead of trusting your doorbell data to cloud services, everything runs locally on hardware you own.

The system is designed to be a simple and affordable alternative to expensive designer doorbells. There should not be a compromise between security, privacy and good design. Open Chime aims to be as beautiful as designer doorbells, as secure as the most expensive security systems, and as private as your own home.

Read more about the rewrite process in my [blog post](https://timoweiss.me/blog/rewriting-virtual-chime).

## Current Status: Full Rewrite

This repository is being actively rewritten from scratch. After 1.5 years of running the original system in production, lessons learned have led to a completely new architecture. The old code is in the `old` branch and will be removed once the rewrite is complete.

## The New Architecture

The rewrite focuses on:

- **Compiled C++ services** instead of Python runtime on-device for reliability and speed
- **Explicit separation** between product runtime and configuration runtime
- **Custom minimal Linux image** tailored for fast boot and appliance-grade reliability
- **Single repository** containing OS, application, Web UI, scripts, and hardware assets
- **OTA updates** for firmware (A/B rootfs slots) and application services

## Products

The Open Chime family consists of multiple products working together:

### Chime (Speaker Box)

The first rewritten product - a purpose-built IoT speaker that plays doorbell sounds when triggered over MQTT. It runs on a custom ~300MB Linux image built with Buildroot, boots in under 5 seconds, and provides a secure web interface for configuration.

**Hardware:** Raspberry Pi Zero W, MAX98357A I2S amplifier, LSM-104F-8 speaker, 3D-printed enclosure

See [Chime README](chime/README.md) for detailed technical specifications.

### Ring (Doorbell)

The doorbell unit. Next product after Chime.

**Hardware:** Raspberry Pi Zero 2 W, I2S audio, Camera Module 3 Wide, two relays, 1–4 Kailh BOX buttons on the Clicker key board, and the Juicer power board with two Class II mains modules in a sealed enclosure. See [Ring hardware](hardware/ring/README.md).

### Base

A future product.

## Repository Structure

```
├── platform/           # Product-agnostic C++ platform (`oc::`; see platform/README.md)
├── chime/              # Chime ring service and HTTPS setup daemon (`src/webd/`)
├── webui/              # Svelte configuration UI served by chime-webd
├── buildroot/          # Chime (Pi Zero W) and Ring (Pi Zero 2 W) images, overlays, and OTA tooling
├── hardware/           # CAD, BOM, wiring (chime/, ring/)
├── scripts/            # Build, CI, flash, deploy, and local-run helpers
├── docs/               # Operational documentation
├── mosquitto/          # Local Mosquitto config for docker-compose
├── ring/               # Doorbell (next product)
└── base/               # Future product
```

## Documentation

- [Platform README](platform/README.md) — product-agnostic library, dependency direction, adding a route
- [Chime README](chime/README.md) — Chime runtime, webd, and config keys
- [Buildroot README](buildroot/README.md) — Image build, flash, and deploy for Chime and Ring
- [Reliability runbook](docs/reliability-runbook.md) — On-device logs, MQTT, webd/TLS, persistent data, OTA, recovery
- [Chime hardware](hardware/chime/README.md): CAD, BOM, wiring, and interface
- [Ring hardware](hardware/ring/README.md): doorbell BOM, wiring, and pin map

## License

This project is licensed under the [MIT License](LICENSE).
