# Chime Service

`chime` is a minimal MQTT-to-audio daemon for the Raspberry Pi Zero W.
`chime-webd` is a lightweight HTTPS control-plane daemon for web configuration APIs/UI.

Operational runbook: [docs/reliability-runbook.md](../docs/reliability-runbook.md).

## Local CI Checks

Run the same checks as GitHub chime CI from the repo root:

```bash
./scripts/chime_ci.sh
```

This configures the native CMake build with `-DOC_BUILD_TESTS=ON`, builds, and runs `ctest --output-on-failure`. A plain `cmake -S .` leaves tests off, so production and offline configures do not download doctest.

CMake is the single build graph for `oc_platform`, `oc_platform_http`, `chime_core`, `chime_webd_core`, `chime`, and `chime-webd`. Native CI, `scripts/local_chime.sh`, and the Buildroot `chime` package all consume that graph—add a source to a CMake target and it is included everywhere. Platform code cannot include `chime/` headers or link Chime targets; `./scripts/platform_only_ci.sh` builds and tests `platform/` with `chime/` absent. See [platform/README.md](../platform/README.md).

Useful options:
- `./scripts/chime_ci.sh --fix-format` to apply `clang-format` to the checked files.
- `./scripts/chime_ci.sh --skip-build` to run CTest against an existing build directory.
- `./scripts/chime_ci.sh --skip-tests` to skip CTest.
- `CHIME_CI_SCOPE=changed CHIME_CI_BASE_REF=origin/main ./scripts/chime_ci.sh` to lint/format only files changed from a base ref.

Native C++ tests can also be run directly:

```bash
cmake -S . -B chime/build-ci -G Ninja -DCMAKE_BUILD_TYPE=Release -DOC_BUILD_TESTS=ON
cmake --build chime/build-ci
ctest --test-dir chime/build-ci --output-on-failure --no-tests=error
```

For repository-wide formatting/lint checks (C/C++ + webui Biome), run:

```bash
./scripts/lint_format_ci.sh
```

## Runtime Behavior

1. Loads config from `/etc/chime.conf` (or `$CHIME_CONFIG`).
2. If `mqtt_host` is empty, waits without connecting and logs that MQTT is not configured. `chime-webd` remains available for setup.
3. Otherwise connects to the MQTT broker and subscribes to configured topics.
4. When a message arrives on `ring_topic` (shipped `ring/+/pressed/#`), plays `sound_path` using `aplay`.
5. Publishes `heartbeat_topic` (shipped `chime/heartbeat`) every `heartbeat_interval` seconds.
6. Automatically reconnects to MQTT after disconnect or loop errors.

## Web Platform (`chime-webd`)

- Serves HTTPS UI/API on port `8443`.
- First boot is unpaired. The pairing code is printed to the serial console (`/dev/console`) and stored at `/var/lib/chime/auth/pairing.code` (`/data/var/lib/chime/auth/pairing.code` on device). Pairing (`POST /api/v1/auth/pair`) sets the admin password and closes.
- After pairing, administration APIs require a session cookie (`chime_session`, `HttpOnly; Secure; SameSite=Strict`) plus `X-CSRF-Token` on POST/PUT. Login is `POST /api/v1/auth/login`. Logout is `POST /api/v1/auth/logout`.
- Public routes: static UI assets, `GET /api/v1/system/version`, `GET /api/v1/auth/status`, unpaired `POST /api/v1/auth/pair`, and `POST /api/v1/auth/login`.
- Hosts current v1 endpoints:
  - `GET /`
  - `GET /api/v1/auth/status`
  - `POST /api/v1/auth/pair`
  - `POST /api/v1/auth/login`
  - `POST /api/v1/auth/logout`
  - `GET /api/v1/config/core`
  - `POST /api/v1/config/core`
  - `GET /api/v1/wifi/scan`
  - `GET /api/v1/mqtt/topics` (observed MQTT topics for ring-topic suggestions)
- Config GET/POST return `wifi_password_set` and `mqtt_password_set` only. Password hashes, Wi-Fi/MQTT passwords, session tokens, and pairing codes are never returned.
- The salted admin verifier is `/var/lib/chime/auth/admin.verifier` (mode `0600`), PBKDF2-HMAC-SHA256 with 600000 iterations (OWASP 2023). The stored iteration count is used at verify time, so a later default change still accepts existing verifiers. Wipe that directory under `/data` to restore pairing without deleting Wi-Fi or MQTT config.
- Reserves `/api/v1/system/*`, `/api/v1/device/*`, and `/api/v1/diagnostics/*` for future API expansion (`501` responses in v1).
- Uses self-signed TLS cert/key at:
  - `/etc/chime-web/tls/cert.pem`
  - `/etc/chime-web/tls/key.pem`
