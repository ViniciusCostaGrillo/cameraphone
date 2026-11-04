# Engenharia Reversa Completa: DroidCam OBS (Desktop DLL + Android APKM)

> Este documento consolida a análise estática e arquitetural completa do plugin desktop `C:\Program Files\obs-studio\obs-plugins\64bit\droidcam-obs.dll` (v2.5.1, cliente `"251"`) e do pacote Android `C:\Program Files\obs-studio\data\obs-plugins\droidcam-obs\DroidCam APK.apkm` (`com.dev47apps.obsdroidcam` v10.0, build `83`), conduzida via **REA (Reverse Engineer Anything) + Ghidra 12.2**.

---

## 1. Visão Geral da Arquitetura

O ecossistema DroidCam OBS opera no modelo **Servidor no Celular / Cliente no OBS Studio**:
- **Android (`com.dev47apps.obsdroidcam`)**: Executa um serviço em segundo plano (`DroidcamService` + `com.dev47apps.obsdroidcam.core.Server`) que escuta conexões TCP na porta padrão **`4747`** e anuncia sua presença na LAN via mDNS (`_droidcamobs._tcp.local.`). Ele captura vídeo via `android.hardware.camera2` -> `Surface` -> `MediaCodec` (H.264 / HEVC 10-bit HLG) ou `libmjpg.so` (MJPEG via `libjpeg-turbo`) e áudio via `AudioRecord` -> `MediaCodec` (`audio/mp4a-latm` AAC).
- **Desktop (`droidcam-obs.dll`)**: Registra uma fonte assíncrona no OBS (`id = "droidcam_obs"`), descobre dispositivos via mDNS ou túnel USB (`adb.exe forward` / `usbmuxd`), realiza um handshake HTTP simples na porta `4747` e passa a ler pacotes binários com cabeçalho de **12 bytes** contendo o timestamp (`pts`) e o tamanho do frame (`length`), decodificando via FFmpeg (`avcodec-62.dll`) ou `libjpeg-turbo`.

---

## 2. Análise do Plugin Desktop (`droidcam-obs.dll`)

### 2.1 Identificação e Metadados do Binário
- **Caminho do Binário:** `C:\Program Files\obs-studio\obs-plugins\64bit\droidcam-obs.dll`
- **PDB Associado:** `C:\Program Files\obs-studio\obs-plugins\64bit\droidcam-obs.pdb`
- **SHA-256:** `3955e0513391cb4c5ab1c7b0a7370a4a7f784cd8d7d6a7ce8fd23183706d068f` (`2.165.760` bytes)
- **Arquitetura:** PE32+ `x86_64` (Windows, compilado com MSVC + linkagem estática da `libjpeg-turbo` `tj3*`)
- **Dependências Dinâmicas (`ev_8ebf4d162542618dfe4bb370c571e8586da67ce6688553f5e7845b45649f1fcb`):**
  - `obs.dll` (`obs_register_source_s`, `obs_source_output_video2`, `obs_source_output_audio`, `os_process_pipe_create`, etc.)
  - `obs-frontend-api.dll` (`obs_frontend_add_event_callback`, `obs_frontend_get_current_scene`, `obs_frontend_get_current_preview_scene`)
  - `avcodec-62.dll` & `avutil-60.dll` (FFmpeg para decodificação H.264, HEVC, AAC e aceleração por hardware D3D11VA/DXVA2/CUDA)
  - `Qt6Core.dll` & `Qt6Widgets.dll`
  - `WS2_32.dll` (Winsock2 TCP/UDP) & `w32-pthreads.dll`

### 2.2 Mapeamento das Funções Principais no Ghidra

