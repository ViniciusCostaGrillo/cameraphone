# Especificação de Design: Cameraphone Plugin OBS (4K / 60+ FPS / HDR / Zero-Trust Security)

> Este documento define a arquitetura limpa (*clean-room*), segura (*Zero-Trust*) e à prova de travamentos do **Cameraphone Plugin OBS**, projetada a partir das descobertas da engenharia reversa do DroidCam OBS (`docs/droidcam-reverse-engineering.md`).

---

## 1. Pilares Arquiteturais

1. **Qualidade Máxima Automática (`CapabilityProbe`):** Interrogador dinâmico da API `Camera2` (`CameraCharacteristics` + lentes físicas multi-sensor) e `MediaCodecList` para detectar automaticamente todas as lentes (Ultra-Wide, Principal, Telefoto, Frontal), resoluções máximas (4K UHD `3840x2160` e superiores), taxas de quadros (`30`, `60`, `120` FPS) e perfis HDR 10-bit (`HLG10`, `HDR10`).
2. **Segurança Extrema Anti-Hacker (*Zero-Trust Camera Security*):**
   - **Vulnerabilidade corrigida do DroidCam:** No DroidCam original, qualquer pessoa na mesma rede Wi-Fi pode abrir `http://<ip>:4747/v5/video/...` e espionar a câmera e o microfone sem senha.
   - **Isolamento Físico no Modo USB (`127.0.0.1` Loopback Bind):** Quando em modo USB, o socket TCP do Android faz bind exclusivamente em `127.0.0.1:4748`. Nenhuma porta fica aberta na placa Wi-Fi.
   - **Autenticação Criptográfica por Token/PIN (`CSPRNG` + `HMAC-SHA256`):** Cada sessão exige um código de pareamento de 6 dígitos (gerado via `SecureRandom`, rotacionável com 1 toque) validado via cabeçalho `Authorization: Bearer <token>` (ou `HMAC-SHA256` com proteção anti-replay). No modo USB via ADB, o plugin OBS pode ler/injetar o token automaticamente via ADB seguro.
   - **Rate Limiting & Bloqueio de Força Bruta:** Máximo de 5 tentativas inválidas por IP antes de bloqueio automático temporário (HTTP `429 Too Many Requests`).
   - **Single-Client Lock & Indicador Visual Inviolável:** Apenas 1 cliente autenticado pode consumir o stream por vez, exibindo no Android o IP/USB conectado e o status **Tally (NO AR / PREVIEW / IDLE)**.
   - **Validação Estrita (Allowlist & Clamping):** Todos os parâmetros do `PATCH /api/v1/control` são validados por tipo e limitados (`clamp`) às faixas reais do hardware (`CameraCharacteristics`), prevenindo ataques de injeção ou DoS.
3. **Estabilidade Absoluta ("Zero-Freeze" no OBS e no Android):**
   - **Isolamento de Threads no OBS:** A thread de renderização do OBS nunca faz I/O de rede ou chamadas ADB. Sockets usam timeouts estritos (`SO_RCVTIMEO = 2000ms`, `TCP_NODELAY = 1`) e filas circulares limitadas (*Bounded Ring Buffers*: máx 6 frames de vídeo). Se o cabo USB for puxado, a fonte não trava o OBS e reconecta automaticamente em background.
   - **Recuperação Determinística no Android:** `Camera2Controller` e `HardwareVideoEncoder` usam `HandlerThread` dedicados, captura de `CameraAccessException` / `MediaCodec.CodecException` e liberação determinística de `Surface` sem vazamento de memória.
