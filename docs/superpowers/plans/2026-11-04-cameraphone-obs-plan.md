# Cameraphone Plugin OBS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a zero-trust, crash-proof, 4K/60+FPS/HDR smartphone-to-OBS Studio camera system (`CamPhoneWire v1` protocol, Android Kotlin `Camera2`/`MediaCodec` app with minimalist OLED UI, and Windows C++20 `libobs`/`FFmpeg` plugin) supporting USB (ADB loopback) and Wi-Fi (mDNS) with automatic hardware capability detection.

**Architecture:** Clean/Hexagonal modular architecture split into three self-contained layers: (1) Shared 16-byte Big-Endian binary framing and Zero-Trust authentication protocol (`protocol/`), (2) Native Android app (`android-app/`) using `Camera2` zero-copy `Surface` into hardware `MediaCodec` (`HEVC`/`AVC`/`AV1` + `AAC`) served over a rate-limited, PIN/HMAC-authenticated TCP server bound to `127.0.0.1:4748` (USB mode) or LAN (`0.0.0.0:4748` + mDNS `_cameraphoneobs._tcp.local.`), and (3) Native 64-bit OBS Studio plugin (`obs-plugin/`) with non-blocking sockets, bounded ring buffers, FFmpeg hardware-accelerated decoding, dynamic capability dropdowns, and real-time Pro camera controls.

**Tech Stack:** C++20, CMake 3.22+, `libobs` / `obs-frontend-api` ABI, FFmpeg (`avcodec-62`, `avutil-60`), Winsock2 (`ws2_32`), Kotlin 1.9+, Android SDK 34 (`minSdk 26`), Android `Camera2` + `MediaCodec` + `NsdManager`, Gradle 8.7, Python 3.14 (host test runner & E2E verifier).

**Spec:** `docs/superpowers/specs/2026-11-04-cameraphone-obs-design.md`

## Global Constraints

- **Default TCP Port:** `4748` (`CAMPHONE_DEFAULT_PORT = 4748`).
- **mDNS Service Type:** `"_cameraphoneobs._tcp."` (`_cameraphoneobs._tcp.local.`).
- **Binary Header Format (`CamPhonePacketHeader`):** 16 bytes Big-Endian: `magic = 0x4350` (`uint16_t`), `stream_type` (`uint8_t`: `1`=Video, `2`=Audio, `3`=Telemetry, `4`=Heartbeat), `flags` (`uint8_t`: `0x01`=CodecConfig, `0x02`=Keyframe, `0x04`=HDR_HLG, `0x08`=HDR_PQ), `payload_length` (`uint32_t`, max `8388608` bytes = 8 MiB), `pts_ns` (`uint64_t` nanoseconds).
- **Zero-Trust Security Floor:**
  - Every endpoint except `GET /api/v1/ping` requires header `X-CamPhone-Token: <6-digit-pin>` validated in constant time.
  - USB-Only mode binds strictly to `127.0.0.1:4748` (`InetAddress.getLoopbackAddress()`).
  - Brute-force protection locks out any client IP after `5` failed attempts within `60` seconds (`HTTP 429 Too Many Requests`).
  - Only `1` streaming client is permitted concurrently (`HTTP 409 Conflict` if already streaming).
- **Zero-Freeze Stability Floor:**
  - Socket timeouts on both desktop and Android: `2000 ms` read/write (`SO_RCVTIMEO` / `SO_SNDTIMEO`), `TCP_NODELAY = 1`.
  - Video ring buffer capacity capped at `6` frames; audio ring buffer capped at `16` frames (oldest frame dropped on overflow, never blocking the network or OBS UI thread).

## Review Focus

