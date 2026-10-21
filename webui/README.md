# Web UI (Svelte)

This directory contains the Svelte + Vite frontend for `chime-webd`.

## Tooling

This project uses **Bun as the default package manager/runtime** for local frontend tasks.

Install dependencies:

```bash
cd webui
bun install
```

Run linting (Biome):

```bash
bun run lint
```

## Local Development

From the repository root:

```bash
./scripts/local_chime.sh webui-dev
```

In a second terminal, run the backend on port 8443 (or set `CHIME_WEBD_PORT`):

```bash
./scripts/local_chime.sh run-webd
```

Vite proxies `/api/*` to `https://127.0.0.1:8443` (TLS verification disabled for local self-signed certs). The proxy strips the `Secure` flag from `Set-Cookie` so the HTTP Vite origin can store the session cookie. Production and `./scripts/local_chime.sh run-webd` keep `Secure` cookies.

Local `chime-webd` is pre-paired. Sign in with password `openchime-local` unless you set `CHIME_WEBD_BOOTSTRAP_PASSWORD`.

To exercise first-boot pairing locally:

```bash
rm -rf chime/build-local/runtime/auth
LOCAL_CHIME_UNPAIRED=1 ./scripts/local_chime.sh run-webd
```

Add `CHIME_WEBD_PAIRING_CODE=ABCD2345` if you want a fixed pairing code instead of the one printed on stdout.

## Production Build

Locally:

```bash
./scripts/local_chime.sh webui-build
```

This writes static assets to `webui/dist/`. That directory is gitignored and is **not** consumed by the OS image build.

When `CHIME_WEBD_UI_DIST_DIR` points at that directory, a locally run `chime-webd` serves the built UI instead of the embedded fallback HTML.

Image builds bake this UI into `/usr/local/share/chime-web-ui/dist`. See [Baked web UI](../buildroot/README.md#baked-web-ui) for the package, dependency cache, and on-device paths.

To update UI assets on a device that is already flashed, without rebuilding the OS image:

```bash
./scripts/local_chime.sh webui-build
./scripts/deploy.sh chime <pi-ip> --with-webd
```

## Styling

Tailwind CSS is configured via the Vite plugin and imported in `src/app.css`.
