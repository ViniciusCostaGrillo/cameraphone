# Plano de Implementação — Compatibilidade NVIDIA Broadcast & Manipulação no OBS

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Adicionar compatibilidade com o NVIDIA Broadcast via driver DirectShow Virtual Camera nativo (`cameraphone-vcam.dll`) e habilitar redimensionamento nativo, corte com `Alt` e filtros (máscaras/cromaqui) no OBS Studio.

**Architecture:** 
1. No OBS Studio: Corrigir `source_get_width` e `source_get_height` em `cameraphone-source.cpp` para retornar a resolução dinâmica do quadro recebido (`1920x1080` / `3840x2160`), desbloqueando alças de transformação vermelhas, crop com Alt e filtros de shader do OBS.
2. Memória Compartilhada IPC: Criar `protocol/vcam_shmem.hpp` gerenciando um ring buffer mapeado em memória (`Local\CameraPhone_VCam_Shmem`) com sincronização por evento Win32 (`Local\CameraPhone_VCam_Event`).
3. Driver Virtual DirectShow: Implementar e compilar `cameraphone-vcam.dll` com suporte a `IBaseFilter`, `IPin`, `IAMStreamConfig` (NV12/YUY2/RGB24) e `IKsPropertySet` (`PIN_CATEGORY_CAPTURE`), registrando-se no Windows sob `CLSID_VideoInputDeviceCategory` para reconhecimento imediato no NVIDIA Broadcast, Discord, Zoom e Meet.
4. Companion: Integrar escrita na memória compartilhada no `CameraPhone.exe` e no plugin OBS, além de botão com 1 clique para registrar/desinstalar a Câmera Virtual.

**Tech Stack:** C++20, DirectShow COM ABI, Win32 Shared Memory Mapping, FFmpeg avcodec-62 (NV12/YUV420P), OBS Studio C ABI v30, LLVM-MinGW (UCRT).

