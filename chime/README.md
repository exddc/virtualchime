# Chime Service

`chime` is a minimal MQTT-to-audio daemon for the Raspberry Pi Zero W.

Operational runbook: `RELIABILITY_RUNBOOK.md`.

## Runtime Behavior

1. Loads config from `/etc/chime.conf` (or `$CHIME_CONFIG`).
2. Connects to MQTT broker and subscribes to configured topics.
3. When a message arrives on `ring_topic`, plays `sound_path` using `aplay`.
4. Publishes `heartbeat_topic` every `heartbeat_interval` seconds.
5. Automatically reconnects to MQTT after disconnect or loop errors.

## Reliability Logging

All logs go to `/var/log/chime.log` through the init supervisor (`S99chime`).

The daemon now logs:
- Service lifecycle (`service starting`, config loaded, shutdown reason, `service stopped`)
- MQTT lifecycle (connect attempts, successful connection, subscribe results, disconnects, loop errors, reconnect attempts, heartbeat publish success/fail)
- Message traffic (topic, qos, retain, payload length and sanitized payload)
- Ring handling (`ring received`, audio playback start, playback completion/failure, dedup when already playing)
- WiFi state (`operstate` and `carrier`) and changes/dropouts for the configured interface
- Periodic health summary every 60 seconds (message counters, reconnect counters, connection state)

## Config Keys

See `/etc/chime.conf` for defaults.

Daemon keys:
- `mqtt_host`, `mqtt_port`, `mqtt_client_id`
- `mqtt_topics` (comma-separated)
- `mqtt_subscribe_qos` (0-2)
- `heartbeat_interval` (0 disables)
- `heartbeat_topic`
- `ring_topic`
- `sound_path`
- `audio_enabled`
- `wifi_interface`
- `wifi_check_interval` (0 disables WiFi state checks)

Init-service keys (used by `S41timesync` and `S99chime`):
- `ntp_servers` (comma-separated)
- `time_http_urls` (comma-separated HTTP URLs used as fallback Date source)
- `time_sync_retries`, `time_sync_retry_delay`, `time_sync_interval`
- `log_max_bytes`, `log_rotate_keep`, `log_rotate_check_interval`
