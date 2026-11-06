<div align="center">

<img src="desktop-app/cameraphone_logo.png" width="140" alt="CameraPhone Logo" />

# 📱🎥 CameraPhone — 4K60/120 HDR10 Pro

**Transforme seu smartphone Android em uma câmera de estúdio 4K60/120 FPS HDR 10-bit para o PC.**  
*100% Gratuito • Zero Marca D'água • Segurança Zero-Trust Anti-Hacker • Arquitetura Zero-Freeze*

[![Windows](https://img.shields.io/badge/Windows-64--bit_Plugin-0078D6?style=for-the-badge&logo=windows&logoColor=white)](dist/CameraPhone.exe)
[![Android](https://img.shields.io/badge/Android-9.0%2B_(API_28--34)-3DDC84?style=for-the-badge&logo=android&logoColor=white)](dist/CameraPhone.apk)
[![C++20](https://img.shields.io/badge/C%2B%2B-20_Native-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](obs-plugin/)
[![Kotlin](https://img.shields.io/badge/Kotlin-Camera2_%2B_MediaCodec-7F52FF?style=for-the-badge&logo=kotlin&logoColor=white)](android-app/)
[![Security](https://img.shields.io/badge/Security-Zero--Trust_CSPRNG-00E676?style=for-the-badge&logo=securityscorecard&logoColor=white)](docs/superpowers/specs/2026-11-04-cameraphone-obs-design.md)

[📦 **Baixar Instalador Windows (`CameraPhone.exe`)**](dist/CameraPhone.exe) • [🤖 **Baixar App Android (`CameraPhone.apk`)**](dist/CameraPhone.apk) • [🔌 **Baixar Plugin Nativo (`cameraphone.dll`)**](dist/cameraphone.dll) • [🔬 **Relatório de Engenharia Reversa**](docs/droidcam-reverse-engineering.md)

</div>

---

## 🎯 Por que o CameraPhone existe?

Durante a análise de arquitetura de soluções proprietárias de câmera via smartphone ([leia o relatório técnico aqui](docs/droidcam-reverse-engineering.md)), identificamos limitações severas de qualidade, estabilidade e segurança que o **CameraPhone** resolve de forma definitiva:

| Recurso / Critério | ❌ Soluções Tradicionais | ✅ CameraPhone (Este Projeto) |
| :--- | :--- | :--- |
| **Resolução 4K UHD (`3840x2160`)** | Escondida ou bloqueada por paywall | **Liberada nativamente** com negociação automática de sensor (`CameraCharacteristics`) |
| **Taxa de Quadros (FPS)** | Limitada a 30/60 FPS fixos | **24 / 30 / 60 / 120 FPS High-Speed** (baseado na capacidade real do hardware) |
| **Codecs de Vídeo em Hardware** | `AVC (H.264)` e `MJPEG` legado | **`HEVC (H.265)`**, **`AV1`** e **`AVC (H.264)`** Zero-Copy via `Surface` (até 150 Mbps) |
| **HDR 10-bit (`HLG10` / `HDR10`)** | Não suportado (apenas 8-bit SDR) | **Suporte nativo a 10-bit `BT.2020` (`P010` / `I010`)** no Android 13+ |
| **Lentes Físicas Múltiplas** | Genéricas, sem distância focal real | **Descoberta automática de todas as lentes físicas** (Ultra-Wide 13mm, Wide 24mm, Telefoto 3x/5x/10x, Frontal) |
| **Controles Pro em Tempo Real** | Bloqueados com erro `HTTP 550` na versão gratuita | **100% liberados:** Zoom Óptico/Híbrido, ISO, Obturador, EV, Balanço de Branco (Kelvin) e Lanterna |
| **Marca D'água** | Injetada nos quadros após alguns minutos | **Zero marca d'água** — pipeline de vídeo 100% limpo |
| **Segurança de Rede (Anti-Hacker)** | Servidor HTTP aberto sem senha na rede Wi-Fi | **Zero-Trust:** PIN de 6 dígitos CSPRNG no Wi-Fi, comparação em tempo constante, bloqueio de IP contra força bruta e autenticação automática via Cabo USB (`127.0.0.1`) |

---

## 🚀 Instalação Zero-Config e Diagnóstico Integrado

Todos os binários compilados, assinados e com o ícone oficial **CameraPhone** já estão prontos na pasta [`dist/`](dist/) e na raiz do repositório:

| Arquivo | Descrição |
| :--- | :--- |
| [`CameraPhone.exe`](CameraPhone.exe) / [`dist/CameraPhone.exe`](dist/CameraPhone.exe) | **Instalador Zero-Config & Central de Logs** para Windows (com ícone oficial embutido) |
| [`CameraPhone.apk`](CameraPhone.apk) / [`dist/CameraPhone.apk`](dist/CameraPhone.apk) | **Aplicativo Android Nativo** (`com.cameraphone.app`, assinado v1/v2/v3 com ícone oficial) |
| [`dist/cameraphone.dll`](dist/cameraphone.dll) | **Plugin Nativo 64-bit OBS** (`CameraPhone 4K Pro`) |
| [`dist/cameraphone-vcam.dll`](dist/cameraphone-vcam.dll) | **Câmera Virtual DirectShow Nativa** para **NVIDIA Broadcast** (`CameraPhone Virtual Camera`) |

### Passo a Passo Zero-Config:
1. Execute [`CameraPhone.exe`](CameraPhone.exe) no PC: ao abrir, ele instala automaticamente o plugin [`cameraphone.dll`](dist/cameraphone.dll), a câmera virtual [`cameraphone-vcam.dll`](dist/cameraphone-vcam.dll), o [`CameraPhone.apk`](dist/CameraPhone.apk) e o motor ADB embutido.
2. Adicione a fonte de câmera **📷 `CameraPhone 4K Pro (Cabo USB / Wi-Fi IP)`** no OBS Studio:
   - **Modo Cabo USB (`Zero-Config Automático`):** Clique no botão **`Instalar APK no Celular (Somente via Cabo USB)`** dentro da própria janela de propriedades da câmera — ele instala o `CameraPhone.apk`, concede as permissões de câmera/microfone, abre o app no celular e inicia o vídeo sem precisar digitar IP nem senha!
   - **Modo Wi-Fi (`Conectar por IP na Rede`):** Digite o IP exibido no topo do app Android e o PIN de 6 dígitos. O PIN fica gravado de forma persistente e a proteção de taxa evita bloqueios indevidos na rede local.
   - **Redimensionamento, Corte e Filtros no OBS:** A fonte expõe as dimensões reais dinamicamente (`1920x1080` / `3840x2160`). Você pode redimensionar pelas alças vermelhas, cortar bordas segurando a tecla `Alt` e aplicar filtros nativos como *Máscara de Imagem / Mistura*, *Chroma Key* e *Correção de Cor*.
   - **Compatibilidade com NVIDIA Broadcast:** Clique em **`5. NVIDIA Broadcast`** no instalador `CameraPhone.exe` (ou use a câmera virtual DirectShow registrada). Abra o **NVIDIA Broadcast**, escolha **`CameraPhone Virtual Camera`** e aproveite todos os filtros de inteligência artificial: Remoção/Desfoque de Fundo, Enquadramento Automático, Contato Visual e Redução de Ruído de Vídeo!
   - **Central de Logs Integrada (`PC + APK`):** Tanto o aplicativo Android quanto o `CameraPhone.exe` e a janela de propriedades da câmera possuem visualizador de logs em tempo real (`%APPDATA%\CameraPhone\cameraphone.log` + `/v1/logs` do celular) para diagnóstico instantâneo.

---

## 🧪 Como Compilar e Rodar a Suíte de Testes

```powershell
# 1. Testes Unitários C++20 do Protocolo CamPhoneWire v1 e Segurança
python protocol/run_tests.py

# 2. Testes Unitários JUnit4 do App Android (SecurityGate, CamPhonePacket, CapabilityProbe, CamPhoneServer)
python android-app/build_and_test.py test

# 3. Compilar o APK Android assinado (CameraPhone.apk) com ícone oficial
python android-app/build_and_test.py assembleDebug

# 4. Testes C++20 e Compilação do Plugin (cameraphone.dll) + Instalador Desktop (CameraPhone.exe)
python obs-plugin/build_plugin.py all

# 5. Teste de Integração End-to-End (Handshake Bearer PIN, Rate-Limit, Patch de Controle e Stream Binário)
python tools/verify_e2e_stream.py

# 6. Verificação Dinâmica em Tempo de Execução (cameraphone.dll, CameraPhone.exe + Ícone e CameraPhone.apk)
python tools/verify_artifacts_runtime.py
```
