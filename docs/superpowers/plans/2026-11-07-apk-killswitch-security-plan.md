# CameraPhone APK Security Kill-Switch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans and superpowers:test-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bilingual (`EN` / `PT`) **Disconnect / Connect** security kill-switch button to the Android APK that enforces a 3-layer Total Network & Hardware Blackout (`SecurityGate` choke point + active TCP socket/server/UDP/mDNS teardown + Camera/Mic/WifiLock hardware release).

**Architecture:** Defense-in-depth across `SecurityGate` (`connectionEnabled` atomic gate returning `AuthResult.Disconnected`), `CamPhoneServer` (tracking `activeStreamSocket` and closing it immediately inside `stop()`), `CameraStreamService` (`setConnectionArmed(armed: Boolean)` tearing down or restoring network + hardware + power locks), and `MainActivity` (prominent red/green toggle button with bilingual labels).

**Tech Stack:** Kotlin (Android SDK 28–36), JUnit4, Java Sockets, Camera2 / MediaCodec.

**Spec:** `docs/superpowers/specs/2026-11-07-apk-killswitch-security-design.md`

## Global Constraints
- Default state on app launch: Connected / Armed (`isConnectionArmed = true`, `connectionEnabled = true`).
- Default UI language: English (`EN`) with `🌐 Language: EN / PT` toggle support.
- Disconnecting must close active stream TCP socket, `ServerSocket(4748)`, UDP `4749` beacon, mDNS, Camera/Mic encoders, and `WifiLock`/`WakeLock`.

## Review Focus
- Disconnecting while an active `/v1/stream` client is connected must immediately close the client's TCP socket (`read()` returns `-1` or throws `SocketException`) and invoke `onStopStream()`.
- Requests arriving while `connectionEnabled == false` must be rejected with `AuthResult.Disconnected` (`HTTP 503`) even if they carry a valid 6-digit PIN or `Bearer usb-zero-config`.
- Re-enabling (`setConnectionArmed(true)`) must restore `connectionEnabled = true` and allow immediate reconnection without lockout side effects.

---

### Task 1: `SecurityGate` & `CamPhoneServer` Kill-Switch + JUnit4 TDD Tests

**Files:**
- Modify: `android-app/app/src/test/java/com/cameraphone/app/security/SecurityGateTest.kt`
- Modify: `android-app/app/src/main/java/com/cameraphone/app/security/SecurityGate.kt`
- Modify: `android-app/app/src/main/java/com/cameraphone/app/net/CamPhoneServer.kt`

- [x] **Step 1: Write failing tests in `SecurityGateTest.kt`** for `connectionEnabled = false` (`AuthResult.Disconnected`) and immediate active stream socket termination on `server.stop()`.
- [x] **Step 2: Run `python android-app/build_and_test.py test` to verify RED.**
- [x] **Step 3: Implement `AuthResult.Disconnected`, `connectionEnabled` in `SecurityGate.kt`, and `activeStreamSocket` teardown in `CamPhoneServer.kt`.**
- [x] **Step 4: Run `python android-app/build_and_test.py test` to verify GREEN.**

### Task 2: `CameraStreamService` & `MainActivity` Bilingual Kill-Switch UI

**Files:**
- Modify: `android-app/app/src/main/java/com/cameraphone/app/service/CameraStreamService.kt`
- Modify: `android-app/app/src/main/java/com/cameraphone/app/ui/MainActivity.kt`

- [x] **Step 1: Implement `isConnectionArmed` and `setConnectionArmed(armed: Boolean)` in `CameraStreamService.kt`.**
- [x] **Step 2: Add `connectToggleBtn` (`🛑 DISCONNECT (CUT CAMERA & NETWORK)` / `🔌 CONNECT (ALLOW PC CONNECTION)`) and bilingual state updates in `MainActivity.kt`.**
- [x] **Step 3: Build APK, installer, and run `python tools/verify_artifacts_runtime.py`.**