4. **Interface Minimalista e Direto ao Ponto:**
   - **No Android:** Tela escura (*OLED Dark Mode* `#0D0F12` para economizar bateria e evitar aquecimento), exibindo apenas o essencial:
     1. **Badge de Status & Tally** (Desconectado / Conectado USB / Conectado Wi-Fi / EM CENA - PROGRAM em vermelho / PREVIEW em verde).
     2. **Seletor de Modo de Segurança:** `Apenas USB (Loopback 127.0.0.1)` vs `USB + Wi-Fi LAN`.
     3. **Card de Conexão:** IP Local + Porta `4748` + **PIN de Segurança de 6 dígitos** (com botão de regenerar).
     4. **Botão "Economia de Tela (Blackout Preview)":** Desliga a renderização do preview na tela do celular mantendo o stream 4K60 rodando frio sem gastar GPU/tela.
   - **No OBS Studio:** Painel de propriedades limpo na fonte `Cameraphone OBS (4K Pro)`:
     - Modo de Conexão (`USB Automático (ADB)` ou `Wi-Fi (IP / Auto-Descoberta mDNS)`), Campo de PIN, Botão **Conectar & Detectar Câmera**, seletores dinâmicos populados pelo celular (**Lente**, **Resolução**, **FPS**, **Codec HEVC/AVC/AV1**, **HDR**, **Bitrate Mbps**) e controles diretos (**Zoom**, **Modo de Exposição Auto/Manual**, **ISO**, **Shutter**, **Foco**, **Balanço de Branco**, **Lanterna**).

---

## 2. Especificação do Protocolo `CamPhoneWire v1`

### 2.1 Endpoints HTTP Autenticados (Porta `4748`)
Todas as requisições exigem o header `X-CamPhone-Token: <6-digit-pin-or-session-token>` (exceto `GET /api/v1/ping` que retorna apenas `{"app":"CameraphoneOBS","version":"1.0","auth_required":true}`).

- **`GET /api/v1/capabilities`**: Retorna o JSON completo com informações do dispositivo, codecs de hardware suportados e lista de câmeras/lentes com suas resoluções, FPS e faixas de sensores.
- **`GET /api/v1/status`**: Retorna telemetria em tempo real (`battery_pct`, `charging`, `temp_celsius`, `active_camera`, `current_fps`, `current_bitrate_kbps`, `tally_state`, `controls`).
- **`PATCH /api/v1/control`**: Atualiza parâmetros da câmera em tempo real (corpo JSON validado e limitado às faixas do sensor).
- **`PUT /api/v1/tally`**: Atualiza o estado de Tally Light (`{"state": "program" | "preview" | "idle"}`).
- **`GET /api/v1/stream?camera={id}&codec={hevc|avc|av1}&width={W}&height={H}&fps={FPS}&bitrate={BPS}&hdr={sdr|hlg10|hdr10}&audio={aac|off}`**:
  Valida autenticação e parâmetros, responde `HTTP/1.1 200 OK\r\nContent-Type: application/x-camphone-stream\r\n\r\n` e transmite pacotes binários de 16 bytes (`CamPhonePacketHeader`).

### 2.2 Cabeçalho Binário de 16 Bytes (`CamPhonePacketHeader`)
Todos os campos numéricos multi-byte estão em **Big-Endian (Network Byte Order)**:

| Offset | Campo | Tipo | Descrição |
| :--- | :--- | :--- | :--- |
| `0..1` | `magic` | `uint16_t` | Sempre `0x4350` (`'C'`, `'P'`). Qualquer valor diferente encerra a leitura imediatamente por segurança. |
| `2` | `stream_type` | `uint8_t` | `0x01` = Vídeo, `0x02` = Áudio, `0x03` = Telemetria JSON, `0x04` = Heartbeat/KeepAlive. |
| `3` | `flags` | `uint8_t` | `0x01` = `FLAG_CODEC_CONFIG` (CSD: SPS/PPS/VPS ou AAC ASC), `0x02` = `FLAG_KEYFRAME`, `0x04` = `FLAG_HDR_HLG`, `0x08` = `FLAG_HDR_PQ`. |
| `4..7` | `payload_length` | `uint32_t` | Tamanho do payload (`0 <= len <= 8388608` [8 MiB]). Valores acima de 8 MiB são rejeitados contra ataques de exaustão de memória. |
| `8..15` | `pts_ns` | `uint64_t` | Timestamp monotônico de captura em **nanossegundos**. |

