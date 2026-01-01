import { defineConfig } from 'vite';

export default defineConfig({
    root: '.',
    base: './',
    build: {
        outDir: 'dist',
        sourcemap: true,
        target: 'esnext',
    },
    server: {
        port: 3000,
        open: true,
    },
});
