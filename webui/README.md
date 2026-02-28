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

Vite proxies `/api/*` to `https://127.0.0.1:8443` (TLS verification disabled for local self-signed certs).

## Production Build

```bash
./scripts/local_chime.sh webui-build
```

This writes static assets to `webui/dist/`.

When `CHIME_WEBD_UI_DIST_DIR` points at that directory, `chime-webd` serves the built UI instead of the embedded fallback HTML.

## Styling

Tailwind CSS is configured via the Vite plugin and imported in `src/app.css`.
