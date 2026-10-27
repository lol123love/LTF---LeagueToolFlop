# 💖 LeagueToolFlop (`LTKBridge`) — Educational Research & Automation Suite

> *"Made by **lol123love** for educational purposes — and because love loves LTK, RuneForge, and their big FLOP!"*

---

## ⚠️ Educational Disclaimer

This project (`LeagueToolFlop` / `LTKBridge`) was created **strictly for educational, interoperability, and reverse-engineering research purposes**. It serves as a technical case study demonstrating:
- How **Tauri + WebView2** desktop applications expose internal IPC bridges (`window.__TAURI_INTERNALS__.invoke`) when launched with Chromium DevTools Protocol (`CDP`) flags.
- How **League of Legends `.fantome` and `.wad.client` (v3.4)** archives and **PropertyBin (`PROP` / `PTCH`)** files are structured, hashed (`FNV1a32` & `XXH64`), and parsed in memory using modern C++20.
- How client-side heuristic and health-verdict checks operate and why relying on unauthenticated frontend IPC or trivial metadata checks is a massive architectural **FLOP**.

---

## 💌 A Love Letter to LTK & RuneForge (and Their Big FLOP!)

First and foremost: **we love LTK (LeagueToolkit) and RuneForge!** Truly! Huge shoutout to the developers for building a shiny Tauri wrapper and trying to gatekeep which mods are "healthy" or "allowed" to load. 

Nothing says *"peak security engineering"* quite like:
1. Checking `championSkinName`, `tagEventList`, and `BankUnit.name` strings inside `skin0.bin` as a "heuristic".
2. Checking whether `.skl` / `.skn` files match a static hash list—which folds immediately the moment you append 16 harmless dead zero-bytes (`0x00`) to the end of the file.
3. Trusting `plugin:library|get_mod_health_verdicts` inside an unencrypted WebView2 frontend where a 10-line JavaScript shim makes every mod in the universe 100% `"healthy"`.

Thank you, **LTK & RuneForge**, for this **legendary FLOP**—it made for a fun afternoon of C++20 coding! 🏆

---

## 🛠️ Architecture & How It Works

The project consists of three components that work together seamlessly:

```text
┌──────────────────────┐       TCP JSON API        ┌───────────────────────────────────────────┐
│   ltk_client.py      │ ◄───────────────────────► │          LeagueToolFlop.dll               │
│  (CLI / REPL / SDK)  │    127.0.0.1:46321..40    │    (Injected into ltk-manager.exe)        │
└──────────────────────┘                           ├───────────────────────────────────────────┤
                                                   │ 1. Auto-Spoofs Health Verdicts (CDP 9222) │
                                                   │ 2. In-Memory Fantome & WAD v3 Processor   │
                                                   │ 3. Ritobin AST Patcher (skin0.bin)        │
                                                   │ 4. Original WAD Overlap SKL/SKN Puller    │
                                                   └───────────────────────────────────────────┘