**Spec:** [`cameraphone_broadcast_and_transform_design.md`](file:///C:/Users/Vinic/.gemini/antigravity/brain/81366343-3009-43bd-9352-50199e327ea4/cameraphone_broadcast_and_transform_design.md)

## Global Constraints
- Nenhuma dependência externa de runtime C++ MSVC redistributable — compilação estática (`-static -static-libgcc -static-libstdc++`).
- Latência de passagem pela memória compartilhada inferior a 1ms.
- O filtro DirectShow nunca pode travar ou congelar o NVIDIA Broadcast quando o celular for desconectado: deve exibir quadro de espera (Standby Card) a 60 FPS com mensagem informativa.
- Manter 100% de compatibilidade com as funcionalidades existentes (USB Zero-Config, Wi-Fi 6 dígitos, áudio e seletor de idiomas PT/EN).

## Review Focus
1. `source_get_width`/`source_get_height` retornando 0 antes do primeiro quadro: deve retornar `config.width`/`config.height` como fallback para evitar layout 0x0.
2. NVIDIA Broadcast exigindo `PIN_CATEGORY_CAPTURE`: se `IKsPropertySet` não estiver presente ou formatado corretamente, o Broadcast não reconhece o dispositivo como webcam.
3. Formato de pixel NV12: o NVIDIA Broadcast prefere NV12 para aceleração direta em TensorRT. A memória compartilhada deve repassar o buffer planar NV12 diretamente do `MediaDecoder` sem conversão de cor desnecessária na CPU.
4. Concorrência na memória compartilhada: proteção com mutex/spin-lock para garantir que leitor e escritor não acessem amostras em escrita parcial.
5. Permissões de registro COM: garantir que `CameraPhone.exe` execute o registro via API COM sem exigir comandos complexos no terminal.

---

### Task 1: Correção de Dimensões e Filtros no OBS Studio (`cameraphone-source.cpp`)

**Files:**
- Modify: `obs-plugin/src/cameraphone-source.cpp:200-260`
- Modify: `obs-plugin/src/cameraphone-source.cpp:610-630`

- [ ] **Step 1.1:** Adicionar campos atômicos `std::atomic<uint32_t> current_width{1920}` e `std::atomic<uint32_t> current_height{1080}` em `CameraPhoneSourceContext`.
- [ ] **Step 1.2:** No callback `on_frame` de decodificação de vídeo em `cameraphone-source.cpp`, atualizar `current_width.store(of.width)` e `current_height.store(of.height)`.
- [ ] **Step 1.3:** Atualizar `source_get_width` e `source_get_height` para retornar `ctx->current_width.load()` e `ctx->current_height.load()`.
- [ ] **Step 1.4:** Recompilar `cameraphone.dll` e instalar em `C:\Program Files\obs-studio\obs-plugins\64bit\cameraphone.dll`.
- [ ] **Step 1.5:** Testar carregamento no OBS e verificar se as dimensões da fonte são reportadas corretamente.

---

### Task 2: Memória Compartilhada IPC de Ultra-Baixa Latência (`protocol/vcam_shmem.hpp`)

**Files:**
- Create: `protocol/vcam_shmem.hpp`
- Create: `tests/test_vcam_shmem.cpp`

- [ ] **Step 2.1:** Implementar o cabeçalho `VCamSharedHeader` (`magic=0x43505643`, `width`, `height`, `fourcc`, `frame_index`, `timestamp_ns`, `buffer_size`).
- [ ] **Step 2.2:** Implementar `VCamSharedMemoryWriter` com `CreateFileMappingW` (`Local\CameraPhone_VCam_Shmem`), `MapViewOfFile`, e `CreateEventW` (`Local\CameraPhone_VCam_Event`).
- [ ] **Step 2.3:** Implementar `VCamSharedMemoryReader` com `OpenFileMappingW` e `OpenEventW` com timeout não-bloqueante.
- [ ] **Step 2.4:** Criar teste unitário `test_vcam_shmem.cpp` que simula gravação de 60 quadros 1080p NV12 por uma thread e consumo por outra thread com verificação de integridade e medição de latência (< 0.5ms).
- [ ] **Step 2.5:** Compilar e executar o teste com `g++`.

---

### Task 3: Driver de Câmera Virtual DirectShow (`vcam/cameraphone-vcam.cpp`)

**Files:**
- Create: `vcam/cameraphone-vcam.cpp`
- Create: `vcam/cameraphone-vcam.def`
- Create: `vcam/vcam-standby.hpp` (gerador do padrão gráfico de standby em NV12/YUY2)

- [ ] **Step 3.1:** Implementar o gerador de imagem de standby procedural (`vcam-standby.hpp`) que gera um fundo elegante degradê escuro com o logotipo e texto "CameraPhone 4K Pro — Conecte via USB ou Wi-Fi" em NV12/RGB.
- [ ] **Step 3.2:** Implementar as classes COM DirectShow:
  - `CVCamSourcePin`: herda interfaces DirectShow para pin de saída de vídeo, implementando `IAMStreamConfig` (enumeração de resoluções 1080p60, 4K60, 720p60) e `IKsPropertySet` (`PIN_CATEGORY_CAPTURE`).
  - `CVCamFilter`: herda `IBaseFilter`, gerencia os estados do filtro (`State_Stopped`, `State_Paused`, `State_Running`) e inicia a thread de entrega que consome da memória compartilhada ou renderiza o standby.
- [ ] **Step 3.3:** Implementar os pontos de entrada COM da DLL:
  - `DllGetClassObject`, `DllCanUnloadNow`.
  - `DllRegisterServer`: registra `CLSID_CameraPhoneVCam` sob `HKCR\CLSID\...` e cria a chave de categoria em `CLSID_VideoInputDeviceCategory` (`{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\...`) com o nome amigável `"CameraPhone Virtual Camera"`.
  - `DllUnregisterServer`: remove as chaves do registro.
- [ ] **Step 3.4:** Compilar `cameraphone-vcam.dll` 64-bit com `g++` e verificar exportações com `llvm-objdump`.

---

### Task 4: Integração de Publicação de Vídeo & Interface do Companion

**Files:**
- Modify: `obs-plugin/src/cameraphone-source.cpp` (publicar quadros no VCam se ativo)
- Modify: `desktop-app/cameraphone_companion.cpp` (botões de Câmera Virtual e publicação)

- [ ] **Step 4.1:** Adicionar instância de `VCamSharedMemoryWriter` no `CameraPhoneSourceContext` do plugin OBS e no `cameraphone_companion.cpp`.
- [ ] **Step 4.2:** No pipeline de decodificação de vídeo de ambos os aplicativos, publicar cada quadro decodificado na memória compartilhada.
- [ ] **Step 4.3:** No `CameraPhone.exe`, adicionar botão `"Instalar Câmera Virtual (NVIDIA Broadcast)"` e `"Desinstalar Câmera Virtual"` com feedback visual de status.
- [ ] **Step 4.4:** Recompilar `CameraPhone.exe` e `cameraphone.dll`.

---

### Task 5: Verificação Completa e Publicação da Release v1.2.0

**Files:**
- Test: `tools/test_vcam_registration.py`
- Test: `tools/verify_artifacts_runtime.py`

- [ ] **Step 5.1:** Criar e executar script de verificação `tools/test_vcam_registration.py` que testa o registro COM, a enumeração pelo Windows e a injeção de quadros simulada.
- [ ] **Step 5.2:** Testar no OBS Studio o redimensionamento, o corte com `Alt` e a aplicação do filtro de Máscara de Imagem.
- [ ] **Step 5.3:** Testar no NVIDIA Broadcast: verificar se `"CameraPhone Virtual Camera"` aparece na lista de câmeras e se os efeitos RTX podem ser aplicados com sucesso.
- [ ] **Step 5.4:** Atualizar pacotes na pasta `dist/` (`CameraPhone.apk`, `CameraPhone.exe`, `cameraphone.dll`, `cameraphone-vcam.dll`).
- [ ] **Step 5.5:** Commit e publicação da release oficial `v1.2.0` no GitHub.
