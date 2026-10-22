# Chime config schema

Product schema version **7**. `buildroot/version.env` `CHIME_CONFIG_VERSION` is the release-level gate and must equal this integer. The persisted file key is `schema_version`.

Source of truth: `schema/chime_config.json`. Generated artifacts:

- `chime/include/chime/generated/config_types.h`
- `chime/include/chime/generated/config_json.h`
- `webui/src/generated/config_schema.ts`

Regenerate with `python3 scripts/gen_chime_config_schema.py`. `scripts/check_config_schema.sh` fails when generated files, `chime.conf`, init-script defaults, or `CHIME_CONFIG_VERSION` drift.

## Ownership

| Role | Meaning |
| --- | --- |
| runtime | `chime` daemon (`ChimeConfig`) |
| webd | `chime-webd` HTTP API (`CoreConfig`) |
| ui | `webui` settings form |
| init-only | `S41timesync` or `S99chime`. Not daemon fields. |
| schema | migration / versioning |
| webd-process | read by `chime-webd` at start, not `/api/v1/config/core` |

Unknown assignment keys: **preserve**. Unknown assignment keys and comments are kept as written. Removed keys are dropped. Missing known keys are filled with schema defaults during migration.

Files that still hold every old default are migrated: `mqtt_topics` from `ring/pressed,ring/status` to `ring/+/pressed/#,ring/+/status`; `ring_topic` from `ring/pressed` to `ring/+/pressed/#`. Custom values are kept. Ring volume is `volume_ring`; notification volume is `volume_notifications`. Shipped MQTT topics follow Ring's `ring/<unit>/pressed/<n>` and `ring/<unit>/status` scheme.

## Key inventory