- Optional static UI override:
  - Set `CHIME_WEBD_UI_DIST_DIR` to serve built web assets (for example Svelte
    `dist/`) instead of the embedded fallback UI.
- Optional auth overrides:
  - `CHIME_WEBD_AUTH_DIR` (default `/var/lib/chime/auth`)
  - `CHIME_WEBD_BOOTSTRAP_PASSWORD` (development only; pairs immediately)
  - `CHIME_WEBD_PAIRING_CODE` (development only; fixed unpaired setup secret)
- Session cookies always include `Secure`. The Vite dev proxy strips that flag so `http://127.0.0.1:5173` can store them.
- Runs as a separate process from `chime` for ring-path reliability isolation.

JSON and HTTP internals, library choice, and size measurements: [docs/webd-http-json.md](../docs/webd-http-json.md). Add product routes by implementing `oc::http::ProductRoutes` (Chime: `WebApi::Register`) rather than editing platform routing. See [platform/README.md](../platform/README.md).

## Reliability Logging

All ring-service logs go to `/var/log/chime.log` through the init supervisor (`S99chime`). `chime-webd` logs to `/var/log/chime-web.log`. Command examples are in the [reliability runbook](../docs/reliability-runbook.md).

The daemon logs:
- Service lifecycle (`service starting`, config loaded, shutdown reason, `service stopped`)
- Unconfigured MQTT (`mqtt_host` empty: wait, no broker connection)
- MQTT lifecycle (connect attempts, successful connection, subscribe results, disconnects, loop errors, reconnect attempts, heartbeat publish success/fail)
- Message traffic (topic, qos, retain, payload length and sanitized payload)
- Ring handling (`ring received`, audio playback start, playback completion/failure, dedup when already playing)
- WiFi state (`operstate` and `carrier`) and changes/dropouts for the configured interface
- Periodic health summary every 60 seconds (message counters, reconnect counters, connection state)

## Config Keys

The product schema is `schema/chime_config.json`. Field names, types, defaults, validation, ownership, secrets, and migration live there. Runtime `ChimeConfig`, webd `CoreConfig`, TypeScript UI types, and this file's inventory are generated from it. See [config schema](../docs/config-schema.md).

See `/etc/chime.conf` for image values. Empty `mqtt_host` means the broker is not configured.

Daemon keys:
- `mqtt_host`, `mqtt_port`, `mqtt_client_id`
- `mqtt_username`, `mqtt_password` (optional broker auth; password is redacted on API reads)
- `mqtt_tls_enabled`, `mqtt_tls_validate_certificate`
- `mqtt_tls_ca_file`, `mqtt_tls_cert_file`, `mqtt_tls_key_file`
- `mqtt_topics` (comma-separated; shipped `ring/+/pressed/#,ring/+/status`)
- `mqtt_subscribe_qos` (0-2)
- `heartbeat_interval` (0 disables)
- `heartbeat_topic` (shipped `chime/heartbeat`)
- `ring_topic` (shipped `ring/+/pressed/#`: any button on any Ring unit)
  - Supports MQTT topic filters (`+` and `#`) for matching incoming message topics
  - Migration moves only the old shipped `ring/pressed` value; custom topics are kept
- `sound_path`
- `volume_ring` (0-100, ring events)
- `volume_notifications` (0-100, startup/notification playback)
- `audio_enabled`
- `wifi_interface`
- `wifi_check_interval` (0 disables WiFi state checks)

Init-service keys (used by `S41timesync` and `S99chime`, not by the daemons):
- `ntp_servers` (comma-separated)
- `time_http_urls` (comma-separated HTTP URLs used as fallback Date source)
- `time_sync_retries`, `time_sync_retry_delay`, `time_sync_interval`
- `log_max_bytes`, `log_rotate_keep`, `log_rotate_check_interval`

`schema_version` is written by `chime-migrate`. `volume_other` was removed in schema 5. `volume_bell` was renamed to `volume_ring` in schema 6. Malformed or future `schema_version` values make `chime-migrate`, `chime`, and `chime-webd` exit 78 so the init supervisors do not restart-loop.
