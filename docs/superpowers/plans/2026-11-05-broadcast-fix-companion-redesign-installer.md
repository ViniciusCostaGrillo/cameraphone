# Implementation Plan: NVIDIA Broadcast Fix, Companion Redesign & Windows Installer

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix NVIDIA Broadcast virtual camera stream freeze, redesign Companion UI to match icon aesthetics with only top buttons and logs, enable cable-only direct Broadcast streaming, fix language button label, and create a complete Windows Installer with auto-start on boot.

**Architecture:** 
- Fix COM DirectShow IMemInputPin/IMemAllocator GUIDs and commit lifecycle in `vcam/cameraphone-vcam.cpp`.
- Embed USB background video reader inside `CameraPhone.exe` that feeds `vcam_shmem` directly without requiring OBS.
- Restyle Win32 UI in `CameraPhone.exe` to a sleek dark theme (#12161A / #00E5FF) with top buttons [Instalar Tudo], [Instalar APK], [Testar Conexão], [Idioma/Language] and expansive logs below.
- Create automated Windows Setup (`CameraPhone-Setup.exe`) configuring Run key, Desktop/Start Menu shortcuts, OBS plugin, and DirectShow registration.

**Tech Stack:** C++20, Win32 API, DirectShow COM, Windows Registry, LLVM-MinGW, FFmpeg, Android ADB.

---

## Global Constraints
- Target Windows 10/11 64-bit.
- Statically linked binaries (`-static`, `-static-libgcc`, `-static-libstdc++`) with zero MSVC runtime dependencies.
- Language toggle must strictly display `Idioma` (in PT) or `Language` (in EN).
- Zero paywalls, zero watermarks, 100% clean-room open-source.

---

## Tasks

### Task 1: Fix DirectShow Virtual Camera COM Pipeline for NVIDIA Broadcast
**Files:**
- Modify: `vcam/cameraphone-vcam.cpp`
- Test: `tests/test_vcam_graph_run.cpp`

- [ ] Update `IID_IMemInputPin_Local` to `{ 0x56a8689d, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } }`.
- [ ] Update `IID_IMemAllocator_Local` to `{ 0x56a8689c, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } }`.
- [ ] Update `CLSID_MemoryAllocator_Local` to `{ 0x1e651cc0, 0xb199, 0x11d0, { 0x82, 0x12, 0x00, 0xc0, 0x4f, 0xc3, 0x2c, 0x45 } }`.
- [ ] Implement `m_pAllocator->Commit()` on filter `Pause()` and `Run()`.
- [ ] Implement `m_pAllocator->Decommit()` on filter `Stop()`.
- [ ] Build test `tests/test_vcam_graph_run.cpp` connecting a null-renderer / sample-grabber to `cameraphone-vcam.dll`, run filter graph for 2 seconds, and verify frames are delivered continuously.
- [ ] Compile `dist/cameraphone-vcam.dll` and register with `regsvr32.exe /s`.

### Task 2: Background USB Feeder for NVIDIA Broadcast in Companion
**Files:**
- Modify: `desktop-app/cameraphone_companion.cpp`
- Modify: `protocol/vcam_shmem.hpp`

- [ ] Implement `UsbBroadcastService` background thread in `cameraphone_companion.cpp` that detects ADB device, ensures `forward tcp:4748 tcp:4748`, and connects to `127.0.0.1:4748` via `StreamClient`.
- [ ] Connect video packets to `MediaDecoder` and push NV12 frames to `VCamSharedMemoryWriter`.
- [ ] Enable running seamlessly in background (via `--background` argument or minimized to system tray).

### Task 3: Redesign Companion UI & Fix Language Button
**Files:**
- Modify: `desktop-app/cameraphone_companion.cpp`

- [ ] Remove Host and PIN edit fields.
- [ ] Top action row: [1. Instalar Tudo], [2. Instalar APK], [3. Testar Conexão], [Idioma / Language].
- [ ] Fix language button label to display strictly `Idioma` (in PT) and `Language` (in EN).
- [ ] Apply dark modern system styling matching `cameraphone_logo.png` (#12161A background, #1C232B container, #00E5FF cyan focus, dark custom edit log).
- [ ] Recompile `dist/CameraPhone.exe` and test UI.

### Task 4: Build Windows Installer (`CameraPhone-Setup.exe`)
**Files:**
- Create: `installer/installer_main.cpp`
- Create: `installer/build_installer.py`
- Modify: `install.ps1`

- [ ] Build standalone `CameraPhone-Setup.exe` with embedded assets (or payload unpacker).
- [ ] Auto-installs to `C:\Program Files\CameraPhone\` and `C:\Program Files\obs-studio\obs-plugins\64bit\`.
- [ ] Adds `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` key `CameraPhone` = `"C:\Program Files\CameraPhone\CameraPhone.exe" --background`.
- [ ] Registers DirectShow Virtual Camera DLL.
- [ ] Creates Desktop and Start Menu shortcuts.

### Task 5: End-to-End Verification, Release v1.3.0 & Documentation
**Files:**
- Modify: `README.md`
- Modify: `KNOWLEDGE.md`
- Script: `tools/create_and_upload_release.py`

- [ ] Run full test suite with `tools/verify_artifacts_runtime.py`.
- [ ] Commit all changes with conventional commit.
- [ ] Tag `v1.3.0` and publish GitHub release with `CameraPhone-Setup.exe` and artifacts.
