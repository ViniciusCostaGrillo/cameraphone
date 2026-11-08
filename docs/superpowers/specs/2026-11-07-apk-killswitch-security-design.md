# CameraPhone APK Security Kill-Switch (Connect / Disconnect) — Design Spec

- **Date:** 2026-11-07
- **Status:** Draft (Pending User Approval)
- **Scope:** Small / Focused Security Feature (`android-app`)

## 1. Intent & Why
Provide the user with an immediate, hardware-enforced **Disconnect / Connect** ("Desconectar / Conectar") security kill-switch in the Android APK so the phone does not remain connected and streaming 100% of the time while the PC companion app (`CameraPhone.exe`) is running.

## 2. Goals, Non-Goals & Anti-Goals
- **Goals:**
  - Start **Connected (Armed)** by default when the Android app opens, showing a prominent red **`🛑 DISCONNECT (CUT CAMERA & NETWORK)`** (`🛑 DESCONECTAR (CORTAR CÂMERA E REDE)`) button.
  - When **Disconnect** is tapped, enforce a **Total Network & Hardware Blackout**:
    1. Set `SecurityGate.connectionEnabled = false` (`AuthResult.Disconnected`), immediately rejecting any in-flight requests at the authentication choke point.
    2. Force-close the active TCP streaming socket (`activeStreamSocket.close()`) and close `ServerSocket(4748)`, UDP beacon `4749`, and mDNS registration (`_cameraphone._tcp`), leaving **zero open ports** on the phone.
    3. Immediately stop `Camera2Controller`, `HardwareVideoEncoder`, and `HardwareAudioEncoder` (turning off Android's green camera/microphone privacy dot) and release `WifiLock` / `WakeLock`.
  - When **Connect** (`🔌 CONNECT (ALLOW PC CONNECTION)` / `🔌 CONECTAR (LIBERAR CONEXÃO COM O PC)`) is tapped, re-enable `SecurityGate`, re-acquire `WifiLock` / `WakeLock`, and restart `ServerSocket(4748)`, UDP beacon `4749`, and mDNS so the PC reconnects automatically within ~1 second.
  - Support full bilingual switching (`EN` default + `PT` toggle).
- **Non-Goals:**
  - Changing the desktop companion protocol or requiring manual action on the PC when reconnecting.
- **Anti-Goals:**
  - Superficial UI-only pause that leaves TCP `4748`, UDP `4749`, or the Android camera/microphone hardware session open in the background.

## 3. Constraints
- Must be 100% compatible with Android 9–16 (SDK 28–36, Samsung OneUI) without calling `stopSelf()` on the `ForegroundService` (which triggers background service start restrictions on Android 14+).
- Must preserve thread safety in `CamPhoneServer` and `SecurityGate` under concurrent client access.

## 4. Approaches Considered
1. **Approach A (Selected — 3-Layer Defense-in-Depth Kill-Switch):** Enforce disconnect across `SecurityGate` (`AuthResult.Disconnected`), `CamPhoneServer` (`activeStreamSocket.close()` + `ServerSocket.close()` + UDP/mDNS teardown), and `CameraStreamService` (camera/mic hardware teardown + `WifiLock`/`WakeLock` release).
2. **Approach B (Rejected — HTTP 403 Only):** Leaves ports `4748`/`4749` open on the LAN, violating the Total Network Blackout requirement.
3. **Approach C (Rejected — `Service.stopSelf()`):** Causes Android 14+ foreground service restart restrictions and loses session log state.

## 5. Architecture & Component Changes
- **[`SecurityGate.kt`](file:///c:/Cameraphone%20Plugin%20OBS/android-app/app/src/main/java/com/cameraphone/app/security/SecurityGate.kt):**
  - Add `AuthResult.Disconnected` to `AuthResult`.
  - Add `@Volatile var connectionEnabled: Boolean = true`.
  - At the very top of `verifyRequest()`, if `!connectionEnabled`, return `AuthResult.Disconnected` immediately.
- **[`CamPhoneServer.kt`](file:///c:/Cameraphone%20Plugin%20OBS/android-app/app/src/main/java/com/cameraphone/app/net/CamPhoneServer.kt):**
  - Track `@Volatile private var activeStreamSocket: Socket? = null`.
  - Handle `AuthResult.Disconnected` by returning `HTTP 503 Service Unavailable` (`{"error":"disconnected_by_user"}`) and closing the socket.
  - In `stop()`, also close `activeStreamSocket` and `activeStreamOutput` inside `synchronized(writeLock)` so any active stream is severed immediately.
- **[`CameraStreamService.kt`](file:///c:/Cameraphone%20Plugin%20OBS/android-app/app/src/main/java/com/cameraphone/app/service/CameraStreamService.kt):**
  - Add `@Volatile var isConnectionArmed: Boolean = true` (default `true` on startup).
  - Add `setConnectionArmed(armed: Boolean)`:
    - When `armed = false`: sets `isConnectionArmed = false`, `securityGate.connectionEnabled = false`, calls `stopHardwarePipeline()`, `stopUdpBeacon()`, `unregisterMdns()`, `server?.stop()`, `server = null`, releases `wifiLock` and `wakeLock`, updates foreground notification to `"Disconnected (Camera & Network Locked)"`, and notifies UI (`tally = "disconnected"`, `streaming = false`).
    - When `armed = true`: sets `isConnectionArmed = true`, `securityGate.connectionEnabled = true`, acquires `wifiLock` and `wakeLock`, calls `restartServer(usbOnly = usbOnlyMode)`, updates foreground notification, and notifies UI (`tally = "idle"`, `streaming = false`).
- **[`MainActivity.kt`](file:///c:/Cameraphone%20Plugin%20OBS/android-app/app/src/main/java/com/cameraphone/app/ui/MainActivity.kt):**
  - Add `connectToggleBtn: Button` right below the status banner.
  - Toggle between red **`🛑 DISCONNECT (CUT CAMERA & NETWORK)`** (`🛑 DESCONECTAR (CORTAR CÂMERA E REDE)`) when armed and green **`🔌 CONNECT (ALLOW PC CONNECTION)`** (`🔌 CONECTAR (LIBERAR CONEXÃO COM O PC)`) when disarmed.
  - Update `tallyBanner` and `ipInfoText` to clearly display **`🔒 DISCONNECTED • NETWORK & CAMERA LOCKED`** (`🔒 DESCONECTADO • REDE E CÂMERA BLOQUEADAS`) when disarmed.
- **[`SecurityGateTest.kt`](file:///c:/Cameraphone%20Plugin%20OBS/android-app/app/src/test/java/com/cameraphone/app/security/SecurityGateTest.kt):**
  - Add JUnit4 unit & integration tests verifying that setting `connectionEnabled = false` rejects requests with `AuthResult.Disconnected` and that stopping `CamPhoneServer` immediately terminates an active stream socket.