| Endereço | Símbolo / Papel | Descrição Técnica |
| :--- | :--- | :--- |
| `0x18010b030` | `obs_module_load_impl` | Valida `avcodec_version()`, registra `droidcam_obs` source e callbacks de cena. |
| `0x18010b520` | `plugin_create` | Aloca `0x220` bytes para a instância da fonte, lê configurações (`resolution`, `video_format`, `activated`, `uhd_unlock`, `enable_audio`, `hw_accel`, `allow_hdr`) e inicia 4 threads (`video_thread`, `video_decode_thread`, `audio_thread`, `tally_thread`). |
| `0x18010bd60` | `plugin_get_properties` | Constrói a UI de propriedades no OBS; popula a lista de resoluções e codecs (`AVC/H.264`, `HEVC/H.265`, `MJPEG`). |
| `0x18010deb0` | `activate_clicked` | Callback do botão Activate/Deactivate; contém o **Easter Egg `"4K"`** para desbloquear resoluções > 1080p. |
| `0x18010dd70` | `refresh_devices_clicked` | Enumera dispositivos via `AdbMgr` (`adb devices`), `usbmuxd` (iOS) e `mdns` (`_droidcamobs._tcp.local.`). |
| `0x18010c4a0` | `resolve_and_connect` | Resolve o tipo de conexão (`1`=Wi-Fi IP, `2`=ADB USB, `3`=iOS USB, `4`=mDNS) e abre o socket TCP. |
| `0x18010cda0` | `video_thread` | Executa o handshake `GET /v5/video/...`, lê pacotes da rede e enfileira na fila de vídeo (`0x110`). |
| `0x18010c9d0` | `video_decode_thread` | Desenfileira pacotes de vídeo, decodifica via FFmpeg (`FUN_180100c30`) ou `libjpeg-turbo` (`FUN_180102340`) e entrega ao OBS via `obs_source_output_video2`. |
| `0x18010d300` | `audio_thread` | Executa `GET /v2/audio`, decodifica AAC via FFmpeg e entrega ao OBS via `obs_source_output_audio`. |
| `0x18010d700` | `tally_thread` | Envia `PUT /v1/tally/{program\|preview\|idle}/` ao celular quando a visibilidade da fonte muda. |
| `0x18010c6e0` | `read_framed_packet` | Lê o cabeçalho binário de 12 bytes (`uint64_t pts_be` + `uint32_t len_be`) e o payload do socket. |
| `0x1800fee20` | `AdbMgr::check_devices` | Executa `adb.exe devices` e `adb.exe -s <serial> shell getprop ro.product.model` via `os_process_pipe_create`. |
| `0x1800febb0` | `AdbMgr::forward_port` | Executa `adb.exe -s <serial> forward tcp:<local_port> tcp:<app_port>`. |
| `0x180105f50` | `mdns_discovery_thread` | Envia queries mDNS UDP na porta `5353` (`224.0.0.251` e `ff02::fb`) procurando `_droidcamobs._tcp.local.`. |
| `0x180100900` | `ffmpeg_decoder_init` | Inicializa `AVCodecContext` e testa aceleração de hardware (`D3D11VA`, `DXVA2`, `CUDA`). |
| `0x180100c30` | `ffmpeg_decode_video` | Envia `AVPacket`, recebe `AVFrame`, transfere da GPU para RAM (`av_hwframe_transfer_data`) e mapeia `P010`/HDR (`Rec. 2100 HLG/PQ`). |

---

## 3. Especificação do Protocolo de Rede e USB

### 3.1 Camadas de Transporte (`0x18010c4a0`)
1. **Wi-Fi Manual (`DeviceType = 1`):** Conecta via TCP a `<wifi_ip>:<app_port>` (porta padrão `4747`).
2. **USB Android via ADB (`DeviceType = 2`):**
   - Executa `adb.exe devices` e `adb.exe -s <serial> shell getprop ro.product.model`.
   - Mapeia uma porta local (`adb.exe -s <serial> forward tcp:<local_port> tcp:4747`) e conecta via TCP em `127.0.0.1:<local_port>`.
3. **USB iOS via `usbmuxd` (`DeviceType = 3`):** Usa `usbmuxd_connect`.
4. **Auto-Descoberta mDNS (`DeviceType = 4`):** Consulta `_droidcamobs._tcp.local.` via UDP multicast `5353`.

### 3.2 Handshake HTTP e Framing Binário de 12 Bytes
- **Vídeo (`0x18010cda0`):**
  `GET /v5/video/{avc|hevc|jpg}/{W}x{H}/port/{P}/os/win/obs/{ver}/client/251/hdr/{0|1}/nonce/5912/ HTTP/1.1\r\n\r\n`
- **Áudio (`0x18010d300`):**
  `GET /v2/audio HTTP/1.1\r\n\r\n`
- **Tally (`0x18010d700`):**
  `PUT /v1/tally/{program|preview|idle}/ HTTP/1.1\r\n\r\n`
- **Cabeçalho de Pacote (`0x18010c6e0`):**
  - Bytes `0..7`: `uint64_t pts` (Big-Endian). Se `pts == -1` (`0xFFFFFFFFFFFFFFFF`), o payload (máx 1024 bytes) é configuração de codec (`SPS/PPS/VPS` ou `AudioSpecificConfig` AAC de 2 bytes). Caso contrário, é o timestamp em microssegundos ($\mu\text{s}$), multiplicado por `1000` para nanossegundos no OBS.
  - Bytes `8..11`: `uint32_t length` (Big-Endian, máx `0xFFFFFF` = 16 MB).

---

## 4. Limitações Artificiais e Gargalos Identificados

1. **Lista Fixa de Resoluções e Easter Egg `"4K"` (`0x18010bd60` e `0x18010deb0`):**
   - O plugin possui apenas 7 resoluções fixas em `0x1802000a8` (`640x480` até `3840x2160`).
   - Por padrão (`uhd_unlock == false`), exibe apenas até `1920x1080`. Digitar `"4K"` ou `"4k"` no campo `wifi_ip` e clicar em Activate define `uhd_unlock = true`.
   - O plugin **não interroga o celular** sobre as lentes reais (Ultra-Wide, Wide, Telefoto), resoluções nativas do sensor ou taxas de quadros (60/120 FPS).
2. **Gargalo GPU -> RAM -> GPU (`0x180100c30`):**
   - Usa `av_hwframe_transfer_data` para copiar cada frame 4K decodificado na GPU de volta para a RAM antes de enviá-lo ao OBS (`obs_source_output_video2`).
3. **Bloqueio Comercial (`HTTP 550` e Marca d'água em `libmjpg.so`):**
   - Controles da câmera (`remote.htm`: Zoom, ISO, Shutter Speed, WB, Foco, Lentes) e uso sem marca d'água (`VideoEncoder.SetOverlayText()`) são bloqueados na versão gratuita.
