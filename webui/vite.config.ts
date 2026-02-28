import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import tailwindcss from "@tailwindcss/vite";

export default defineConfig({
  plugins: [svelte(), tailwindcss()],
  server: {
    proxy: {
      "/api": {
        target: "https://127.0.0.1:8443",
        changeOrigin: true,
        secure: false,
      },
    },
  },
  build: {
    target: "es2018",
    sourcemap: false,
    minify: "esbuild",
    cssCodeSplit: false,
  },
});
