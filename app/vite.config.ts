import { defineConfig } from "vite";

// Tauri(데스크톱)에서도 같은 빌드를 쓰므로 상대 경로로 빌드한다.
export default defineConfig({
  base: "./",
  clearScreen: false,
  server: { port: 5173, strictPort: true },
  build: { target: "es2022" },
});
