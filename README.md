# Virtual Chime Open Smart Doorbell System

Virtual Chime is an open source smart doorbell system built for people who want control over their home security without sacrificing privacy or design. Instead of trusting your doorbell data to cloud services, everything runs locally on hardware you own.

The system is designed to be a simple and affordable alternative to expensive designer doorbells. There should not be a compromise between security, privacy and good design. Virtual Chime aims to be as beautiful as designer doorbells, as secure as the most expensive security systems, and as private as your own home.

Read more about the rewrite process in my [blog post](https://timoweiss.me/blog/rewriting-virtual-chime).

## Current Status: Full Rewrite

This repository is being actively rewritten from scratch. After 1.5 years of running the original system in production, lessons learned have led to a completely new architecture. The old code is in the `old` branch and will be removed once the rewrite is complete.

## The New Architecture

The rewrite focuses on:

- **Compiled C++ services** instead of Python runtime on-device for reliability and speed
- **Explicit separation** between product runtime and configuration runtime
- **Custom minimal Linux image** tailored for fast boot and appliance-grade reliability
- **Single repository** containing OS, application, Web UI, scripts, and hardware assets
- **OTA updates** for software services and Web UI

## Products

The Virtual Chime family consists of multiple products working together:

### Chime (Speaker Box)

The first rewritten product - a purpose-built IoT speaker that plays doorbell sounds when triggered over MQTT. It runs on a custom ~300MB Linux image built with Buildroot, boots in under 5 seconds, and provides a secure web interface for configuration.

**Hardware:** Raspberry Pi Zero W, MAX98357A I2S amplifier, LSM-104F-8 speaker, 3D-printed enclosure

See [Chime README](chime/README.md) for detailed technical specifications.

### Bell (Doorbell) - In Progress

The actual doorbell unit with camera, button, and MQTT integration. Currently in planning stages, will follow the same architecture as the chime.

## Repository Structure

```
├── buildroot/          # Buildroot configuration for custom Linux images
├── chime/              # C++ source for chime audio/MQTT service
├── chime-webd/         # C++ source for HTTPS configuration daemon
├── webui/              # Svelte-based web configuration interface
├── scripts/            # Build and deployment automation
├── hardware/           # 3D models and hardware designs
└── docs/               # Documentation and blog drafts
```

## Documentation

- [Chime README](chime/README.md) - Technical overview of the chime product
- [Hardware README](hardware/README.md) - Detailed writeup of the hardware designs

## License

tbd.