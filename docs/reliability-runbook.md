# Reliability runbook

On-device operations for a flashed Open Chime image. Build and flash steps stay in [buildroot/README.md](../buildroot/README.md). Product behavior and config keys stay in [chime/README.md](../chime/README.md).

SSH as `root` unless noted. Serial console is `ttyS0` at `115200`.

This runbook describes the Chime image. The Ring image shares OTA, networking, time sync, and `chime-webd`, but keeps a read-only root and different paths; see [Ring image](../buildroot/README.md#ring-image).

## First boot with no MQTT broker

A clean image ships `mqtt_host=` in `/etc/chime.conf`. Empty means **not configured**, not loopback.

Expected behavior:

- `chime-webd` stays up on HTTPS port `8443` for WiFi and broker setup.
- `chime` stays running but does **not** open a broker connection.
- `/var/log/chime.log` reports `mqtt_host is not configured; ring service waiting for setup via chime-webd`.
- After saving a real `mqtt_host` in the web UI (or editing `/etc/chime.conf`) and applying, `S99chime` restarts and then connects.

Check:

```sh
grep -E '^mqtt_host=' /etc/chime.conf
/etc/init.d/S45webd status
/etc/init.d/S99chime status
grep mqtt_host /var/log/chime.log | tail
```

## Service status and logs

Init scripts (BusyBox):

| Service | Script | Process | Log |
| --- | --- | --- | --- |
| Persistent `/data` bind | `/etc/init.d/S31persistent` | — | — |
| Networking | `/etc/init.d/S40network` | `wpa_supplicant`, `udhcpc` | — |
| Time sync | `/etc/init.d/S41timesync` | `ntpd` / fallback | `/var/log/chime.log` |
| OTA boot guard | `/etc/init.d/S42otaguard` | — | — |
| Setup daemon | `/etc/init.d/S45webd` | `chime-webd` | `/var/log/chime-web.log` |
| SSH | `/etc/init.d/S50dropbear` | `dropbear` | — |
| Ring daemon | `/etc/init.d/S99chime` | `chime` | `/var/log/chime.log` |
| OTA confirm | `/etc/init.d/S99otaconfirm` | — | — |

```sh
/etc/init.d/S99chime status
/etc/init.d/S45webd status
/etc/init.d/S41timesync status

tail -f /var/log/chime.log
tail -f /var/log/chime-web.log
ls -lh /var/log/chime.log*
```

`S99chime` and `S45webd` supervise their daemons and restart them after a crash. Status reports both the supervisor PID (`/var/run/chime_supervisor.pid`, `/var/run/chime-webd_supervisor.pid`) and whether the process is running.

From a host:

```sh
ssh root@<device-ip> 'tail -f /var/log/chime.log'
```

## MQTT and ring checks

```sh
grep -E '^(mqtt_host|mqtt_port|mqtt_topics|ring_topic)=' /etc/chime.conf
grep -E 'mqtt|ring received|heartbeat' /var/log/chime.log | tail
```

If `mqtt_host` is empty, do not expect a connect attempt. After a broker is configured, logs should show `connecting to broker`, `connected`, and `subscribed topic=...`.

Publish a test ring from a machine that can reach the broker (not from an unconfigured device):

```sh
# From the repo, against a local Docker broker:
./scripts/ring.sh

# Against an explicit broker:
./scripts/ring.sh ring/pressed pressed <broker-host> 1883
```

On-device, a successful ring logs `ring received` in `/var/log/chime.log`.

## webd and TLS

`chime-webd` listens on `0.0.0.0:8443` and is independent of MQTT. Hostname is `chime` (`/etc/hostname`).

TLS material (generated on first start if missing):

- `/etc/chime-web/tls/cert.pem`
- `/etc/chime-web/tls/key.pem`

```sh
/etc/init.d/S45webd status
tail -f /var/log/chime-web.log
ls -l /etc/chime-web/tls/cert.pem /etc/chime-web/tls/key.pem
```

Open `https://<device-ip>:8443` (self-signed certificate). A new device shows pairing until an admin password is set. After pairing, sign in with that password. Apply from the UI restarts networking (`/etc/init.d/S40network restart`) and the ring service (`/etc/init.d/S99chime restart`).

The pairing code is written to the serial console (`ttyS0`) while the device is unpaired. It is also stored at `/data/var/lib/chime/auth/pairing.code` (mode `0600`) for operators with serial or SSH access. After pairing that file is removed.

## Administration authentication

`chime-webd` is a single-admin appliance. It stores a PBKDF2-HMAC-SHA256 password verifier (600000 iterations) at `/data/var/lib/chime/auth/admin.verifier` (runtime path `/var/lib/chime/auth/admin.verifier` via the persistent bind). Sessions live in memory (12 hour cookie) and do not survive process restart. Mutating API calls need the `chime_session` cookie and `X-CSRF-Token`. Pairing and login failures are rate-limited per client address; malformed JSON does not consume that budget.

To restore pairing without touching Wi-Fi, MQTT, or ring sounds:

```sh
rm -rf /data/var/lib/chime/auth
reboot
```

Do not delete `/data/var/lib/chime/` as a whole; that directory also holds observed topics and uploaded sounds.

## Persistent config

`/data` is `mmcblk0p4` (`/etc/fstab`). `S31persistent` copies factory files into `/data` when missing, bind-mounts `/data/etc` onto `/etc/persistent`, and installs symlinks so runtime paths see those files. `rename(2)` can then replace `chime.conf` atomically. File bind-mounts from older images are unmounted first.

| Persistent path | Runtime path |
| --- | --- |
| `/data/etc/chime.conf` | `/etc/chime.conf` → `/etc/persistent/chime.conf` |
| `/data/etc/wpa_supplicant.conf` | `/etc/wpa_supplicant/wpa_supplicant.conf` → `/etc/persistent/wpa_supplicant.conf` |
| `/data/etc/dropbear/` | `/etc/dropbear` → `/etc/persistent/dropbear` (SSH host keys, symlink set at build time) |
| `/data/var/lib/chime/` | `/var/lib/chime/` (observed topics, uploaded ring sounds, `/auth` verifier, `config.fatal`) |
| `/data/ota/` | OTA pending/status (not bind-mounted) |

WiFi credentials are **not** tracked in git. Image builds copy `wpa_supplicant.conf.example` locally to `wpa_supplicant.conf` (gitignored). SSH keys use `root/.ssh/authorized_keys.example` the same way.

To return config to factory defaults without reflashing, delete the persisted file and reboot so `copy_if_missing` restores the image copy:

```sh
rm -f /data/etc/chime.conf
reboot
```

## Release identity

```sh
cat /etc/openchime-release
cat /etc/chime-app-version
uname -r
/usr/local/bin/chime --version
```

`/etc/openchime-release` is written at image build (`post_build.sh`) and includes `OPENCHIME_OS_VERSION`, `CHIME_APP_VERSION`, `CHIME_CONFIG_VERSION`, kernel release, and git metadata.

From a host:

```sh
./scripts/deploy.sh version <device-ip>
```

## OTA status, confirm, and rollback

A/B rootfs: slot A is `/dev/mmcblk0p2`, slot B is `/dev/mmcblk0p3`. Boot device is `/dev/mmcblk0p1` (`/boot/cmdline.txt` `root=`).

On-device files (from `ota-common.sh`):

- `/data/ota/pending.env` — in-flight update (`STATE`, `TARGET_SLOT`, `PREVIOUS_SLOT`, `ATTEMPTS_LEFT`, …)
- `/data/ota/status.env` — last result (`confirmed`, `boot_unconfirmed`, `rollback_auto`, …)

```sh
# Host
./scripts/deploy.sh ota-status <device-ip>
./scripts/deploy.sh firmware <device-ip> --wait-online
./scripts/deploy.sh firmware-rollback <device-ip> --slot A

# Device
cat /proc/cmdline
cat /data/ota/status.env
cat /data/ota/pending.env
/usr/local/sbin/ota-confirm
/usr/local/sbin/ota-rollback --slot A
```

Boot-time behavior:

- `S42otaguard` decrements `ATTEMPTS_LEFT` on a pending slot. When attempts are exhausted it runs `ota-rollback` to `PREVIOUS_SLOT` and reboots.
- `S99otaconfirm` runs `ota-confirm` when `S99chime` and `S45webd` supervisors are running. MQTT does **not** have to be configured for that health check.

`--wait-online` on `deploy.sh firmware` waits for SSH, runs `ota-confirm`, then prints `ota-status`.

## Recovery

1. **Serial**: GPIO14 TX → adapter RX, GPIO15 RX → adapter TX, GND → GND. `enable_uart=1` in boot `config.txt`. Getty is on `ttyS0` in `/etc/inittab`.
2. **WiFi**: `lsmod | grep brcm`, `ip link show wlan0`, `wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf`, `udhcpc -i wlan0`. See [buildroot/README.md](../buildroot/README.md) for module/firmware notes.
3. **SSH**: `ssh root@<device-ip>` (key-only). Discover a Pi Zero W: `arp -a | grep b8:27:eb`.
4. **Stuck pending OTA**: inspect `/data/ota/pending.env`; roll back with `/usr/local/sbin/ota-rollback` or `./scripts/deploy.sh firmware-rollback <device-ip>`.
5. **Bad persisted config**: remove `/data/etc/chime.conf` and/or `/data/etc/wpa_supplicant.conf`, reboot.
6. **Lost admin password**: remove `/data/var/lib/chime/auth` only, reboot, then pair again from the serial pairing code. Wi-Fi and MQTT config stay in place.
7. **Reflash**: `./scripts/flash_sd.sh /dev/diskN` from a host (destroys the card, including `/data`).
