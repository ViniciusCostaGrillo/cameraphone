# Language Selection (PT/EN) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Adicionar um botão de seleção de idioma (Português 🇧🇷 / Inglês 🇺🇸) no aplicativo Android, no Companion desktop do Windows e nas propriedades da fonte do OBS Studio, traduzindo todas as strings visuais e mantendo 100% inalterada a lógica de streaming, conexão e decodificação.

**Architecture:** Mapeamento de dicionário de strings bilíngues (Português/Inglês) com persistência local (SharedPreferences no Android, enum de estado em runtime no Companion Win32 e propriedade OBS Data no plugin) e alternância dinâmica em tempo de execução via botão na interface.

**Tech Stack:** Kotlin (Android UI), C++20 Win32 API (`SetWindowTextA`), C++20 OBS Studio Source ABI (`obs_property_list_add_string`).

**Spec:** [docs/cameraphone-architecture.md](file:///c:/Cameraphone%20Plugin%20OBS/docs/cameraphone-architecture.md)

## Global Constraints
- Nenhuma alteração nos protocolos de streaming (CamPhoneWire v1), sockets, encoders (MediaCodec) ou decodificadores (FFmpeg avcodec-62).
- Os padrões de conexão via Cabo USB e Wi-Fi devem continuar operando exatamente como estão hoje.
- O idioma padrão inicial deve respeitar a preferência ou iniciar em Português, com opção instantânea de alternar para Inglês.

## Review Focus
- Persistência no Android: ao fechar e reabrir o app, o idioma selecionado pelo usuário deve ser mantido via SharedPreferences.
- Redesenho de layout Win32: ao clicar no botão de idioma no Companion, todos os botões e caixas de texto devem atualizar seus textos imediatamente sem fechar a janela.
- Labels de status dinâmicos: mensagens de status em tempo real (como "Aguardando conexão" / "Waiting for connection") devem acompanhar o idioma ativo.

---

### Task 1: Dicionário Bilíngue e Botão de Idioma no Android (`MainActivity.kt`)

**Files:**
- Modify: `android-app/app/src/main/java/com/cameraphone/app/ui/MainActivity.kt`
- Test: `android-app/app/src/test/java/com/cameraphone/app/security/SecurityGateTest.kt`

- [ ] **Step 1: Criar enum e dicionário de strings no Android**
  Definir `enum class AppLanguage { PT, EN }` com mapa de strings para:
  - Tally Banner (Aguardando / Conectado / Standby)
  - IP e Cabo USB banner
  - Cartão de PIN e botão de gerar novo PIN
  - Switch de Bloqueio USB
  - Botão de Desconectar
  - Console de logs (título, limpar, compartilhar)

- [ ] **Step 2: Adicionar botão de alternância no Header da UI**
  Adicionar botão `langToggleBtn` no topo da `MainActivity` exibindo `🇧🇷 PT` ou `🇺🇸 EN`. Ao clicar, alterna o idioma, salva em `SharedPreferences("cameraphone_prefs")` e chama `applyLanguageStrings()`.

- [ ] **Step 3: Compilar e rodar testes do app Android**
  Executar: `python android-app/build_and_test.py test`
  Executar: `python android-app/build_and_test.py assembleDebug`
  Copiar `app-debug.apk` para `dist/CameraPhone.apk`.

---

### Task 2: Botão de Alternância e Tradução no Companion Win32 (`cameraphone_companion.cpp`)

**Files:**
- Modify: `desktop-app/cameraphone_companion.cpp`

- [ ] **Step 1: Criar dicionário de tradução e controle de idioma no Win32**
  Definir enum `enum class UiLanguage { PT, EN }` e função `update_ui_language(HWND hwnd, UiLanguage lang)` que atualiza os textos de:
  - Título da Janela e Cabeçalho
  - Labels de Host e PIN
  - Botões 1 ("Instalar Plugin" / "Install Plugin"), 2 ("Instalar APK (USB)" / "Install APK (USB)"), 3 ("Testar Conexão" / "Test Connection"), 4 ("Ver Logs" / "View Logs")
  - Botão de alternância `ID_BTN_LANG` com texto `[ 🇧🇷 PT | 🇺🇸 EN ]`

- [ ] **Step 2: Adicionar o botão de idioma na barra superior**
  Criar `CreateWindowExA` para `ID_BTN_LANG` posicionado em `(550, 14, 110, 26)`. No evento `WM_COMMAND`, alternar entre `PT` e `EN` e chamar `update_ui_language()`.

- [ ] **Step 3: Compilar `CameraPhone.exe` e validar**
  Compilar via MinGW C++20 com `-mwindows -static` e rodar `--self-test`.

---

### Task 3: Suporte a Idioma nas Propriedades do Plugin OBS (`cameraphone-source.cpp`)

**Files:**
- Modify: `obs-plugin/src/cameraphone-source.cpp`

- [ ] **Step 1: Adicionar propriedade de lista de Idiomas (Português / English)**
  Adicionar `obs_properties_add_list` com opções `"pt"` (Português) e `"en"` (English).
  Ao alterar o idioma, atualizar os rótulos dinâmicos dos botões e campos de configuração da fonte.

- [ ] **Step 2: Recompilar o plugin OBS (`cameraphone.dll`)**
  Executar `cmake --build obs-plugin/build -j4` e copiar para `dist/cameraphone.dll`.

---

### Task 4: Validação Geral e Testes E2E

- [ ] **Step 1: Executar `python tools/verify_artifacts_runtime.py`**
- [ ] **Step 2: Executar `python tools/test_live_plugin_e2e.py`**
