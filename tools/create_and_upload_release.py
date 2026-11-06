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
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        if (DIST / "CameraPhone-Setup.exe").exists():
            zf.write(DIST / "CameraPhone-Setup.exe", "CameraPhone-Setup.exe")
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

RELEASE_NOTES = """## 📱🎥 CameraPhone — v1.3.0 (Novo Instalador Windows & Correção NVIDIA Broadcast)

Esta versão traz o novo **Instalador Completo do Windows (`CameraPhone-Setup.exe`)**, a correção definitiva para a *"Falha na funcionalidade"* no **NVIDIA Broadcast**, a transmissão direta por **Cabo USB** sem precisar abrir o OBS Studio, e o redesign completo da interface gráfica no estilo do ícone oficial!

---

### ✨ Principais Novidades & Destaques

#### 1. 🚀 Novo Instalador Windows Oficial (`CameraPhone-Setup.exe`)
- **Instalação em 1 Clique:** Instala automaticamente todos os componentes em `C:\\Program Files\\CameraPhone\\` e `C:\\Program Files\\obs-studio\\obs-plugins\\64bit\\`.
- **Inicia com o Windows:** Configura a chave `CameraPhone` em `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run` com a flag `--background`.
- **Bandeja do Sistema (System Tray):** Fica em segundo plano minimizado na bandeja, mantendo os servidores e a câmera virtual sempre prontos.
- **Atalhos Oficiais:** Cria atalhos na Área de Trabalho e no Menu Iniciar automaticamente.

#### 2. 🎥 Correção Definitiva do NVIDIA Broadcast ("Falha na funcionalidade")
- **Alinhamento de GUIDs COM DirectShow:** Corrigidas as GUIDs de `IMemInputPin` (`{56A8689D-0AD4-11CE-B03A-0020AF0BA770}`), `IMemAllocator` e flags de categoria no driver `cameraphone-vcam.dll`.
- **Ciclo de Vida do Alocador DirectShow:** Adicionadas chamadas explícitas a `m_pAllocator->Commit()` nas transições `Pause()` e `Run()`, e `Decommit()` em `Stop()`, eliminando o erro `VFW_E_NOT_COMMITTED (0x80040211)` e permitindo que os modelos TensorRT Maxine AI da NVIDIA inicializem perfeitamente.

#### 3. 🔌 Transmissão Direta por Cabo USB (Zero-Config Broadcast)
- O Companion (`CameraPhone.exe`) agora possui o alimentador contínuo em segundo plano: basta plugar o celular no cabo USB que o vídeo já é decodificado e alimentado na câmera virtual.
- Você pode usar o **NVIDIA Broadcast**, **Zoom**, **Discord**, **Teams** e **Google Meet** diretamente com o celular sem precisar abrir o OBS Studio!

#### 4. 🎨 Redesign Moderno Inspirado no Ícone & Correção de Idioma
- **Visual Dark Slate:** Janela em `#12161A`, botões ergonômicos no topo e console amplo de logs em tempo real em `#0D1117`.
- **Botão de Idioma Corrigido:** Texto estrito `Idioma` (em Português) ou `Language` (em Inglês).
- **Sem poluição visual:** Lacunas de IP/PIN removidas da tela principal (o cabo USB é automático e no OBS o IP pode ser configurado diretamente na fonte).

---

### 📦 Arquivos da Release

| Arquivo | Descrição |
| :--- | :--- |
| **`CameraPhone-Setup.exe`** | **Instalador Oficial Completo do Windows** (Recomendado para todos os usuários) |
| **`CameraPhone-v1.3.0-Windows.zip`** | Pacote portátil completo (Executável, Plugin OBS, VCam, APK e ADB) |
| **`CameraPhone.exe`** | Companion Desktop e Alimentador USB da Câmera Virtual |
| **`CameraPhone.apk`** | Aplicativo Android oficial assinado (v1/v2/v3) com PIN persistente |
| **`cameraphone.dll`** | Plugin nativo 64-bit para OBS Studio (`C:\\Program Files\\obs-studio\\obs-plugins\\64bit`) |
| **`cameraphone-vcam.dll`** | Câmera Virtual DirectShow compatível com NVIDIA Broadcast |
"""

def main():
    token = get_token()
    repo = "ViniciusCostaGrillo/cameraphone"
    tag = "v1.3.0"

    zip_path = DIST / "CameraPhone-v1.3.0-Windows.zip"
    create_bundle_zip(zip_path)

    assets_to_upload = [
        DIST / "CameraPhone-Setup.exe",
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
        "name": "CameraPhone v1.3.0 — Novo Instalador Windows & Correção NVIDIA Broadcast",
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
            with urllib.request.urlopen(get_req) as resp:
                rel_data = json.loads(resp.read().decode('utf-8'))
        else:
            raise

    release_id = rel_data["id"]
    upload_url_template = rel_data["upload_url"] # e.g. https://uploads.github.com/repos/.../assets{?name,label}
    upload_url_base = upload_url_template.split('{')[0]

    print(f"Release ID: {release_id}. Uploading assets...")

    # Fetch existing assets to avoid 422 duplicate asset errors
    assets_req = urllib.request.Request(
        f"https://api.github.com/repos/{repo}/releases/{release_id}/assets",
        headers={
            "Authorization": f"token {token}",
            "User-Agent": "CameraPhone-Release-Agent",
            "Accept": "application/vnd.github.v3+json"
        }
    )
    with urllib.request.urlopen(assets_req) as resp:
        existing_assets = {a["name"]: a["id"] for a in json.loads(resp.read().decode('utf-8'))}

    for asset_path in assets_to_upload:
        asset_name = asset_path.name
        if asset_name in existing_assets:
            print(f"Deleting existing asset {asset_name} (ID: {existing_assets[asset_name]})...")
            del_req = urllib.request.Request(
                f"https://api.github.com/repos/{repo}/releases/assets/{existing_assets[asset_name]}",
                headers={
                    "Authorization": f"token {token}",
                    "User-Agent": "CameraPhone-Release-Agent"
                },
                method="DELETE"
            )
            urllib.request.urlopen(del_req)

        print(f"Uploading {asset_name} ({asset_path.stat().st_size:,} bytes)...")
        content_type = "application/zip" if asset_name.endswith('.zip') else "application/octet-stream"

        upload_url = f"{upload_url_base}?name={asset_name}"
        with open(asset_path, 'rb') as f:
            data = f.read()

        up_req = urllib.request.Request(
            upload_url,
            data=data,
            headers={
                "Authorization": f"token {token}",
                "User-Agent": "CameraPhone-Release-Agent",
                "Content-Type": content_type,
                "Content-Length": str(len(data))
            },
            method="POST"
        )
        with urllib.request.urlopen(up_req) as resp:
            print(f"  ✓ {asset_name} uploaded successfully (HTTP {resp.status})")

    print("\n🎉 All release assets uploaded successfully!")
    print(f"URL: {rel_data.get('html_url', f'https://github.com/{repo}/releases/tag/{tag}')}")

if __name__ == "__main__":
    main()
