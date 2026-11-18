# PortScanner 🔍

[🇬🇧 English](#-english) · [🇷🇺 Русский](#-русский)

---

## 🇬🇧 English

### Fast multithreaded TCP/UDP port scanner & OS detector for Windows


A single-file port scanner written in C++17 with zero external dependencies. Uses an **asynchronous `connect()` engine with `WSAPoll()` batching** to scan thousands of ports per second. Includes passive OS fingerprinting based on open ports and service banners.

### ✨ Features

- ⚡ **Async connect() engine** — hundreds of parallel non-blocking sockets per thread via `WSAPoll()`
- 🚀 **Blazing fast** — full 65535-port scan in ~2–10 s on LAN
- 🌐 **TCP scan** — classic connect() scan with batching (default range 1–1024)
- 📡 **UDP scan** — sends probes, detects open / closed / `open|filtered`
- 🖥️ **OS detection** — passive fingerprinting via:
  - open-port heuristics (135/139/445/3389 → Windows, 22/111 → Linux, 548 → macOS)
  - banner grabbing (SSH, HTTP, FTP, SMTP)
  - HTTP `Server:` header (IIS / nginx / Apache)
- 🏷️ **Service names** — well-known ports labeled (`80 → http`, `3389 → rdp`, …)
- 🧵 **Multithreaded** — up to 128 worker threads, auto-tuned to CPU count
- 📦 **Single file, zero deps** — only links `ws2_32.lib` (already in Windows)
- 🛠️ **Cross-compiler** — builds with MinGW (g++) and MSVC (cl)