| Key | Owner | Type | Default | Required | Valid range | Secret | Persist | Role | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `schema_version` | schema | int | `7` | optional | 1-1000000 | no | file | schema | Persisted product schema version. Written by chime-migrate. Distinct from CHIME_CONFIG_VERSION in buildroot/version.env, which must equal this integer. |
| `mqtt_host` | runtime,webd | string | `` | required | max 256 chars, no whitespace | no | file | runtime/webd/ui | Empty in the file means the broker is not configured; the daemon waits. Saving from the UI requires a non-empty host. |
| `mqtt_port` | runtime,webd | int | `1883` | required | 1-65535 | no | file | runtime/webd/ui |  |
| `mqtt_client_id` | runtime,webd | string | `chime` | required | max 128 chars | no | file | runtime/webd/ui |  |
| `mqtt_username` | runtime,webd | string | `` | required | max 128 chars | no | file | runtime/webd/ui |  |
| `mqtt_password` | runtime,webd | string | `` | optional | max 256 chars | redact on read; preserve if omitted | file | runtime/webd/ui | Redacted on GET (mqtt_password_set). Omitted on POST preserves the stored value. |
| `mqtt_tls_enabled` | runtime,webd | bool | `false` | required | any string | no | file | runtime/webd/ui |  |
| `mqtt_tls_validate_certificate` | runtime,webd | bool | `true` | required | any string | no | file | runtime/webd/ui |  |
| `mqtt_tls_ca_file` | runtime,webd | string | `` | required | max 256 chars | no | file | runtime/webd/ui |  |
| `mqtt_tls_cert_file` | runtime,webd | string | `` | required | max 256 chars | no | file | runtime/webd/ui | Must be set together with mqtt_tls_key_file. |
| `mqtt_tls_key_file` | runtime,webd | string | `` | required | max 256 chars | no | file | runtime/webd/ui | Must be set together with mqtt_tls_cert_file. Path only; the key material is not stored in chime.conf. |
| `mqtt_topics` | runtime,webd | csv | ` (shipped ring/+/pressed/#,ring/+/status)` | required | comma-separated non-empty tokens | no | file | runtime/webd/ui | Shipped subscribe list matches every Ring unit: ring/+/pressed/# for presses (any button), ring/+/status for status. |
| `mqtt_subscribe_qos` | runtime | int | `0` | optional | 0-2 | no | file | runtime | Not exposed in the web UI. Operators edit the file directly. |
| `heartbeat_interval` | runtime | int | `60 (shipped 20)` | optional | 0-3600 | no | file | runtime | Seconds; 0 disables. Omitted-key default is 60. The image ships 20. |
| `heartbeat_topic` | runtime | string | `chime/heartbeat` | optional | max 256 chars | no | file | runtime | Chime reports it is alive. Default chime/heartbeat. Not exposed in the web UI. |
| `ntp_servers` | init | csv | `time.cloudflare.com,time.google.com,pool.ntp.org` | optional | comma-separated tokens | no | file | init-only | Init-only. Read by S41timesync, never by the chime or chime-webd daemons. |
| `time_http_urls` | init | csv | `http://connectivitycheck.gstatic.com/generate_204,http://detectportal.firefox.com/success.txt,http://example.com/` | optional | comma-separated tokens | no | file | init-only | Init-only HTTP Date fallback list for S41timesync. |
| `time_sync_retries` | init | int | `6` | optional | 1-100 | no | file | init-only | Init-only. S41timesync falls back to this default when the value is missing or invalid. |
| `time_sync_retry_delay` | init | int | `5` | optional | 1-3600 | no | file | init-only | Init-only seconds between startup time-sync attempts. |
| `time_sync_interval` | init | int | `3600` | optional | 0-86400 | no | file | init-only | Init-only. 0 disables periodic resync. |
| `ring_topic` | runtime,webd | string | `ring/+/pressed/#` | required | max 256 chars, no whitespace | no | file | runtime/webd/ui | Filter that triggers audio. Default ring/+/pressed/# matches any button on any Ring unit. Payload has no required schema. |
| `sound_path` | runtime | string | `/usr/local/share/chime/ring.wav` | optional | max 256 chars | no | file | runtime | Ring WAV path. The UI replaces the file via /api/v1/ring/sounds rather than editing this key. |
| `notification_success_sound_path` | runtime,webd | string | `/usr/local/share/chime/test.wav` | optional | max 256 chars | no | file | runtime/webd/ui |  |
| `notification_failure_sound_path` | runtime,webd | string | `/usr/local/share/chime/ring.wav` | optional | max 256 chars | no | file | runtime/webd/ui |  |
| `volume_ring` | runtime,webd | int | `80` | required | 0-100 | no | file | runtime/webd/ui | Software volume for ring playback. |
| `volume_notifications` | runtime,webd | int | `70` | required | 0-100 | no | file | runtime/webd/ui | Software volume for success/failure notification playback. |
| `audio_enabled` | runtime | bool | `true` | optional | any string | no | file | runtime | Not exposed in the web UI. |
| `wifi_interface` | runtime,webd-process | string | `wlan0` | optional | max 32 chars | no | file | runtime | Used by the chime daemon and by chime-webd at process start for Wi-Fi scan. Not a /api/v1/config/core field. |
| `wifi_check_interval` | runtime | int | `5` | optional | 0-3600 | no | file | runtime | Seconds; 0 disables interface-state logs. Not exposed in the web UI. |
| `log_max_bytes` | init | int | `262144` | optional | 1024-104857600 | no | file | init-only | Init-only log rotation size for /var/log/chime.log. Not a daemon field. |
| `log_rotate_keep` | init | int | `5` | optional | 1-100 | no | file | init-only | Init-only rotated file count for S99chime. |
| `log_rotate_check_interval` | init | int | `30` | optional | 1-3600 | no | file | init-only | Init-only seconds between log-size checks in S99chime. |
| `wifi_ssid` | webd | string | `` | required | max 32 chars | no | wpa | webd/ui | Stored in wpa_supplicant.conf, not chime.conf. |
| `wifi_password` | webd | string | `` | optional | max 63 chars | redact on read; preserve if omitted | wpa | webd/ui | Stored in wpa_supplicant.conf. Redacted on GET (wifi_password_set). Omitted on POST preserves the stored PSK. |

## Removed keys

| Key | When | Reason |
| --- | --- | --- |
| `volume_other` | dropped in v5 | Persisted and shown in the UI but never consumed by the audio path. Ring playback uses volume_ring; notification playback uses volume_notifications. There is no third audio category. |

## Renamed keys

| Key | When | New name |
| --- | --- | --- |
| `volume_bell` | renamed in v6 | `volume_ring` |

## Before / after samples

Before (shipped schema 4, no persisted version key): [`docs/config-samples/chime.conf.v4`](config-samples/chime.conf.v4).

After: [`buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf`](../buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf).

Migration from unversioned files treats them as schema 4. `chime-migrate` runs from `S32config-migrate` after `S31persistent` bind-mounts `/data/etc` onto `/etc/persistent` and before `S41timesync`, `S45webd`, and `S99chime`. The daemons also migrate on start so `scripts/local_chime.sh` stays consistent. Ordered per-version steps in `schema/chime_config.json` `migrations` run from the file version to the current schema, then missing keys are filled and invalid values repaired. On rewrite, migration writes `<resolved-path>.bak` (following the `/etc/chime.conf` symlink) and replaces the live file with `rename(2)`. A write failure leaves the original inode unchanged. Malformed or future `schema_version` values are a permanent failure: `chime-migrate` and the daemons exit 78 (`EX_CONFIG`), `S32config-migrate` records `/var/lib/chime/config.fatal`, and `S45webd` / `S99chime` do not restart.

Secrets: GET `/api/v1/config/core` returns `wifi_password_set` and `mqtt_password_set`, never the password values. POST may omit `wifi_password` / `mqtt_password` to keep the stored secret.