---

## 3. Arquitetura de Arquivos (`c:\Cameraphone Plugin OBS`)

```text
c:\Cameraphone Plugin OBS\
├── docs/
│   ├── droidcam-reverse-engineering.md
│   └── superpowers/
│       ├── specs/2026-11-04-cameraphone-obs-design.md
│       └── plans/2026-11-04-cameraphone-obs-plan.md
├── protocol/
│   ├── camphone_protocol.h              # Header C++20 header-only: framing 16B, validação de limites, constantes e serialização
│   └── test_protocol.cpp                # Testes unitários C++ do protocolo, segurança de framing e bounds checking
├── obs-plugin/
│   ├── CMakeLists.txt                   # Build CMake do plugin OBS (64-bit Windows DLL) e executável de testes
│   └── src/
│       ├── plugin-main.cpp              # Entrypoint obs_module_load / obs_module_unload
│       ├── cameraphone-source.hpp       # Declaração da fonte OBS e propriedades dinâmicas
│       ├── cameraphone-source.cpp       # Implementação da fonte OBS, UI minimalista e Tally callback
│       ├── security-auth.hpp            # Geração de nonce, assinatura/cabeçalhos de autenticação e sanitização de entradas
│       ├── discovery-manager.hpp        # Descoberta USB (ADB forward seguro) + Wi-Fi mDNS (_cameraphoneobs._tcp.local.)
│       ├── discovery-manager.cpp
│       ├── stream-client.hpp            # Cliente TCP/HTTP não-bloqueante, reconexão automática e fila circular limitada
│       ├── stream-client.cpp
│       ├── media-decoder.hpp            # Decodificador FFmpeg (H.264/HEVC/AV1 + D3D11VA/NVDEC + P010 HDR + AAC Audio)
│       └── media-decoder.cpp
├── android-app/
│   ├── build.gradle.kts
│   ├── settings.gradle.kts
│   ├── gradle.properties
│   └── app/
│       ├── build.gradle.kts
│       └── src/
│           ├── main/
│           │   ├── AndroidManifest.xml
│           │   ├── res/                 # Layout minimalista OLED Dark Mode, cores e strings
│           │   └── java/com/cameraphone/obs/
│           │       ├── MainActivity.kt               # UI Minimalista (Modo USB/Wi-Fi, PIN de 6 dígitos, Tally, Blackout Preview)
│           │       ├── security/SecurityGate.kt      # CSPRNG PIN, Constant-time token check, Rate-Limiter IP e Input Clamping
│           │       ├── protocol/CamPhonePacket.kt    # Empacotador Big-Endian de 16 bytes com validação de limites
│           │       ├── camera/CapabilityProbe.kt     # Interrogador de lentes físicas/lógicas, 4K/60/120fps, HDR10/HLG e sensores
│           │       ├── camera/Camera2Controller.kt   # Sessão Camera2 Zero-Copy Surface + controles Pro em tempo real
│           │       ├── media/HardwareVideoEncoder.kt # MediaCodec HEVC/AVC/AV1 Surface Encoder (até 150 Mbps + HDR)
│           │       ├── media/HardwareAudioEncoder.kt # AudioRecord 48kHz + MediaCodec AAC
│           │       ├── net/CamPhoneServer.kt         # Servidor TCP/HTTP (bind 127.0.0.1 no modo USB ou LAN no Wi-Fi) + mDNS
│           │       └── service/CameraStreamService.kt # ForegroundService resiliente com WakeLock
│           └── test/java/com/cameraphone/obs/
│               ├── SecurityGateTest.kt               # Testes unitários de autenticação, brute-force lockout e input clamping
│               └── CamPhonePacketTest.kt             # Testes unitários do framing de 16 bytes
└── tools/
    └── verify_e2e_stream.py             # Suite de verificação ponta a ponta (Testa handshake, segurança 401/429, capabilities 4K e stream binário)
```
