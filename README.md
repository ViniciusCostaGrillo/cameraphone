# CameraPhone OBS (4K60/120 HDR10 • Zero-Trust • Zero-Freeze)

Sistema completo e gratuito de câmera profissional via smartphone para o **OBS Studio**, projetado a partir de engenharia reversa clean-room para eliminar as limitações, paywalls (`HTTP 550`), marcas d'água e falhas de segurança do DroidCam OBS.

## Principais Diferenciais

1. **Qualidade Máxima Automática por Hardware (`CapabilityProbe`):**
   - Interroga diretamente a `Camera2 API` (`CameraCharacteristics`, `physicalCameraIds`) e `MediaCodecList` do Android.
   - Libera todas as lentes físicas reais do aparelho (Ultra-Wide, Principal, Telefoto 3x/5x/10x, Frontal) com cálculo automático da distância focal equivalente em 35mm.
   - Suporte nativo a **4K UHD (`3840x2160`)**, **60 FPS / 120 FPS High-Speed**, **HEVC (H.265)**, **AV1**, **AVC (H.264)** e **HDR 10-bit (`HLG10` / `HDR10` `BT.2020`)** com codificação 100% em hardware zero-copy via `Surface`.
2. **Segurança Extrema Anti-Hacker (Zero-Trust):**
   - **PIN CSPRNG de 6 dígitos** gerado por `SecureRandom` a cada sessão e validado em tempo constante (`MessageDigest.isEqual` / `constant_time_equals`) contra ataques de timing.
   - **Rate-Limiting e Lockout por IP:** Bloqueia automaticamente por 60 segundos (`HTTP 429`) qualquer IP que erre o PIN 5 vezes.
   - **Isolamento Estrito no Modo Cabo USB (`127.0.0.1`):** Quando o switch *"Modo Exclusivo Cabo USB"* está ativo no app, o socket TCP faz bind estrito em `127.0.0.1:4748`, tornando o celular invisível na rede Wi-Fi pública/doméstica.
   - **Sanitização de Comandos:** Serial ADB validado por allowlist estrita (imune a Command Injection) e parâmetros de câmera validados contra os limites físicos reais do sensor.
3. **Arquitetura Zero-Freeze (Sem Travamentos):**
   - Sockets dedicados com `TCP_NODELAY = 1` e `SO_RCVTIMEO = 2000ms` (a thread de UI do OBS nunca trava se o cabo for desconectado).
   - Fila limitada (`BoundedFrameQueue`) de baixa latência que descarta quadros antigos em caso de congestionamento, mas **preserva obrigatoriamente** cabeçalhos de codec (`SPS/PPS/VPS`).
4. **Interface Minimalista OLED Dark:**
   - App Android em preto carbono (`#0D0F12`), sem preview redundante na tela para economizar bateria e evitar superaquecimento em gravações 4K60, com **Modo Tela Preta OLED** (duplo toque para sair) e indicador **Tally Light** (`NO AR` em vermelho / `EM PREVIEW` em verde).

---

## Estrutura do Projeto & Binários Prontos (`dist/`)

- [`CameraPhone-OBS.exe`](CameraPhone-OBS.exe) / [`dist/CameraPhone-OBS.exe`](dist/CameraPhone-OBS.exe) — Aplicativo Desktop Companion & Instalador 1-Clique para Windows (instala o plugin no OBS Studio, configura túnel ADB USB e testa o hardware da câmera).
- [`CameraPhone-OBS.apk`](CameraPhone-OBS.apk) / [`dist/CameraPhone-OBS.apk`](dist/CameraPhone-OBS.apk) — Aplicativo Android compilado e assinado.
- [`dist/cameraphone-obs.dll`](dist/cameraphone-obs.dll) — Plugin nativo C++20 de 64 bits para OBS Studio.
- [`docs/droidcam-reverse-engineering.md`](docs/droidcam-reverse-engineering.md) — Relatório completo da engenharia reversa do DroidCam OBS (`droidcam-obs.dll` + `DroidCam APK.apkm`).
- [`docs/superpowers/specs/2026-11-04-cameraphone-obs-design.md`](docs/superpowers/specs/2026-11-04-cameraphone-obs-design.md) — Especificação de arquitetura e protocolo `CamPhoneWire v1`.
- [`protocol/`](protocol/) — Header C++20 compartilhado (`camphone_protocol.hpp`) e testes unitários de protocolo/segurança.
- [`android-app/`](android-app/) — Código-fonte do aplicativo Android nativo em Kotlin (`Camera2` + `MediaCodec` + `NsdManager` + `SecurityGate`).
- [`obs-plugin/`](obs-plugin/) — Código-fonte do plugin nativo C++20 de 64 bits para OBS Studio (`cameraphone-obs.dll`).
- [`desktop-app/`](desktop-app/) — Código-fonte do aplicativo Desktop Companion (`CameraPhone-OBS.exe`).
- [`tools/verify_e2e_stream.py`](tools/verify_e2e_stream.py) — Verificador de integração End-to-End do protocolo, segurança e negociação 4K60 HDR.

---

## Como Instalar e Usar

### 1. Instalar o Plugin no OBS Studio (Windows 64-bit)
Copie o binário compilado para a pasta de plugins do OBS Studio:
```powershell
Copy-Item "obs-plugin\build\cameraphone-obs.dll" "C:\Program Files\obs-studio\obs-plugins\64bit\cameraphone-obs.dll" -Force
```

### 2. Instalar o App no Android via ADB
Conecte o celular com a **Depuração USB** ativada e execute:
```powershell
adb install -r "android-app\app\build\outputs\apk\debug\app-debug.apk"
```

### 3. Conectar no OBS Studio
1. Abra o app **CameraPhone OBS** no celular e anote o **PIN de 6 dígitos** exibido na tela.
2. No OBS Studio, adicione uma nova Fonte: **CameraPhone 4K Pro (USB / Wi-Fi)**.
3. Selecione o dispositivo USB (ou IP Wi-Fi), digite o PIN de 6 dígitos e escolha a **Lente Física**, **Resolução (até 4K)**, **FPS (até 120)**, **Codec (HEVC / AV1 / AVC)**, **HDR 10-bit** e ajustes Pro (Zoom, ISO, Balanço de Branco e Lanterna).

---

## Executando Todas as Suítes de Teste

```powershell
# 1. Testes C++20 do Protocolo CamPhoneWire v1 e Segurança
python protocol/run_tests.py

# 2. Testes Unitários JUnit4 do App Android (SecurityGate, CamPhonePacket, CapabilityProbe, CamPhoneServer)
python android-app/build_and_test.py test

# 3. Testes C++20 e Build do Plugin OBS (cameraphone-obs.dll)
python obs-plugin/build_plugin.py all

# 4. Teste de Integração End-to-End (Handshake, Rate-Limit, Controle e Streaming Binário)
python tools/verify_e2e_stream.py
```