1. **Corrupted or Malicious Packet Header Overflows (`payload_length > 8 MiB` or `magic != 0x4350`):** Parser must immediately reject headers where `magic != 0x4350`, `stream_type` not in `1..4`, or `payload_length > 8388608` without allocating memory (pinned in Task 1 & Task 3).
2. **Unauthorized LAN Spying / Brute-Force PIN Guessing:** Server must reject missing/invalid `X-CamPhone-Token` with `HTTP 401`, use constant-time byte comparison, and return `HTTP 429` on the 6th failed attempt from the same IP (pinned in Task 2 & Task 5).
3. **Out-of-Range Camera Control Injection (`PATCH /api/v1/control`):** Malicious or buggy JSON values (e.g., `iso: -999999`, `zoom: 999.0`, `shutter_ns: 0`, `camera_id: "../../etc"`) must be sanitized against allowlists and clamped to the hardware's `CameraCharacteristics` ranges without throwing unhandled exceptions (pinned in Task 2 & Task 4).
4. **Abrupt USB Cable Disconnection Mid-Frame (Truncated TCP Stream):** When a socket closes after sending 16 header bytes and only half of the payload, `StreamClient` must time out within `2000 ms`, discard the partial packet, transition cleanly to `Reconnecting` state, and never block the OBS UI thread (pinned in Task 6).
5. **Ring Buffer Saturation Under Decoder Stall:** When the consumer lags behind the producer, `BoundedFrameQueue` must evict the oldest non-config frame (preserving `FLAG_CODEC_CONFIG` packets so the decoder always receives `SPS/PPS/VPS`) and keep memory bounded to `<= 6` video frames (pinned in Task 1 & Task 6).

---

### Task 1: Shared Wire Protocol, Constant-Time Security Primitives & Bounded Frame Queue (`protocol/`)

**Files:**
- Create: `protocol/camphone_protocol.hpp`
- Create: `protocol/test_protocol.cpp`
- Create: `protocol/run_tests.py`

**Interfaces:**
- Consumes: Standard C++20 library (`<cstdint>`, `<vector>`, `<string>`, `<optional>`, `<deque>`, `<mutex>`).
- Produces:
  - `namespace camphone`:
    - `constexpr uint16_t kMagic = 0x4350;`
    - `constexpr uint16_t kDefaultPort = 4748;`
    - `constexpr uint32_t kMaxPayloadSize = 8 * 1024 * 1024;`
    - `enum class StreamType : uint8_t { Video = 1, Audio = 2, Telemetry = 3, Heartbeat = 4 };`
    - `enum PacketFlags : uint8_t { FlagNone = 0, FlagCodecConfig = 0x01, FlagKeyframe = 0x02, FlagHdrHlg = 0x04, FlagHdrPq = 0x08 };`
    - `struct PacketHeader { uint16_t magic; StreamType stream_type; uint8_t flags; uint32_t payload_length; uint64_t pts_ns; };`
    - `std::array<uint8_t, 16> serialize_header(const PacketHeader& hdr);`
    - `std::optional<PacketHeader> parse_and_validate_header(const uint8_t* buf, size_t len);`
    - `bool constant_time_equals(std::string_view a, std::string_view b);`
    - `bool is_valid_pin_format(std::string_view pin);`
    - `class BoundedFrameQueue` with `push(PacketHeader, std::vector<uint8_t>)`, `pop()`, `size()`, `clear()` that preserves `FlagCodecConfig` packets while capping normal frames at `max_frames`.

- [ ] **Step 1: Write the failing test in `protocol/test_protocol.cpp` and runner `protocol/run_tests.py`**

