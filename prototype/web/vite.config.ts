import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: { middlewareMode: true, watch: { usePolling: true, interval: 500 } },
  build: { chunkSizeWarningLimit: 900, rollupOptions: { output: { manualChunks: { avatar: ['pixi.js', 'pixi-live2d-display/cubism4'] } } } },
});
