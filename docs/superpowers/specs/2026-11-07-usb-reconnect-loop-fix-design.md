# Design Spec: USB Reconnect Loop Fix & OBS/Companion Coexistence

**Date:** 2026-11-07
**Status:** Approved

## 1. Problem Statement & Root Causes
Logs from the PC (`%APPDATA%\CameraPhone\cameraphone.log`) and Android APK (`/v1/logs` on `samsung SM-A556E`) showed a continuous ~1.85-second connect/disconnect cycle:
1. **Race Condition in `UsbBroadcastFeeder::run()` (`desktop-app/cameraphone_companion.cpp`):**
   - `m_client.start_streaming(...)` starts a background network thread asynchronously (`is_connected()` starts as `false` until the TCP + HTTP `200 OK` handshake completes).
   - Immediately on the next line, `while (m_running.load() && m_client.is_connected())` evaluated `is_connected() == false`, exited in 0ms, and called `m_client.stop_streaming()`, killing the socket while the phone was still opening Camera2 and MediaCodec.
2. **Stream Contention Between Companion (`CameraPhone.exe`) and OBS (`cameraphone.dll`) / Duplicate Companion Instances:**
   - If two instances of `CameraPhone.exe` run (e.g., `--background` tray instance + foreground instance), or if both `CameraPhone.exe` and OBS Studio connect to `/v1/stream` simultaneously, they kick each other off in a loop.
3. **Stream Ownership Check in `CamPhoneServer.kt`:**
   - In `CamPhoneServer.kt`, when a second `/v1/stream` request replaced the first, the first handler's `finally` block checked `if (ownsActiveStream && activeStreamOutput != null)` instead of `activeStreamOutput === out`, causing the old connection's cleanup to stop the newly started stream.

## 2. Goals & Non-Goals
- **Goals:**
  - Eliminate the premature self-disconnect in `UsbBroadcastFeeder`.
  - Ensure single-instance execution of `CameraPhone.exe` via a named Windows mutex (`Local\CameraPhone_Companion_Mutex`), bringing the existing window to front when launched again.
  - Coordinate stream ownership between OBS Studio (`cameraphone.dll`) and `CameraPhone.exe` (`UsbBroadcastFeeder`) so `UsbBroadcastFeeder` yields whenever OBS is actively streaming (since OBS already feeds `VCamSharedMemoryWriter`).
  - Fix `CamPhoneServer.kt` `finally` block so an evicted connection never stops the replacement stream.
- **Non-Goals:**
  - Running dual simultaneous hardware encoders on Android.

## 3. Architecture & Changes
1. **`protocol/vcam_shmem.hpp`:**
   - Add `static bool is_producer_active(uint64_t max_age_ms = 1500)` or a named mutex/event helper `Local\CameraPhone_OBS_Active` so `UsbBroadcastFeeder` can check if OBS Studio is actively streaming before opening its own `/v1/stream` connection, and yield immediately if OBS starts.
2. **`obs-plugin/src/stream-client.hpp` & `obs-plugin/src/stream-client.cpp`:**
   - Add `bool is_running() const noexcept { return running_.load(); }`.
   - Avoid redundant `DiscoveryManager::ensure_usb_tunnel_ready` calls when the socket was intentionally closed by `stop_streaming()` (`!running_.load()`).
3. **`desktop-app/cameraphone_companion.cpp`:**
   - Wait for initial connection (up to 5s grace window while `m_client.is_running()`) or rely on `m_client.is_running()` while monitoring `is_connected()` after handshake, and yield whenever OBS holds `Local\CameraPhone_OBS_Active`.
   - Enforce single-instance mutex `Local\CameraPhone_Companion_Mutex` in `main()` (for interactive / `--background` runs), signaling/showing the existing `CameraPhoneCompanionWnd` window if already running.
4. **`android-app/app/src/main/java/com/cameraphone/app/net/CamPhoneServer.kt`:**
   - In `finally`, check `if (ownsActiveStream && activeStreamOutput === out)` inside `synchronized(writeLock)` before clearing `activeStreamOutput` and calling `onStopStream()`.
