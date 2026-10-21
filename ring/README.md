# Ring

Doorbell product sources. Hardware for this unit is [hardware/ring/](../hardware/ring/README.md).

The Ring OS image (Raspberry Pi Zero 2 W) is built from `buildroot/configs/openchime_ring_rpi02w_defconfig`; see [Ring image](../buildroot/README.md#ring-image) for build, boot configuration, and bring-up checks. Until a Ring daemon and configuration server exist here, the image runs `chime-webd` for pairing, Wi-Fi, and MQTT.