Test cases in `protocol/test_protocol.cpp`:
- `test_roundtrip_valid_header()`: Serializes `{0x4350, StreamType::Video, FlagKeyframe | FlagHdrHlg, 1048576, 123456789000ULL}` into 16 big-endian bytes and parses it back identically.
- `test_reject_invalid_magic_and_oversized_payload()` (Review Focus #1): Asserts `parse_and_validate_header` returns `std::nullopt` when `magic == 0x4443`, `stream_type == 0`, `stream_type == 99`, or `payload_length == 8388609` (`kMaxPayloadSize + 1`).
- `test_constant_time_auth_and_pin_validation()`: Asserts `constant_time_equals("482910", "482910") == true`, `constant_time_equals("482910", "482911") == false`, and `is_valid_pin_format` accepts only 6 ASCII digits.
- `test_bounded_queue_preserves_codec_config_on_overflow()` (Review Focus #5): Pushes 1 `FlagCodecConfig` packet + 8 normal video frames into a `BoundedFrameQueue(6)`; asserts queue size is `6` and the first popped packet is still the `FlagCodecConfig` packet.

- [ ] **Step 2: Run test to verify it fails**

Run: `& "C:\Users\Vinic\AppData\Local\Python\bin\python.exe" protocol/run_tests.py`
Expected: FAIL (`camphone_protocol.hpp: No such file or directory`).

- [ ] **Step 3: Implement `protocol/camphone_protocol.hpp` and ensure compiler toolchain in `protocol/run_tests.py`**

Implement big-endian serialization/deserialization, strict header bounds validation (`magic == 0x4350`, `stream_type in 1..4`, `payload_length <= kMaxPayloadSize`), constant-time XOR string comparison, and `BoundedFrameQueue` that never drops `FlagCodecConfig` frames when evicting the oldest frame on overflow.

- [ ] **Step 4: Run test to verify it passes**

Run: `& "C:\Users\Vinic\AppData\Local\Python\bin\python.exe" protocol/run_tests.py`
Expected: PASS (`All 4 protocol & security tests passed!`).

- [ ] **Step 5: Commit**

```powershell
git add protocol/camphone_protocol.hpp protocol/test_protocol.cpp protocol/run_tests.py
git commit -m "feat(protocol): add CamPhoneWire v1 16-byte framing, constant-time auth, and bounded queue"
```

---

### Task 2: Android Zero-Trust Security Gate & Input Clamping (`android-app/.../security/`)

**Files:**
- Create: `android-app/settings.gradle.kts`
- Create: `android-app/build.gradle.kts`
- Create: `android-app/gradle.properties`
- Create: `android-app/app/build.gradle.kts`
- Create: `android-app/app/src/main/java/com/cameraphone/obs/security/SecurityGate.kt`
- Create: `android-app/app/src/test/java/com/cameraphone/obs/security/SecurityGateTest.kt`

**Interfaces:**
- Consumes: `java.security.SecureRandom`, `java.security.MessageDigest`.
- Produces:
  - `package com.cameraphone.obs.security`
  - `data class CameraBounds(val validCameraIds: Set<String>, val zoomRange: ClosedFloatingPointRange<Float>, val isoRange: IntRange, val shutterNsRange: LongRange, val evRange: IntRange, val maxFocusDistance: Float, val maxBitrateBps: Int = 150_000_000)`
  - `data class SanitizedControlPatch(val cameraId: String?, val zoom: Float?, val aeMode: String?, val iso: Int?, val shutterNs: Long?, val ev: Int?, val wbMode: String?, val wbKelvin: Int?, val afMode: String?, val focusDistance: Float?, val torch: Boolean?, val bitrateBps: Int?, val requestIdr: Boolean?)`
  - `enum class AuthResult { ALLOWED, UNAUTHORIZED, RATE_LIMITED }`
  - `class SecurityGate(maxFailedAttempts: Int = 5, lockoutDurationMs: Long = 60_000L)`:
    - `fun currentPin(): String`
    - `fun regeneratePin(): String`
    - `fun verifyRequest(clientIp: String, providedToken: String?, nowMs: Long = System.currentTimeMillis()): AuthResult`
    - `fun sanitizeControlPatch(raw: Map<String, Any?>, bounds: CameraBounds): SanitizedControlPatch`

- [ ] **Step 1: Write the failing test in `SecurityGateTest.kt`**

Test cases:
- `testPinIsSixDigitsAndRegenerates()`: Verifies `currentPin()` matches `^\d{6}$` and `regeneratePin()` produces a valid 6-digit PIN.
- `testConstantTimeVerificationAndBruteForceLockout()` (Review Focus #2): Sends 5 invalid tokens from `"192.168.1.50"` -> returns `AuthResult.UNAUTHORIZED`; 6th attempt even with valid token -> returns `AuthResult.RATE_LIMITED`; after `nowMs + 60_001L` -> valid token returns `AuthResult.ALLOWED`.
- `testSanitizeControlPatchClampsAndRejectsInjection()` (Review Focus #3): Passes `mapOf("camera_id" to "../../etc/passwd", "zoom" to 999.0, "iso" to -500, "shutter_ns" to 999999999999L, "ae_mode" to "sql_inject", "bitrate_bps" to 999_000_000)` against `CameraBounds(setOf("0", "1"), 0.5f..10.0f, 50..3200, 40_000L..33_333_333L, -20..20, 10.0f)` -> asserts `cameraId == null`, `zoom == 10.0f`, `iso == 50`, `shutterNs == 33_333_333L`, `aeMode == null`, `bitrateBps == 150_000_000`.

- [ ] **Step 2: Run test to verify it fails**

Run Gradle test (or Kotlin/JVM test runner) on `SecurityGateTest.kt`.
Expected: FAIL (`Unresolved reference: SecurityGate`).

- [ ] **Step 3: Implement `SecurityGate.kt` and Gradle project configuration**

Use `SecureRandom` for 6-digit PIN generation, `MessageDigest.isEqual` for constant-time comparison, per-IP failure tracking with automatic expiry, and strict allowlist + numeric clamping in `sanitizeControlPatch`.

- [ ] **Step 4: Run test to verify it passes**

Run test on `SecurityGateTest.kt`.
Expected: PASS (all 3 test methods green).

- [ ] **Step 5: Commit**

```powershell
git add android-app/
git commit -m "feat(android-security): add Zero-Trust SecurityGate with CSPRNG PIN, brute-force lockout, and input clamping"
```

---

### Task 3: Android Binary Packet Framer & Capability Probe (`android-app/.../protocol/` & `camera/`)

**Files:**
- Create: `android-app/app/src/main/java/com/cameraphone/obs/protocol/CamPhonePacket.kt`
- Create: `android-app/app/src/main/java/com/cameraphone/obs/camera/CapabilityModels.kt`
- Create: `android-app/app/src/main/java/com/cameraphone/obs/camera/CapabilityProbe.kt`
- Create: `android-app/app/src/test/java/com/cameraphone/obs/protocol/CamPhonePacketTest.kt`

**Interfaces:**
- Consumes: `java.nio.ByteBuffer`, `java.nio.ByteOrder`, `android.hardware.camera2.*`, `android.media.MediaCodecList`.
- Produces:
  - `object CamPhonePacket`:
    - `const val MAGIC: Short = 0x4350`
    - `const val MAX_PAYLOAD_SIZE: Int = 8 * 1024 * 1024`
    - `fun encodeHeader(streamType: Byte, flags: Byte, payloadLength: Int, ptsNs: Long): ByteArray`
    - `fun decodeHeader(bytes: ByteArray): DecodedHeader?`
  - `data class ResolutionFormat(val width: Int, val height: Int, val fps: List<Int>)`
  - `data class CameraCapability(val id: String, val facing: String, val label: String, val focalLengthMm: Float, val aperture: Float, val torchSupported: Boolean, val hdrProfiles: List<String>, val zoomRange: List<Float>, val isoRange: List<Int>, val shutterNsRange: List<Long>, val evRange: List<Int>, val minFocusDistance: Float, val formats: List<ResolutionFormat>)`
  - `class CapabilityProbe`:
    - `fun buildCapabilitiesJson(context: Context): String`
    - `fun filterAndSortFormats(rawSizes: List<Pair<Int, Int>>, maxFpsBySize: Map<Pair<Int, Int>, List<Int>>, encoderSupported: (Int, Int, Int) -> Boolean): List<ResolutionFormat>`

- [ ] **Step 1: Write the failing test in `CamPhonePacketTest.kt`**

Test cases:
- `testEncodeAndDecode16ByteHeaderBigEndian()`: Encodes `streamType = 1`, `flags = 0x06`, `payloadLength = 524288`, `ptsNs = 987654321000L` -> verifies exact 16-byte layout (`[0x43, 0x50, 0x01, 0x06, ...]`) and round-trips via `decodeHeader`.
- `testRejectInvalidMagicAndOversizedPayload()` (Review Focus #1): Asserts `encodeHeader` throws `IllegalArgumentException` if `payloadLength < 0` or `payloadLength > MAX_PAYLOAD_SIZE`, and `decodeHeader` returns `null` for wrong magic or oversized length.
- `testFilterAndSortFormatsPrioritizes4kAnd60Fps()`: Verifies `CapabilityProbe.filterAndSortFormats` sorts resolutions descending from `3840x2160` (`[24, 30, 60]`) down to `1280x720` and excludes sizes rejected by the hardware `MediaCodec` predicate.

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL (`Unresolved reference: CamPhonePacket`).

- [ ] **Step 3: Implement `CamPhonePacket.kt`, `CapabilityModels.kt`, and `CapabilityProbe.kt`**

Implement 16-byte Big-Endian framing in `CamPhonePacket.kt` and full `Camera2` (`CameraCharacteristics`, physical lenses, 35mm equivalent focal length calculation, HDR10/HLG `DynamicRangeProfiles`) $\cap$ `MediaCodecList` (`video/hevc`, `video/avc`, `video/av01`) probing in `CapabilityProbe.kt`.

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS (all 3 tests green).

- [ ] **Step 5: Commit**

```powershell
git add android-app/app/src/main/java/com/cameraphone/obs/protocol/ android-app/app/src/main/java/com/cameraphone/obs/camera/ android-app/app/src/test/java/com/cameraphone/obs/protocol/
git commit -m "feat(android-probe): add 16-byte packet framer and dynamic 4K/60fps/HDR Camera2 CapabilityProbe"
```

---

### Task 4: Android Zero-Copy `Camera2Controller` & Hardware `MediaCodec` Encoders (`android-app/.../media/`)

**Files:**
- Create: `android-app/app/src/main/java/com/cameraphone/obs/media/HardwareVideoEncoder.kt`
- Create: `android-app/app/src/main/java/com/cameraphone/obs/media/HardwareAudioEncoder.kt`
- Create: `android-app/app/src/main/java/com/cameraphone/obs/camera/Camera2Controller.kt`

**Interfaces:**
- Consumes: `CamPhonePacket`, `SanitizedControlPatch`, `CameraBounds`, `android.hardware.camera2.*`, `android.media.*`.
- Produces:
  - `class HardwareVideoEncoder(width: Int, height: Int, fps: Int, bitrateBps: Int, codecId: String, hdrProfile: String, onPacket: (Byte, Long, ByteArray) -> Unit)`:
    - `fun start(): Surface`
    - `fun updateBitrate(newBitrateBps: Int)`
    - `fun requestKeyFrame()`
    - `fun stop()`
  - `class HardwareAudioEncoder(sampleRate: Int = 48000, channels: Int = 2, bitrateBps: Int = 192_000, onPacket: (Byte, Long, ByteArray) -> Unit)`:
    - `fun start()`
    - `fun setMuted(muted: Boolean)`
    - `fun stop()`
  - `class Camera2Controller(context: Context)`:
    - `fun startCapture(cameraId: String, width: Int, height: Int, fps: Int, hdrProfile: String, encoderSurface: Surface, previewSurface: Surface?)`
    - `fun applyControlPatch(patch: SanitizedControlPatch)`
    - `fun currentBounds(): CameraBounds`
    - `fun stopCapture()`

- [ ] **Step 1: Write test verifying encoder config & parameter mapping in `CamPhonePacketTest.kt`**

Add `testCodecMimeAndHdrProfileMapping()` asserting `"hevc"` maps to `"video/hevc"`, `"avc"` to `"video/avc"`, `"av1"` to `"video/av01"`, and `"hlg10"` sets `HEVCProfileMain10` + `COLOR_STANDARD_BT2020` + `COLOR_TRANSFER_HLG`.

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL (`Unresolved reference: HardwareVideoEncoder`).

- [ ] **Step 3: Implement `HardwareVideoEncoder.kt`, `HardwareAudioEncoder.kt`, and `Camera2Controller.kt`**

- `HardwareVideoEncoder`: Configures `MediaCodec` with `COLOR_FormatSurface`, low-latency flags (`KEY_LOW_LATENCY = 1`, `KEY_PRIORITY = 0` real-time), 1-second GOP (`KEY_I_FRAME_INTERVAL = 1`), optional 10-bit HDR (`COLOR_STANDARD_BT2020`, `COLOR_TRANSFER_HLG` / `ST2084`), and emits `FlagCodecConfig` (`0x01`) for `BUFFER_FLAG_CODEC_CONFIG` and `FlagKeyframe` (`0x02`) for `BUFFER_FLAG_KEY_FRAME`.
- `HardwareAudioEncoder`: Captures 48 kHz stereo PCM from `AudioRecord` (`AudioSource.CAMCORDER` with fallback to `MIC`) and encodes via `MediaCodec` `"audio/mp4a-latm"` (`AACObjectLC`), emitting the 2-byte `AudioSpecificConfig` with `FlagCodecConfig`.
- `Camera2Controller`: Opens `CameraDevice` on a dedicated `HandlerThread`, creates `OutputConfiguration` with `DynamicRangeProfiles.HLG10` / `HDR10` when requested, sets `CONTROL_AE_TARGET_FPS_RANGE`, and applies live `SanitizedControlPatch` updates (`CONTROL_ZOOM_RATIO`, manual `SENSOR_SENSITIVITY` + `SENSOR_EXPOSURE_TIME`, `CONTROL_AE_EXPOSURE_COMPENSATION`, manual `COLOR_CORRECTION_GAINS` Rggb Kelvin approximation, `LENS_FOCUS_DISTANCE`, `FLASH_MODE_TORCH`) via `captureSession.setRepeatingRequest` without dropping a single frame.

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add android-app/app/src/main/java/com/cameraphone/obs/media/ android-app/app/src/main/java/com/cameraphone/obs/camera/Camera2Controller.kt android-app/app/src/test/java/com/cameraphone/obs/protocol/CamPhonePacketTest.kt
git commit -m "feat(android-media): add zero-copy Camera2Controller and HardwareVideoEncoder/AudioEncoder (4K/HDR/AAC)"
```

---

### Task 5: Android Zero-Trust TCP/HTTP Server, Foreground Service & Minimalist OLED UI (`android-app/.../net/`, `service/`, `MainActivity.kt`)

**Files:**
- Create: `android-app/app/src/main/java/com/cameraphone/obs/net/CamPhoneServer.kt`
- Create: `android-app/app/src/main/java/com/cameraphone/obs/service/CameraStreamService.kt`
- Create: `android-app/app/src/main/java/com/cameraphone/obs/MainActivity.kt`
- Create: `android-app/app/src/main/AndroidManifest.xml`
- Create: `android-app/app/src/main/res/values/strings.xml`
- Create: `android-app/app/src/main/res/values/colors.xml`
- Create: `android-app/app/src/main/res/layout/activity_main.xml`

**Interfaces:**
- Consumes: `SecurityGate`, `CamPhonePacket`, `CapabilityProbe`, `Camera2Controller`, `HardwareVideoEncoder`, `HardwareAudioEncoder`.
- Produces:
  - `class CamPhoneServer(port: Int = 4748, usbOnlyLoopback: Boolean = true, securityGate: SecurityGate, handler: RequestHandler)`:
    - Binds `ServerSocket` to `InetAddress.getByName("127.0.0.1")` when `usbOnlyLoopback == true` or `InetAddress.getByName("0.0.0.0")` when `usbOnlyLoopback == false`.
    - Handles `GET /api/v1/ping`, `GET /api/v1/capabilities`, `GET /api/v1/status`, `PATCH /api/v1/control`, `PUT /api/v1/tally`, and `GET /api/v1/stream`.
  - `CameraStreamService`: Android Foreground Service (`foregroundServiceType="camera|microphone"`), manages `NsdManager` registration (`_cameraphoneobs._tcp.`) only when Wi-Fi mode is enabled, and holds a partial `WakeLock`.
  - `MainActivity`: Minimalist OLED (`#0D0F12`) single-screen UI with:
    1. Tally Status Banner (`IDLE` / `PREVIEW` / `NO AR - PROGRAM`)
    2. Security Mode Toggle (`Somente Cabo USB (127.0.0.1 - Ultra Seguro)` vs `USB + Wi-Fi LAN`)
    3. Connection Card (`Porta 4748`, IP LAN, **6-Digit Security PIN** + botão `Novo PIN`)
    4. Botão `Modo Blackout (Apagar Tela p/ Economizar Bateria)` — apaga a tela para `#000000` com um duplo-toque para restaurar.

- [ ] **Step 1: Write server security & routing unit tests in `SecurityGateTest.kt`**

Add `testServerRejectsUnauthenticatedAndEnforcesSingleClientLock()` (Review Focus #2): Starts `CamPhoneServer` on an ephemeral loopback port (`127.0.0.1:0`) with a mock `RequestHandler`:
- `GET /api/v1/ping` without token -> `200 OK`
- `GET /api/v1/capabilities` without token -> `401 Unauthorized`
- `GET /api/v1/capabilities` with valid `X-CamPhone-Token` -> `200 OK` + JSON capabilities
- `GET /api/v1/stream` with valid token -> `200 OK` + 16-byte framed packets
- Second concurrent `GET /api/v1/stream` while first stream is active -> `409 Conflict`.

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL (`Unresolved reference: CamPhoneServer`).

- [ ] **Step 3: Implement `CamPhoneServer.kt`, `CameraStreamService.kt`, `MainActivity.kt`, `AndroidManifest.xml`, and resources**

Implement non-blocking/timeout-guarded `CamPhoneServer`, `CameraStreamService`, and the minimalist OLED `MainActivity`, then build the APK and run unit tests.

- [ ] **Step 4: Run tests and assemble the Android APK**

Run unit tests and assemble `app-debug.apk`.
Expected: PASS and `app-debug.apk` generated cleanly.

- [ ] **Step 5: Commit**

```powershell
git add android-app/
git commit -m "feat(android-app): add CamPhoneServer, ForegroundService, and minimalist OLED Zero-Trust UI"
```

---

### Task 6: Desktop OBS Plugin Core — Discovery (`ADB` + `mDNS`), Non-Blocking Stream Client & FFmpeg 4K/HDR Decoder (`obs-plugin/`)

**Files:**
- Create: `obs-plugin/src/discovery-manager.hpp`
- Create: `obs-plugin/src/discovery-manager.cpp`
- Create: `obs-plugin/src/stream-client.hpp`
- Create: `obs-plugin/src/stream-client.cpp`
- Create: `obs-plugin/src/media-decoder.hpp`
- Create: `obs-plugin/src/media-decoder.cpp`
- Create: `obs-plugin/src/plugin-main.cpp`
- Create: `obs-plugin/src/cameraphone-source.hpp`
- Create: `obs-plugin/src/cameraphone-source.cpp`
- Create: `obs-plugin/CMakeLists.txt`
- Create: `obs-plugin/tests/test_client_and_discovery.cpp`

**Interfaces:**
- Consumes: `protocol/camphone_protocol.hpp`, Winsock2 (`ws2_32`), FFmpeg (`avcodec`, `avutil`), `libobs`.
- Produces:
  - `class DiscoveryManager`:
    - `static bool is_valid_adb_serial(std::string_view serial)` (Allowlist `[A-Za-z0-9._:-]+` preventing command injection).
    - `std::vector<DiscoveredDevice> list_usb_devices(const std::string& adb_path)`
    - `bool forward_usb_port(const std::string& adb_path, const std::string& serial, uint16_t local_port, uint16_t remote_port = 4748)`
    - `std::vector<DiscoveredDevice> scan_mdns_devices(uint32_t timeout_ms = 500)`
  - `class StreamClient`:
    - `std::optional<std::string> fetch_capabilities(const std::string& host, uint16_t port, const std::string& token)`
    - `bool send_control_patch(const std::string& host, uint16_t port, const std::string& token, const std::string& json_body)`
    - `bool send_tally_state(const std::string& host, uint16_t port, const std::string& token, const std::string& state)`
    - `void start_stream(const StreamConfig& cfg, FrameCallback on_frame, StatusCallback on_status)`
    - `void stop_stream()`
  - `class MediaDecoder`:
    - Dynamically binds FFmpeg `avcodec-62.dll` / `avutil-60.dll` (from OBS Studio's bin folder) to decode `H.264`, `HEVC` (including 10-bit `P010` HLG/HDR10), `AV1`, and `AAC` audio with hardware acceleration (`D3D11VA` / `DXVA2` / `CUDA`).
  - `cameraphone-obs.dll`:
    - Exports `obs_module_load`, `obs_module_unload`, `obs_module_ver` and registers the `cameraphone_obs` async video+audio source with dynamic capability dropdowns and real-time Pro camera sliders.

- [ ] **Step 1: Write the failing test in `obs-plugin/tests/test_client_and_discovery.cpp`**

Test cases:
- `test_adb_serial_sanitization_blocks_command_injection()`: Verifies `DiscoveryManager::is_valid_adb_serial("R5CW12345AB") == true`, `is_valid_adb_serial("emulator-5554") == true`, and `is_valid_adb_serial("123 & calc.exe") == false`.
- `test_stream_client_handles_truncated_packet_without_freezing()` (Review Focus #4): Spawns a local loopback mock TCP server that sends `HTTP/1.1 200 OK\r\n\r\n`, one valid 16-byte header (`payload_length = 4096`), and only `512` bytes before closing the socket -> asserts `StreamClient` detects the EOF/timeout within `< 2500 ms`, does not invoke `on_frame` with a partial buffer, and exits cleanly when `stop_stream()` is called.
- `test_stream_client_authenticates_and_receives_4k_capabilities_and_frames()`: Connects `StreamClient` to a mock server requiring `X-CamPhone-Token: 654321`, fetches `/api/v1/capabilities`, sends `PATCH /api/v1/control`, and receives a `FlagCodecConfig` + `FlagKeyframe` video packet.

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL (`discovery-manager.hpp: No such file or directory`).

- [ ] **Step 3: Implement `obs-plugin/src/*` and build `cameraphone-obs.dll` + `test_client_and_discovery.exe`**

Implement `DiscoveryManager`, `StreamClient`, `MediaDecoder`, `cameraphone-source.cpp`, and `plugin-main.cpp`.

- [ ] **Step 4: Run C++ client/discovery tests and verify `cameraphone-obs.dll` exports**

Run `test_client_and_discovery.exe` and inspect exports of `cameraphone-obs.dll` with REA `inspect_native_exports`.
Expected: PASS (`All C++ OBS plugin tests passed!`) and `cameraphone-obs.dll` exports `obs_module_load`, `obs_module_unload`, `obs_module_ver`.

- [ ] **Step 5: Commit**

```powershell
git add obs-plugin/
git commit -m "feat(obs-plugin): implement 64-bit cameraphone-obs.dll with dynamic 4K/HDR capabilities, Pro controls, and Zero-Freeze client"
```

---

### Task 7: End-to-End Integration & Emulator/Device Verification (`tools/verify_e2e_stream.py`)

**Files:**
- Create: `tools/verify_e2e_stream.py`
- Create: `README.md`

**Interfaces:**
- Consumes: `android-app` (`CamPhoneServer` / APK on emulator or local JVM harness), `obs-plugin` (`cameraphone-obs.dll` & `test_client_and_discovery.exe`).
- Produces: Complete E2E verification report testing Zero-Trust authentication (401/429), dynamic 4K60/HDR capability negotiation, live Pro control patching, Tally updates, and 16-byte framed media streaming.

- [ ] **Step 1: Write `tools/verify_e2e_stream.py` exercising the full protocol and security test matrix**

- [ ] **Step 2: Run `tools/verify_e2e_stream.py` and Android / C++ test suites**

Run: `& "C:\Users\Vinic\AppData\Local\Python\bin\python.exe" tools/verify_e2e_stream.py`
Expected: PASS (`E2E Verification: 100% checks passed`).

- [ ] **Step 3: Write `README.md` with clear, minimalist installation and usage guide (USB & Wi-Fi)**

- [ ] **Step 4: Commit**

```powershell
git add tools/verify_e2e_stream.py README.md
git commit -m "test(e2e): add end-to-end verification suite and minimalist user documentation"
```
