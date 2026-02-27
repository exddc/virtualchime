# Buildroot for Raspberry Pi Zero W

Lightweight Linux image for Pi Zero W with WiFi, SSH, and the chime application.

## Quick Start

```bash
# 1. Configure WiFi credentials (required)
cp board/raspberrypi0w/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf.example \
   board/raspberrypi0w/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf
# Edit with SSID/password

# 2. Add SSH key (required)
cat ~/.ssh/id_ed25519.pub > board/raspberrypi0w/rootfs_overlay/root/.ssh/authorized_keys

# 3. Build - this takes 30-90 minutes
./scripts/docker_build.sh

# 4. Flash - this takes 1-2 minutes
./scripts/flash_sd.sh /dev/diskN

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
VIRTUALCHIME_OS_VERSION=0.1.0
CHIME_CONFIG_VERSION=1
```

`chime/VERSION`
```bash
1.0.0
```

- `VIRTUALCHIME_OS_VERSION`: bump when you build/flash a new OS image.
- `CHIME_CONFIG_VERSION`: bump when default `chime.conf` semantics or format changes.
- `chime/VERSION`: bump when `chime` binary behavior changes.

During image builds, `post_build.sh` writes `/etc/virtualchime-release` on the target rootfs.
Read versions on-device with:

```bash
cat /etc/virtualchime-release
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

- Target hardware: Raspberry Pi Zero W.
- Raspberry Pi Zero 2 W may run this image but is not officially validated; test in your
  environment before production use.

Image layout sizing:

- Boot partition: `128M` (`board/raspberrypi0w/genimage.cfg`).
- Root filesystem image: `256M` (`configs/virtualchime_rpi0w_defconfig`).

That means the generated image is about 384 MB plus partition-table/alignment overhead.
Practical guidance:

- Absolute minimum card size: **1 GB**
- Recommended minimum for reliability/availability: **1 GB**

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

For app-only changes (`chime/` or `common/`), use:

```bash
./scripts/deploy.sh chime <pi-ip>
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
| Path | Purpose |
|------|---------|
| `etc/init.d/S30modules` | Decompresses WiFi modules, runs depmod, loads brcmfmac |
| `etc/init.d/S40network` | Starts wpa_supplicant and DHCP |
| `etc/init.d/S45webd` | Chime HTTPS web daemon supervisor (`chime-webd`) |
| `etc/init.d/S41timesync` | Syncs system clock from NTP and keeps periodic resync running |
| `etc/init.d/S50dropbear` | SSH daemon (key-only auth) |
| `etc/init.d/S99chime` | Chime application with auto-restart and log rotation |
| `etc/wpa_supplicant/wpa_supplicant.conf` | WiFi credentials (gitignored) |
| `etc/chime-web/tls` | `chime-webd` self-signed TLS certificate and private key |
| `etc/inittab` | Getty on ttyS0 for serial console |
| `root/.ssh/authorized_keys` | SSH public keys (gitignored) |

### Build configuration
| File | Purpose |
|------|---------|
| `configs/virtualchime_rpi0w_defconfig` | Main Buildroot config |
| `version.env` | OS/config version source of truth |
| `../chime/VERSION` | Chime app SemVer source of truth |
| `package/chime/chime.mk` | Chime package recipe |
| `board/raspberrypi0w/genimage.cfg` | SD card image layout |
| `board/raspberrypi0w/post_build.sh` | Creates firmware symlinks, runs depmod |
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

### Chime reliability logs
```bash
# Follow daemon logs
tail -f /var/log/chime.log

# Or from host
ssh root@<pi-ip> 'tail -f /var/log/chime.log'

# Rotated files
ls -lh /var/log/chime.log*
```

Log stream includes service start/stop, MQTT connect/disconnect/reconnect,
message receipt details, heartbeat publish status, WiFi link-state transitions,
and periodic health counters.

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

## Packages Included

- kmod (modprobe, depmod)
- wpa_supplicant (WiFi)
- dropbear (SSH)
- dhcpcd, udhcpc (DHCP)
- ntp/ntpd (clock synchronization)
- iw (WiFi debug)
- brcmfmac firmware (Pi Zero W WiFi)
