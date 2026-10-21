# Buildroot images

Lightweight Linux images built from one external tree:

| Product | Board | Defconfig | Image |
|---------|-------|-----------|-------|
| Chime | Raspberry Pi Zero W | `configs/openchime_rpi0w_defconfig` | `output/sdcard.img` |
| Ring | Raspberry Pi Zero 2 W | `configs/openchime_ring_rpi02w_defconfig` | `output/ring/sdcard.img` |

Both share `board/common/` (OTA scripts, networking, time sync, SSH, the `chime-webd` supervisor, and Wi-Fi/SSH secrets). The Ring image is described in [Ring image](#ring-image).

On-device operations (service logs, MQTT, webd/TLS, persistent data, OTA, recovery) are in [docs/reliability-runbook.md](../docs/reliability-runbook.md).

## Quick Start

```bash
# 1. Configure WiFi credentials (required, shared by both products)
cp board/common/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf.example \
   board/common/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf
# Edit with SSID/password

# 2. Add SSH key (required, shared by both products)
cat ~/.ssh/id_ed25519.pub > board/common/rootfs_overlay/root/.ssh/authorized_keys

# 3. Build - this takes 30-90 minutes
./scripts/docker_build.sh                  # Chime
./scripts/docker_build.sh --product ring   # Ring

# 4. Flash - this takes 1-2 minutes
./scripts/flash_sd.sh /dev/diskN                  # Chime
./scripts/flash_sd.sh --product ring /dev/diskN   # Ring

# 5. Check versions on a Pi
./scripts/deploy.sh version <pi-ip>
```

## Versioning

Version sources:

```bash
buildroot/version.env
chime/VERSION
```

`buildroot/version.env`
```bash
OPENCHIME_OS_VERSION=<x.y.z>
CHIME_CONFIG_VERSION=<n>
```

`chime/VERSION`
```bash
<x.y.z>
```

- `OPENCHIME_OS_VERSION`: bump when you build/flash a new OS image. Chime and Ring share it.
- `CHIME_CONFIG_VERSION`: bump when default `chime.conf` semantics or the product schema in `schema/chime_config.json` change.
- `chime/VERSION`: bump when `chime` binary behavior changes.

During image builds, `post_build.sh` writes `/etc/openchime-release` on the target rootfs.
Read versions on-device with:

```bash
cat /etc/openchime-release
cat /etc/chime-app-version
uname -r
/usr/local/bin/chime --version
```

To enforce version bumps in CI (or before merge):

```bash
./scripts/check_version_bump.sh origin/main HEAD
```

## Build System

Uses Docker to avoid host dependencies. Build artifacts stored in a Docker volume.

```bash
# Rebuild from scratch
./scripts/docker_build.sh --clear-docker-cache
```

## Hardware compatibility and minimum SD card size

- Chime image: Raspberry Pi Zero W.
- Ring image: Raspberry Pi Zero 2 W. Do not flash the Chime image on a Zero 2 W; use the Ring
  defconfig, which builds the Zero 2 W kernel, device tree, and Wi-Fi/Bluetooth firmware.

Image layout sizing:

- Boot partition: `128M` (`board/<board>/genimage.cfg`).
- Root filesystem A: `256M` (`configs/openchime_rpi0w_defconfig` output).
- Root filesystem B: `256M` (same image, inactive update slot).
- Data partition: `256M` (persistent state and OTA metadata).

That means the generated image is about 896 MB plus partition-table/alignment overhead.
The Ring uses `384M` per rootfs slot for the camera and Bluetooth stack, about 1.15 GB in total.

Practical guidance:

- Absolute minimum card size: **2 GB**
- Recommended card size for reliability/availability: **4 GB**

### Docker resource settings (recommended)

If your Mac is mostly idle during builds, Docker is likely CPU/RAM-limited.
In Docker Desktop, set:

- CPUs: `6-8`
- Memory: `8-12 GiB`
- Swap: `2-4 GiB`

### Fast iteration workflow

Use the full image build only when OS-level components changed (kernel, rootfs overlay,
Buildroot packages, boot files, image layout):

```bash
./scripts/docker_build.sh
```

For app-only changes (`chime/` or `platform/`), use:

```bash
./scripts/deploy.sh chime <pi-ip>
```

For web UI-only updates on an already-flashed device (replaces
`/usr/local/share/chime-web-ui/dist` without rebuilding the OS image):

```bash
./scripts/local_chime.sh webui-build
./scripts/deploy.sh chime <pi-ip> --with-webd
```

For OTA firmware slot updates (inactive rootfs write + reboot), use:

```bash
./scripts/deploy.sh firmware <pi-ip> --wait-online
```

Check OTA state and pending/confirmed status:

```bash
./scripts/deploy.sh ota-status <pi-ip>
```

Force rollback target slot for next boot:

```bash
./scripts/deploy.sh firmware-rollback <pi-ip> --slot A
```

Deploy configuration-only changes with:

```bash
./scripts/deploy.sh config <pi-ip>
```

`docker_build.sh` supports:

```bash
# Override parallel jobs (defaults to container nproc)
JOBS=8 ./scripts/docker_build.sh

# Reuse existing builder image without docker build
SKIP_IMAGE_BUILD=1 ./scripts/docker_build.sh

# Clear Docker build cache volume first, then build
./scripts/docker_build.sh --clear-docker-cache
```

## Key Files

### Boot (FAT partition)
| File | Source | Purpose |
|------|--------|---------|
| `config.txt` | `board/raspberrypi0w/config.txt` | Pi boot config |
| `cmdline.txt` | `board/raspberrypi0w/cmdline.txt` | Kernel parameters |
| `bootcode.bin` | rpi-firmware package | GPU bootloader |
| `start.elf` | rpi-firmware package | GPU firmware |
| `zImage` | kernel build | Linux kernel |
| `bcm2708-rpi-zero-w.dtb` | kernel build | Device tree |

### Root filesystem overlay

Shared (`board/common/rootfs_overlay`, applied first):

| Path | Purpose |
|------|---------|
| `etc/init.d/S40network` | Starts wpa_supplicant and DHCP |
| `etc/init.d/S41timesync` | Syncs system clock from NTP and keeps periodic resync running; `/etc/default/timesync` may set `CONFIG_FILE` and `LOGFILE` |
| `etc/init.d/S42otaguard` | Tracks pending OTA boot attempts and triggers rollback when exhausted |
| `etc/init.d/S45webd` | `chime-webd` supervisor; `/etc/default/chime-webd` may set `LOGFILE` and export `CHIME_WEBD_*` overrides |
| `etc/init.d/S50dropbear` | SSH daemon (key-only auth); host keys in `/data/etc/dropbear` survive OTA updates |
| `usr/local/sbin/ota-*` | OTA install, confirm, and rollback |
| `etc/wpa_supplicant/wpa_supplicant.conf` | WiFi credentials (gitignored) |
| `root/.ssh/authorized_keys` | SSH public keys (gitignored) |

Chime (`board/raspberrypi0w/rootfs_overlay`):

| Path | Purpose |
|------|---------|
| `etc/init.d/S30modules` | Decompresses WiFi modules, runs depmod, loads brcmfmac |
| `etc/init.d/S31persistent` | Bind-mounts `/data/etc` onto `/etc/persistent` and symlinks runtime config paths |
| `etc/init.d/S32config-migrate` | Rewrites `/etc/chime.conf` to the current schema before daemons start |
| `etc/init.d/S99chime` | Chime application with auto-restart and log rotation |
| `etc/init.d/S99otaconfirm` | Confirms pending OTA update when `chime` + `chime-webd` are healthy |
| `etc/chime-web/tls` | `chime-webd` self-signed TLS certificate and private key |
| `etc/inittab` | Getty on ttyS0 for serial console |

### Build configuration
| File | Purpose |
|------|---------|
| `configs/openchime_rpi0w_defconfig` | Chime Buildroot config |
| `configs/openchime_ring_rpi02w_defconfig` | Ring Buildroot config |
| `board/common/post_build_lib.sh` | Post-build helpers shared by both boards (OTA checks, release metadata) |
| `version.env` | OS/config version source of truth |
| `../chime/VERSION` | Chime app SemVer source of truth |
| `package/chime/chime.mk` | Chime CMake package recipe (source list lives in CMake) |
| `package/chime-web-ui/chime-web-ui.mk` | Installs locked Bun deps, builds the Svelte UI, and installs it into the rootfs |
| `bun-version` | Pinned Bun version for the builder image and CI |
| `bun-linux-x64.sha256` / `bun-linux-aarch64.sha256` | SHA-256 of the matching GitHub Bun zip |
| `board/common/assert_chime_web_ui_dist.sh` | Fails the image build if `index.html` or referenced assets are missing |
| `board/raspberrypi0w/genimage.cfg` | SD card image layout |
| `board/raspberrypi0w/post_build.sh` | Chime: creates firmware symlinks, runs depmod, asserts baked web UI |
| `board/raspberrypi0w/post_image.sh` | Copies boot files, generates sdcard.img |

## Critical Details

### Serial Console
- Pi Zero W uses **ttyS0** (mini UART) when `enable_uart=1`
- Baud rate: **115200**
- Getty configured in `/etc/inittab`

### config.txt (minimal working config)
```
kernel=zImage
enable_uart=1
dtoverlay=disable-bt
```
Do NOT add `gpu_mem` or `core_freq` - causes boot failure (4 blinks).

### WiFi Module Loading
Kernel modules are xz-compressed. On first boot, S30modules:
1. Decompresses: rfkill, cfg80211, brcmutil, brcmfmac, brcmfmac-wcc
2. Runs `depmod -a`
3. Runs `modprobe brcmfmac`

### Firmware Symlink
The brcmfmac driver requires device-specific firmware path:
```
/lib/firmware/brcm/brcmfmac43430-sdio.raspberrypi,model-zero-w.bin -> brcmfmac43430-sdio.bin
```
Created by `post_build.sh`.

### Baked web UI

`chime-webd` serves static files from `/usr/local/share/chime-web-ui/dist` at
`GET /` on port 8443. A clean image build must contain that tree; first boot
must not depend on `scripts/deploy.sh`.

`scripts/docker_build.sh` stages `webui/` (no host `dist/` or `node_modules/`)
into `/home/builder/webui-src` and writes `.source-id` from that tree. The
Buildroot package `chime-web-ui` uses `.source-id` as its package version, so a
source or `bun.lock` change rebuilds without `dirclean`. It runs
`bun install --frozen-lockfile`, then `bun run build`, and installs the bundle.
`post_build.sh` fails the image if `index.html` or a referenced asset is
missing.

Bun checks each package against the integrity hash in `bun.lock` and caches it
in `dl/chime-web-ui/bun-cache`. Only a completed `chime-web-ui` build fills that
cache; `make source` does not. After one online build, later builds that reuse
the same `dl/` directory (the Docker cache volume) install from the cache
without network access. To clear the cache, delete `dl/chime-web-ui/bun-cache`
or run `docker_build.sh --clear-docker-cache`.

The HTML fallback in `chime/src/webd/ui_assets.cpp` is a diagnostic for a missing
or broken dist directory on a running device. It is not an acceptable default for
a freshly flashed image.

`scripts/deploy.sh chime <pi-ip> --with-webd` can still replace the on-device
assets later without rebuilding the OS image. Chime-only binary rebuilds do not
stage or build the UI.

On-device paths:

| Path | Role |
|------|------|
| `/usr/local/share/chime-web-ui/dist` | Production Svelte bundle (`index.html` + hashed assets) |
| `/usr/local/bin/chime-webd` | HTTPS daemon that serves that dist at port 8443 |
| `/etc/chime-web/tls/cert.pem` | Self-signed TLS certificate |
| `/etc/chime-web/tls/key.pem` | TLS private key |

### wpa_supplicant.conf format
```
ctrl_interface=/var/run/wpa_supplicant
update_config=1
country=XX

network={
    ssid="SSID"
    psk="PASSWORD"
}
```
Do NOT include `ctrl_interface_group` - causes parse error.

## Debugging

### Boot failure (4 blinks)
- `start.elf` not found
- Check boot partition has all firmware files
- Simplify config.txt to just `kernel=zImage`

### No serial output
- Verify wiring: GPIO14(TX)->FTDI RX, GPIO15(RX)->FTDI TX, GND->GND
- Check `enable_uart=1` in config.txt
- Check `/etc/inittab` has getty on ttyS0

### WiFi not working
```bash
# Check modules loaded
lsmod | grep brcm

# Manual load sequence
modprobe brcmfmac

# Check interface
ip link show wlan0

# Manual WiFi connect
wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf
udhcpc -i wlan0
```

### SSH connection
```bash
# Find Pi on network
arp -a | grep b8:27:eb

# Connect
ssh root@<ip>
```

### Service logs, MQTT, OTA, and recovery

See [docs/reliability-runbook.md](../docs/reliability-runbook.md).

### Incorrect clock / 1970 timestamps
```bash
# Check current UTC time
date -u

# Check time sync service
/etc/init.d/S41timesync status
```

If `date` is still near 1970:
- Verify internet reachability.
- Check whether `ntpd` exists on the running image (`command -v ntpd`).
- Verify `/etc/chime.conf` time sync keys (`ntp_servers`, `time_http_urls`, `time_sync_retries`, `time_sync_interval`).
- On an already-flashed image without `ntpd`, the fallback methods (`rdate`/HTTP Date via `wget`) are used when available.

## Packages Included (Chime)

- kmod (modprobe, depmod)
- wpa_supplicant (WiFi)
- dropbear (SSH)
- udhcpc (DHCP)
- ntp/ntpd (clock synchronization, run by `S41timesync`)
- mosquitto client library and `mosquitto_pub`/`mosquitto_sub` (no broker)
- iw (WiFi debug)
- brcmfmac firmware (Pi Zero W WiFi)
- chime and chime-webd
- chime-web-ui (Svelte production bundle at `/usr/local/share/chime-web-ui/dist`)

## Ring image

`configs/openchime_ring_rpi02w_defconfig` builds the Ring doorbell image for the Raspberry Pi Zero 2 W (32-bit, Cortex-A53). It uses the same Buildroot release, kernel tag, toolchain, partition layout, OTA scripts, and shared overlay as Chime. Hardware and pin map: [hardware/ring](../hardware/ring/README.md).

```bash
./scripts/docker_build.sh --product ring
./scripts/flash_sd.sh --product ring /dev/diskN
ssh root@ring.local
```

Packages beyond the Chime base: libcamera with the rpi/vc4 pipeline and libcamera-apps, BlueZ (`bluetoothd`, `bluetoothctl`, `btmon`), libgpiod tools, and alsa-utils (`aplay`, `arecord`, `amixer`, `speaker-test`).

The build output goes to `buildroot/output/ring/` and to `output-ring/` in the Docker cache volume, so Chime and Ring builds do not overwrite each other.

### Differences from Chime

| Area | Ring | Chime |
|------|------|-------|
| Root filesystem | Read-only. Logs in RAM (`/var/log` is `/tmp`). | Remounted read-write at boot |
| Mutable state | `/data` only, through `/etc/persistent` bind mount and build-time symlinks | Config symlinks rewritten on rootfs at every boot; `/etc/dropbear` symlink set at build time |
| Kernel modules | `kmod` with xz support; `mdev` loads drivers by modalias | `S30modules` decompresses modules on first boot |
| Bluetooth | Enabled on the PL011 (`krnbt`), `bluetoothd`, `bluetoothctl`, `btmon` | Disabled |
| Serial console | `ttyS0` (mini-UART), 115200 | `ttyS0`, 115200 |
| Watchdog | BusyBox `watchdog` services `/dev/watchdog` every 5 s | None |
| Under-voltage | `S16powerlog` appends `rpi_volt` alarm changes to `/data/var/log/power.log` | None |

### Boot configuration

`board/raspberrypizero2w/config.txt` sets the boot GPIO state from the [Ring pin map](../hardware/ring/variants/rpi02w_cm3/README.md): buttons GPIO17/27/22/23 as inputs with pull-ups, relays GPIO5/6 as outputs driven high. It loads:

- `dtoverlay=imx708` for Camera Module 3 Wide (`camera_auto_detect=0`).
- `dtoverlay=googlevoicehat-soundcard` for bring-up audio: one duplex card, 48 kHz, S32_LE, two channels, MAX98357A playback on PCM_DOUT and SPH0645 capture on PCM_DIN. It drives GPIO16 as SD_MODE; the harness leaves GPIO16 unwired for this reason.
- `dtparam=i2c_arm=on` for the SHT40 on I2C1; `S11modules` loads `i2c-dev` from `/etc/modules`.

The production audio card has no SD_MODE GPIO. `post_image.sh` builds it from `board/raspberrypizero2w/openchime-ring-audio-overlay.dts` into `overlays/openchime-ring-audio.dtbo`: one `audio-graph-card2` card named `RingAudio` with the MAX98357A and ADAU7002 (SPH0645) codecs on the I2S link, Pi as bit and frame clock provider. It has not run on hardware. To test it, replace the `googlevoicehat-soundcard` line in `/boot/config.txt` with `dtoverlay=openchime-ring-audio` and reboot.

### Configuration web server

The Ring runs `chime-webd` on port 8443 for pairing, Wi-Fi, and MQTT until a Ring-specific daemon exists. `/etc/default/chime-webd` points it at Ring paths:

| Purpose | Path |
|---------|------|
| Config (Chime schema, factory copy in `/usr/local/share/openchime/factory/ring.conf`) | `/etc/ring.conf` → `/data/etc/ring.conf` |
| Wi-Fi | `/etc/wpa_supplicant/wpa_supplicant.conf` → `/data/etc/wpa_supplicant.conf` |
| TLS | `/data/etc/webd-tls/` |
| Auth, observed topics | `/data/var/lib/ring/` (mounted at `/var/lib/ring`) |
| Log | `/var/log/ring-web.log` |

Apply restarts networking only. The UI still shows the Chime page.

### Bring-up checks

```sh
cat /etc/openchime-release
mount | grep -E ' / | /data | /etc/persistent '
aplay -l && arecord -l                       # expect one card, 0
speaker-test -D hw:0 -c 2 -r 48000 -F S32_LE -t sine -l 1
arecord -D hw:0 -c 2 -r 48000 -f S32_LE -d 3 /tmp/mic.wav
libcamera-hello --list-cameras               # expect imx708_wide
libcamera-vid -t 5000 --width 1280 --height 720 -o /tmp/test.h264
i2cdetect -y 1                               # expect 0x44 (SHT40)
gpioget gpiochip0 17 27 22 23                # 1 when released
bluetoothctl show
cat /data/var/log/power.log
```

### Development on a read-only root

`scripts/deploy.sh chime` targets the Chime. On a Ring, either deploy a full rootfs through OTA:

```bash
./scripts/deploy.sh firmware ring.local --image buildroot/output/ring/rootfs.ext4 --wait-online
```

or remount the root read-write for a session and copy files by hand. The next reboot mounts it read-only again.

```sh
mount -o remount,rw /
```
