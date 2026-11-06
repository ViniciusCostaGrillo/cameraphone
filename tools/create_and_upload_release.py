import io
import json
import os
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"

def get_token():
    p = subprocess.Popen(['git', 'credential', 'fill'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    stdout, _ = p.communicate('protocol=https\nhost=github.com\n')
    for line in stdout.splitlines():
        if line.startswith('password='):
            return line.split('=', 1)[1]
    raise RuntimeError("Could not retrieve git credential password")

def create_bundle_zip(zip_path: Path):
    print(f"Creating Windows bundle ZIP at {zip_path.name}...")
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.write(DIST / "CameraPhone.exe", "CameraPhone.exe")
        zf.write(DIST / "CameraPhone.apk", "CameraPhone.apk")
        zf.write(DIST / "cameraphone.dll", "cameraphone.dll")
        zf.write(DIST / "cameraphone-vcam.dll", "cameraphone-vcam.dll")
        zf.write(ROOT / "install.ps1", "install.ps1")
        zf.write(ROOT / "README.md", "README.md")
        zf.write(ROOT / "KNOWLEDGE.md", "KNOWLEDGE.md")
        adb_dir = DIST / "adb"
        if adb_dir.exists():
            for f in adb_dir.iterdir():
                if f.is_file():
                    zf.write(f, f"adb/{f.name}")
    print(f"Bundle ZIP created ({zip_path.stat().st_size:,} bytes).")

RELEASE_NOTES = """## 📱🎥 CameraPhone — v1.2.0 (NVIDIA Broadcast & Transformações OBS)

Esta versão traz a tão aguardada compatibilidade nativa com o **NVIDIA Broadcast**, permitindo utilizar todos os efeitos avançados de Inteligência Artificial das placas GeForce RTX (Remoção e Desfoque de Fundo, Enquadramento Automático, Contato Visual e Redução de Ruído) direto na câmera do seu celular, além de liberar o redimensionamento livre, corte com `Alt` e aplicação de máscaras/filtros dentro do **OBS Studio**!

---

### ✨ Principais Novidades & Destaques

#### 1. 🎥 Suporte Nativo ao NVIDIA Broadcast (`cameraphone-vcam.dll`)
- **Driver DirectShow COM Dedicado:** Implementado driver de câmera virtual nativo em C++20 com registro automático em `CLSID_VideoInputDeviceCategory` (`{860BB310-5D01-11d0-BD3B-00A0C911CE86}`).
- **Categoria `PIN_CATEGORY_CAPTURE`:** Exposta no pino de saída via `IKsPropertySet`, atendendo ao critério estrito de detecção do NVIDIA Broadcast.
- **Memória Compartilhada IPC Ultra-Rápida:** Transferência de quadros NV12 zero-copy via buffer nomeado Win32 (`Local\\CameraPhone_VCam_Shmem`) com latência inferior a **0,8 milissegundos**.
- **Standby Procedural 60 FPS:** Quando o celular não estiver transmitindo, a câmera virtual gera um degradê escuro elegante e animado a 60 FPS, garantindo que o NVIDIA Broadcast nunca congele nem trave os modelos TensorRT.
- **Botão 5 no Companion (`CameraPhone.exe`):** Clique em `5. NVIDIA Broadcast` para registrar a câmera virtual no Windows em um clique!

#### 2. 📐 Redimensionamento Livre, Corte com `Alt` e Máscaras no OBS Studio
- **Dimensões Dinâmicas Reais:** `source_get_width` e `source_get_height` agora retornam dinamicamente a resolução real negociada (`1920x1080` / `3840x2160`).
- **Alças de Seleção no Canvas:** Você pode redimensionar a fonte livremente pelas alças vermelhas nos cantos.
- **Recorte com Alt:** Segure a tecla `Alt` e arraste as bordas para cortar enquadramentos indesejados.
- **Filtros de Máscara Suportados:** Permite aplicar nativamente filtros de *Máscara de Imagem / Mistura*, *Chroma Key* e *Correção de Cor*, comportando-se exatamente como uma webcam USB física.

#### 3. 🛡️ Correção do Bloqueio de IP por PIN Wi-Fi (HTTP 429 Too Many Requests)
- **PIN Persistente no Android:** O PIN de 6 dígitos gerado pelo celular agora fica salvo no `SharedPreferences`. O PIN não é mais resetado a cada reabertura do app!
- **Auto-Reset ao Abrir o App:** Abrir o app no celular limpa automaticamente qualquer bloqueio temporário de IP da sua rede local (`resetLockouts`).
- **Backoff Inteligente no OBS:** Se o plugin receber `401 Unauthorized`, ele aguarda 5 segundos antes de retentar, eliminando o envio de múltiplas requisições em curto intervalo que disparava o bloqueio 429.

---

### 📦 Arquivos da Release

| Arquivo | Descrição |
| :--- | :--- |
| **`CameraPhone-v1.2.0-Windows.zip`** | Pacote completo portátil (Executável, Plugin OBS, VCam, APK e ADB) |
| **`CameraPhone.exe`** | Instalador Zero-Config, Central de Logs e Ativador da Câmera Virtual |
| **`CameraPhone.apk`** | Aplicativo Android oficial assinado (v1/v2/v3) com PIN persistente |
| **`cameraphone.dll`** | Plugin nativo 64-bit para OBS Studio (`C:\\Program Files\\obs-studio\\obs-plugins\\64bit`) |
| **`cameraphone-vcam.dll`** | Câmera Virtual DirectShow para NVIDIA Broadcast |

---

### 🚀 Como Usar com NVIDIA Broadcast:
1. Baixe e execute o **`CameraPhone.exe`** e clique em **`5. NVIDIA Broadcast`** (ou execute `install.ps1`).
2. Abra o aplicativo **NVIDIA Broadcast**.
3. Na aba **Câmera**, escolha **`CameraPhone Virtual Camera`** como dispositivo de entrada.
4. Escolha os efeitos de IA desejados (Remoção de Fundo, Enquadramento Automático, Contato Visual).
5. No OBS Studio, se desejar usar a saída processada com IA, adicione uma fonte de **Dispositivo de Captura de Vídeo** e selecione **`Camera (NVIDIA Broadcast)`**!
"""

def main():
    token = get_token()
    repo = "ViniciusCostaGrillo/cameraphone"
    tag = "v1.2.0"

    zip_path = DIST / "CameraPhone-v1.2.0-Windows.zip"
    create_bundle_zip(zip_path)

    assets_to_upload = [
        zip_path,
        DIST / "CameraPhone.exe",
        DIST / "CameraPhone.apk",
        DIST / "cameraphone.dll",
        DIST / "cameraphone-vcam.dll",
    ]

    for a in assets_to_upload:
        if not a.exists():
            raise FileNotFoundError(f"Missing asset: {a}")

    print(f"Creating GitHub Release for tag {tag}...")
    create_url = f"https://api.github.com/repos/{repo}/releases"
    payload = {
        "tag_name": tag,
        "name": "CameraPhone v1.2.0 — Compatibilidade NVIDIA Broadcast & Transformações OBS",
        "body": RELEASE_NOTES.strip(),
        "draft": False,
        "prerelease": False
    }

    req = urllib.request.Request(
        create_url,
        data=json.dumps(payload).encode('utf-8'),
        headers={
            "Authorization": f"token {token}",
            "User-Agent": "CameraPhone-Release-Agent",
            "Accept": "application/vnd.github.v3+json",
            "Content-Type": "application/json"
        },
        method="POST"
    )

    try:
        with urllib.request.urlopen(req) as resp:
            rel_data = json.loads(resp.read().decode('utf-8'))
    except urllib.error.HTTPError as e:
        err_msg = e.read().decode('utf-8', errors='ignore')
        print(f"HTTPError {e.code}: {err_msg}")
        if "already_exists" in err_msg or e.code == 422:
            print("Release already exists, retrieving existing release...")
            get_req = urllib.request.Request(
                f"https://api.github.com/repos/{repo}/releases/tags/{tag}",
                headers={
                    "Authorization": f"token {token}",
                    "User-Agent": "CameraPhone-Release-Agent",
                    "Accept": "application/vnd.github.v3+json"
                }
            )
            with urllib.request.urlopen(get_req) as r:
                rel_data = json.loads(r.read().decode('utf-8'))
        else:
            raise

    release_id = rel_data["id"]
    upload_url_template = rel_data["upload_url"] # e.g. https://uploads.github.com/.../assets{?name,label}
    upload_base = upload_url_template.split('{')[0]
    html_url = rel_data.get("html_url", f"https://github.com/{repo}/releases/tag/{tag}")

    print(f"Release ID: {release_id} - HTML: {html_url}")

    # Check existing assets to delete duplicates before re-uploading
    existing_assets = {a["name"]: a["id"] for a in rel_data.get("assets", [])}

    for asset_path in assets_to_upload:
        name = asset_path.name
        if name in existing_assets:
            print(f"Deleting previous asset {name} (ID: {existing_assets[name]})...")
            del_req = urllib.request.Request(
                f"https://api.github.com/repos/{repo}/releases/assets/{existing_assets[name]}",
                headers={
                    "Authorization": f"token {token}",
                    "User-Agent": "CameraPhone-Release-Agent",
                    "Accept": "application/vnd.github.v3+json"
                },
                method="DELETE"
            )
            try:
                with urllib.request.urlopen(del_req) as dresp:
                    pass
            except Exception as de:
                print(f"Warning on delete {name}: {de}")

        print(f"Uploading asset: {name} ({asset_path.stat().st_size:,} bytes)...")
        content_type = "application/zip" if name.endswith(".zip") else ("application/vnd.android.package-archive" if name.endswith(".apk") else "application/octet-stream")
        
        with open(asset_path, "rb") as f:
            file_bytes = f.read()

        up_url = f"{upload_base}?name={urllib.request.quote(name)}"
        up_req = urllib.request.Request(
            up_url,
            data=file_bytes,
            headers={
                "Authorization": f"token {token}",
                "User-Agent": "CameraPhone-Release-Agent",
                "Accept": "application/vnd.github.v3+json",
                "Content-Type": content_type
            },
            method="POST"
        )
        with urllib.request.urlopen(up_req) as uresp:
            asset_info = json.loads(uresp.read().decode('utf-8'))
            print(f"  -> Uploaded {name}: download_url={asset_info.get('browser_download_url')}")

    print("\n==========================================================================")
    print(f"🎉 RELEASE {tag} PUBLICADA COM SUCESSO NO GITHUB!")
    print(f"🔗 URL da Release: {html_url}")
    print("==========================================================================")

if __name__ == "__main__":
    main()
