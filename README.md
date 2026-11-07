<div align="center">

<img src="desktop-app/cameraphone_logo.png" width="140" alt="CameraPhone Logo" />

# 📱🎥 CameraPhone — 4K60/120 HDR10 Pro

**Transforme seu smartphone Android em uma câmera de estúdio 4K60/120 FPS HDR 10-bit para o PC.**  
*100% Gratuito • Zero Marca D'água • Suporte Nativo a NVIDIA Broadcast • Redimensionamento e Filtros no OBS • Segurança Zero-Trust Anti-Hacker • Arquitetura Zero-Freeze*

[![Release](https://img.shields.io/github/v/release/ViniciusCostaGrillo/cameraphone?color=00E676&label=Release&style=for-the-badge&logo=github)](https://github.com/ViniciusCostaGrillo/cameraphone/releases/tag/v1.3.2)
[![Windows](https://img.shields.io/badge/Windows-Setup_Installer_%26_VCam-0078D6?style=for-the-badge&logo=windows&logoColor=white)](dist/CameraPhone-Setup.exe)
[![NVIDIA Broadcast](https://img.shields.io/badge/NVIDIA-Broadcast_Compatible-76B900?style=for-the-badge&logo=nvidia&logoColor=white)](dist/cameraphone-vcam.dll)
[![Android](https://img.shields.io/badge/Android-9.0%2B_(API_28--34)-3DDC84?style=for-the-badge&logo=android&logoColor=white)](dist/CameraPhone.apk)
[![C++20](https://img.shields.io/badge/C%2B%2B-20_Native-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](obs-plugin/)
[![Kotlin](https://img.shields.io/badge/Kotlin-Camera2_%2B_MediaCodec-7F52FF?style=for-the-badge&logo=kotlin&logoColor=white)](android-app/)

[🚀 **Baixar Instalador Windows (`CameraPhone-Setup.exe`)**](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/CameraPhone-Setup.exe) • [🤖 **App Android (`CameraPhone.apk`)**](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/CameraPhone.apk) • [🖥️ **Companion (`CameraPhone.exe`)**](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/CameraPhone.exe) • [🔌 **Plugin OBS (`cameraphone.dll`)**](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/cameraphone.dll) • [🎥 **Câmera Virtual NVIDIA (`cameraphone-vcam.dll`)**](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/cameraphone-vcam.dll)

</div>

---

## 📑 Sumário Executivo

1. [Por que o CameraPhone existe?](#-por-que-o-cameraphone-existe)
2. [Compatibilidade com NVIDIA Broadcast & Efeitos de IA](#-compatibilidade-com-nvidia-broadcast--efeitos-de-ia)
3. [Transformações Livres no OBS Studio (Redimensionar, Recorte e Máscaras)](#-transformações-livres-no-obs-studio-redimensionar-recorte-e-máscaras)
4. [Instalação Zero-Config e Modos de Conexão](#-instalação-zero-config-e-modos-de-conexão)
5. [Segurança Zero-Trust & Prevenção de Bloqueios (HTTP 429)](#-segurança-zero-trust--prevenção-de-bloqueios-http-429)
6. [Central Desktop & Instalador (`CameraPhone.exe`)](#-central-desktop--instalador-cameraphoneexe)
7. [Tabela de Downloads dos Binários Oficiais](#-tabela-de-downloads-dos-binários-oficiais)
8. [Como Compilar e Executar a Suíte de Testes](#-como-compilar-e-executar-a-suíte-de-testes)
9. [Base de Conhecimento e Engenharia Reversa](#-base-de-conhecimento-e-engenharia-reversa)

---

## 🎯 Por que o CameraPhone existe?

Durante a análise minuciosa de soluções proprietárias de webcam via celular ([consulte nosso relatório de engenharia reversa](docs/droidcam-reverse-engineering.md)), identificamos paywalls artificiais, servidores abertos sem segurança na rede doméstica e limitações no pipeline de renderização. O **CameraPhone** foi construído do zero (*clean-room*) para entregar excelência sem concessões:

| Recurso / Critério | ❌ Soluções Tradicionais (DroidCam / EpocCam) | ✅ CameraPhone (Este Projeto) |
| :--- | :--- | :--- |
| **Resolução 4K UHD (`3840x2160`)** | Bloqueada atrás de paywall | **100% Liberada Nativamente** com negociação automática via `CameraCharacteristics` |
| **Taxas de Quadros (FPS)** | 30 / 60 FPS fixos | **24 / 30 / 60 / 120 FPS High-Speed** (de acordo com o sensor) |
| **Codecs em Hardware** | Apenas AVC (H.264) e MJPEG legado | **`HEVC (H.265)`**, **`AV1`** e **`AVC (H.264)`** Zero-Copy via `Surface` (até 150 Mbps) |
| **HDR 10-bit (`HLG10` / `HDR10`)** | Não suportado (apenas 8-bit SDR) | **Suporte nativo a 10-bit `BT.2020` (`P010` / `I010`)** no Android 13+ |
| **NVIDIA Broadcast** | Incompatível ou requer emulação de terceiros | **Driver DirectShow COM Nativo** (`cameraphone-vcam.dll`) com detecção imediata |
| **Transformações no OBS Studio** | Problemas de redimensionamento e recorte | **Bounding Box Dinâmico:** Redimensionamento livre, corte com `Alt` e suporte a máscaras |
| **Lentes Físicas Múltiplas** | Apenas genéricas (traseira/frontal) | **Descoberta de todas as lentes reais** (Ultra-Wide 13mm, Wide 24mm, Telefoto 3x/5x/10x, Frontal) |
| **Controles Manuais Pro** | Bloqueados com HTTP 550 na versão gratuita | **100% Liberados:** Zoom Óptico/Híbrido, ISO, Obturador, EV, Kelvin WB e Lanterna |
| **Marca D'água** | Injetada no vídeo após alguns minutos | **Zero marca d'água** — sinal limpo e profissional |
| **Segurança na Rede** | Porta aberta sem senha na LAN | **Zero-Trust:** PIN CSPRNG com persistência, túnel USB loopback e proteção contra força bruta |
| **Custo** | Assinatura mensal/anual ou versão paga | **100% Gratuito & Open-Source** |

---

## 🌟 Compatibilidade com NVIDIA Broadcast & Efeitos de IA

O **CameraPhone** agora integra um driver DirectShow Virtual Camera nativo ([`cameraphone-vcam.dll`](file:///c:/Cameraphone%20Plugin%20OBS/dist/cameraphone-vcam.dll)) projetado especificamente para os requisitos estritos do aplicativo **NVIDIA Broadcast**:

```
[ Smartphone Android ]  ---(H.265 / AV1 @ 60 FPS)---> [ OBS Studio / Companion ]
                                                                │
                                                     (Memória Compartilhada IPC NV12 < 0.8ms)
                                                                ▼
                                                   [ cameraphone-vcam.dll ]
                                                   (CLSID_VideoInputDeviceCategory)
                                                                │
                                                                ▼
                                                   [ NVIDIA Broadcast (IA RTX) ]
                                                   ├── Remoção / Desfoque de Fundo
                                                   ├── Auto-Framing (Enquadramento)
                                                   ├── Eye Contact (Contato Visual)
                                                   └── Redução de Ruído de Vídeo
                                                                │
                                                                ▼
                                      [ OBS Studio / Discord / Zoom / Google Meet / Teams ]
```

### Principais Características da Câmera Virtual:
- **Detecção Imediata:** Registrada sob `CLSID_VideoInputDeviceCategory` (`{860BB310-5D01-11d0-BD3B-00A0C911CE86}`) com exposição da propriedade `PIN_CATEGORY_CAPTURE` via `IKsPropertySet`.
- **Formato NV12 Zero-Copy:** Quadros entregues no formato nativo exigido pelos Tensor Cores da NVIDIA, eliminando conversões pesadas de espaço de cor na CPU.
- **Standby Procedural 60 FPS:** Quando o smartphone não estiver transmitindo, a câmera virtual gera automaticamente um degradê escuro animado a 60 FPS, evitando que o NVIDIA Broadcast trave, congele ou interrompa a pipeline dos modelos de inteligência artificial.
- **Ativação em 1 Clique:** Basta clicar no botão **`5. NVIDIA Broadcast`** no executável [`CameraPhone.exe`](file:///c:/Cameraphone%20Plugin%20OBS/CameraPhone.exe) ou executar o script [`install.ps1`](file:///c:/Cameraphone%20Plugin%20OBS/install.ps1).

---

## 📐 Transformações Livres no OBS Studio (Redimensionar, Recorte e Máscaras)

Ao adicionar a fonte **`CameraPhone 4K Pro (Cabo USB / Wi-Fi IP)`** no OBS Studio, você desfruta de paridade total com webcams físicas convencionais:

- **Redimensionamento Livre pelas Alças:** A fonte informa dinamicamente sua resolução real ao OBS (`source_get_width` e `source_get_height`), habilitando as alças vermelhas nos vértices do canvas para redimensionamento e rotação.
- **Recorte Preciso com `Alt`:** Segure a tecla `Alt` e arraste qualquer uma das 4 bordas para cortar enquadramentos indesejados com precisão pixel a pixel.
- **Compatibilidade com Filtros Nacionais do OBS:**
  - **Máscara de Imagem / Mistura:** Aplique máscaras alfa, formas circulares ou molduras customizadas.
  - **Chroma Key / Color Key:** Remova fundo verde/azul com aceleração via shader.
  - **Correção de Cor & LUTs:** Ajuste gama, contraste, saturação e aplique tabelas de cor 3D LUT cinematográficas.

---

## 🚀 Instalação Zero-Config e Modos de Conexão

### 1. Conexão via Cabo USB (`Zero-Config Plug & Play`)
Ideal para gravações longas, lives profissionais e latência mínima sem oscilações de Wi-Fi:
1. Conecte o smartphone ao computador via cabo USB e certifique-se de que a *Depuração USB* está ativa.
2. Abra o OBS Studio e adicione a fonte **📷 `CameraPhone 4K Pro (Cabo USB / Wi-Fi IP)`**.
3. Na janela de propriedades, selecione o modo **Cabo USB** e clique em **`Instalar APK no Celular (Somente via Cabo USB)`**.
4. O plugin instala o aplicativo automaticamente no smartphone, concede permissões de câmera e microfone, inicializa o túnel ADB na porta `4748` e inicia o vídeo instantaneamente — **sem necessidade de digitar IP nem PIN**!

### 2. Conexão sem Fio via Wi-Fi (`Conectar por IP na Rede`)
Perfeito para setups sem fios, câmeras secundárias e mobilidade em estúdio:
1. Conecte o smartphone e o PC na mesma rede Wi-Fi (preferencialmente rede 5 GHz).
2. Abra o app **CameraPhone** no celular. O topo da tela exibirá o IP local (ex.: `192.168.1.120`) e um PIN de segurança de 6 dígitos (ex.: `492 815`).
3. No OBS Studio, selecione o modo **Wi-Fi**, insira o IP e o PIN exibidos e clique em OK.

---

## 🛡️ Segurança Zero-Trust & Prevenção de Bloqueios (HTTP 429)

Muitos utilitários de webcam expõem a porta de vídeo na rede local sem autenticação, permitindo que qualquer dispositivo na mesma rede acesse a câmera. O **CameraPhone** resolve isso com arquitetura de segurança rigorosa:

- **PIN Persistente no Android:** O PIN de 6 dígitos gerado é salvo de forma segura no `SharedPreferences` do dispositivo. Você não precisa redigitar o PIN no OBS a cada reabertura do aplicativo!
- **Bloqueio de Ataques de Força Bruta:** O servidor valida o cabeçalho `Authorization: Bearer <PIN>` com algoritmo de comparação em tempo constante (`MessageDigest.isEqual`). Após 5 tentativas inválidas, o IP é temporariamente bloqueado.
- **Desbloqueio Inteligente na Rede Local:** Sempre que o usuário abre ou desbloqueia o aplicativo no smartphone, a lista de bloqueios temporários é automaticamente resetada (`resetLockouts`), evitando travamentos acidentais por digitação incorreta.
- **Backoff Inteligente no OBS:** Se o plugin receber resposta `401 Unauthorized`, ele aguarda 5 segundos antes de retentar, evitando disparo em massa de requisições que causavam bloqueio por taxa (HTTP `429 Too Many Requests`).

---

## 🎛️ Central Desktop & Companion (`CameraPhone.exe`)

O executável portátil [`CameraPhone.exe`](file:///c:/Cameraphone%20Plugin%20OBS/CameraPhone.exe) foi completamente redesenhado com base no design system escuro do ícone oficial (*Dark Slate* `#12161A`, detalhes em Ciano `#00E5FF` e console escuro `#0D1117`). A interface agora é limpa e intuitiva:

```
+---------------------------------------------------------------------------------------+
|  CameraPhone 4K Pro                                                        [ Idioma ] |
|  Câmera Virtual NVIDIA Broadcast & Plugin OBS Zero-Config                             |
+---------------------------------------------------------------------------------------+
| [1. Instalar Tudo]  [2. Instalar APK no Celular]  [3. Testar Conexao]  [4. Ver Logs]  |
+---------------------------------------------------------------------------------------+
| CONSOLE DE DIAGNOSTICO EM TEMPO REAL                                                  |
| ┌───────────────────────────────────────────────────────────────────────────────────┐ |
| │ [ZERO-CONFIG INSTALADO COM SUCESSO]                                               │ |
| │   * Plugin OBS: C:\Program Files\obs-studio\obs-plugins\64bit\cameraphone.dll     │ |
| │   * Câmera Virtual: CameraPhone Virtual Camera (NVIDIA Broadcast pronta!)          │ |
| │   * Driver ADB & APK atualizados para conexao USB plug-and-play.                  │ |
| │ [Sistema] Transmissao USB direta para CameraPhone Virtual Camera ativa.           │ |
| │ [Dica] No OBS: adicione 'CameraPhone 4K Pro' e selecione Cabo USB ou preencha IP. │ |
| │ [Dica] No NVIDIA Broadcast: selecione 'CameraPhone Virtual Camera' na lista.      │ |
| └───────────────────────────────────────────────────────────────────────────────────┘ |
+---------------------------------------------------------------------------------------+
```

- **Transmissão Direta para o NVIDIA Broadcast por Cabo:** Ao conectar o smartphone via USB, o Companion detecta o aparelho e alimenta imediatamente a câmera virtual DirectShow sem precisar que o OBS Studio esteja aberto.
- **Botão 1 (Instalar Tudo):** Instala o plugin no OBS Studio, registra a câmera virtual DirectShow no Windows e copia o APK e ADB.
- **Botão 2 (Instalar APK no Celular):** Instala o aplicativo `CameraPhone.apk` no smartphone via cabo USB com todas as permissões concedidas em 1 clique.
- **Botão 3 (Testar Conexão):** Testa o link USB, abre o túnel na porta `4748` e exibe na tela todas as especificações do aparelho (modelo, SoC, sensores e lentes físicas).
- **Botão 4 (Ver Logs):** Abre os logs locais do PC e os logs internos do smartphone em tempo real no console amplo.
- **Botão de Idioma:** Alterne instantaneamente entre `Idioma` (PT) e `Language` (EN).
- **Modo Silencioso / Bandeja do Sistema:** Com a flag `--background`, o aplicativo inicia junto com o Windows minimizado na barra de tarefas (System Tray), deixando a câmera virtual sempre pronta para o NVIDIA Broadcast e Discord.

---

## 📦 Tabela de Downloads dos Binários Oficiais (v1.3.2)

Todos os binários oficiais pré-compilados, assinados criptograficamente e sem dependências externas estão disponíveis na release oficial:

| Arquivo | Tamanho | Descrição | Download Direto |
| :--- | :---: | :--- | :---: |
| 🚀 **`CameraPhone-Setup.exe`** | 8.3 MB | **Instalador Completo do Windows** (Instala tudo, cria atalhos, autostart e desinstalador `Desinstalar CameraPhone.exe`) | [Baixar Instalador](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/CameraPhone-Setup.exe) |
| 🖥️ **`CameraPhone.exe`** | 1.7 MB | **Companion Desktop Redesenhado & Alimentador Automático da VCam** | [Baixar EXE](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/CameraPhone.exe) |
| 📱 **`CameraPhone.apk`** | 865 KB | **Aplicativo Android Oficial Assinado** (Conexão automática com PC ao abrir) | [Baixar APK](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/CameraPhone.apk) |
| 🔌 **`cameraphone.dll`** | 1.7 MB | **Plugin Nativo OBS Studio 64-bit** (com redimensionamento e máscaras) | [Baixar DLL](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/cameraphone.dll) |
| 🎥 **`cameraphone-vcam.dll`** | 836 KB | **Câmera Virtual DirectShow 60 FPS para NVIDIA Broadcast** | [Baixar VCam](https://github.com/ViniciusCostaGrillo/cameraphone/releases/download/v1.3.2/cameraphone-vcam.dll) |

---

## 🧪 Como Compilar e Executar a Suíte de Testes

Caso deseje compilar o projeto a partir do código-fonte ou executar as validações automatizadas:

```powershell
# 1. Testes Unitários C++20 do Protocolo CamPhoneWire v1 e Memória Compartilhada
python protocol/run_tests.py

# 2. Testes Unitários do App Android (SecurityGate, CamPhonePacket, CapabilityProbe)
python android-app/build_and_test.py test

# 3. Compilação do APK Android assinado (CameraPhone.apk)
python android-app/build_and_test.py assembleDebug

# 4. Compilação do Plugin OBS (cameraphone.dll) e do Instalador (CameraPhone.exe)
python obs-plugin/build_plugin.py all

# 5. Compilação da Câmera Virtual DirectShow (cameraphone-vcam.dll)
g++ -O3 -shared -std=c++20 -static -static-libgcc -static-libstdc++ vcam/cameraphone-vcam.cpp vcam/cameraphone-vcam.def -lole32 -loleaut32 -luuid -o dist/cameraphone-vcam.dll

# 6. Suíte Completa de Verificação Dinâmica em Tempo de Execução (DLL, VCam, EXE e APK)
python tools/verify_artifacts_runtime.py
```

---

## 📚 Base de Conhecimento e Engenharia Reversa

Para uma compreensão aprofundada de todas as decisões arquiteturais, desmontagem de código, análise de pacotes de rede e soluções para desafios de baixo nível do Windows e Android, consulte os documentos detalhados:

- 📖 [**`KNOWLEDGE.md`** — Base de Conhecimento Técnica Unificada](KNOWLEDGE.md)
- 🔬 [**`docs/droidcam-reverse-engineering.md`** — Relatório Completo de Engenharia Reversa](docs/droidcam-reverse-engineering.md)
- 📐 [**`docs/superpowers/plans/2026-11-05-broadcast-and-obs-transforms.md`** — Especificação da Câmera Virtual e Transformações](docs/superpowers/plans/2026-11-05-broadcast-and-obs-transforms.md)

---

<div align="center">

Desenvolvido para a comunidade de criadores, streamers e produtores de conteúdo.  
**CameraPhone — O poder das câmeras profissionais de cinema e smartphones no seu setup.**

</div>
