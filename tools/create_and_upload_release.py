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

RELEASE_NOTES = """## 📱🎥 CameraPhone 4K Pro — v1.4.0 (Wireless Wi-Fi & USB NVIDIA Broadcast + English Default UI)

🌐 **Official Website:** [https://viniciuscostagrillo.github.io/cameraphone/](https://viniciuscostagrillo.github.io/cameraphone/)

---

# 🇺🇸 English Release Notes

**CameraPhone 4K Pro v1.4.0** introduces **Zero-Config Wireless Wi-Fi + USB streaming for NVIDIA Broadcast**, sets **English (`EN`) as the default interface language** across all apps (with an instant **`Language: EN / PT`** toggle button), and launches the official GitHub Pages project website!

### ✨ Key New Features in v1.4.0
1. **📶 Automatic Wi-Fi + USB Streaming for NVIDIA Broadcast:**
   - `CameraPhone.exe` now features a hybrid USB + Wi-Fi auto-discovery engine (`DiscoveryManager::discover_wifi_devices`).
   - When connected via USB, it streams with zero-config over ADB loopback (`127.0.0.1:4748`) and automatically caches your phone's `wlan0` Wi-Fi IP.
   - Unplug the USB cable at any time: `CameraPhone.exe` automatically discovers your phone on the local Wi-Fi network (via cached IP, UDP beacon on port `4749`, or fast non-blocking `/24` TCP scan) and continues feeding **`CameraPhone Virtual Camera`** in **NVIDIA Broadcast** wirelessly without needing to type any IP or PIN!
2. **🌐 English Default UI + Instant `Language: EN / PT` Toggle:**
   - All applications (`CameraPhone-Setup.exe`, `CameraPhone.exe`, `CameraPhone.apk`, and the OBS Studio plugin) now default to **English (`EN`)** with a top-right **`Language: EN / PT`** button to switch to Portuguese (`PT-BR`) at any time.
3. **🛡️ Private LAN Zero-Config Security (`SecurityGate.kt`):**
   - Authorizes zero-config connections from loopback (`127.0.0.1`) and RFC1918 private LAN subnets (`192.168.*`, `10.*`, `172.16-31.*`) while blocking any external/public WAN IP.

### 📦 Official Downloads (`v1.4.0`)
| File | Description |
| :--- | :--- |
| **`CameraPhone-Setup.exe`** | **Full Windows Installer** (OBS Plugin, NVIDIA Broadcast Virtual Camera, Companion App, ADB, Shortcuts & Uninstaller) |
| **`CameraPhone-v1.4.0-Windows.zip`** | Portable All-in-One Windows Bundle |
| **`CameraPhone.exe`** | Standalone Desktop Companion & Hybrid USB/Wi-Fi Virtual Camera Feeder |
| **`CameraPhone.apk`** | Signed Android App (Auto-connects via USB & Wi-Fi, OLED Blackout Mode, EN/PT Toggle) |
| **`cameraphone.dll`** | Native 64-bit OBS Studio Plugin (`C:\\Program Files\\obs-studio\\obs-plugins\\64bit`) |
| **`cameraphone-vcam.dll`** | 64-bit DirectShow 4K60 Virtual Camera for NVIDIA Broadcast |

---

# 🇧🇷 Notas de Lançamento (Português)

O **CameraPhone 4K Pro v1.4.0** traz o suporte **Zero-Config Híbrido (Cabo USB + Wi-Fi Automático) para o NVIDIA Broadcast**, define o **Inglês (`EN`) como idioma padrão** em todos os aplicativos (com botão **`Language: EN / PT`** no topo direito para alternar para Português), e lança a página oficial do projeto no GitHub Pages!

### ✨ Destaques da Versão v1.4.0
1. **📶 Transmissão Automática via Wi-Fi e Cabo USB no NVIDIA Broadcast:**
   - Use no **NVIDIA Broadcast** tanto pelo cabo USB quanto sem fio pela rede Wi-Fi, sem precisar digitar IP ou PIN!
2. **🌐 Idioma Inglês como Padrão + Botão `Language: EN / PT`:**
   - Todos os aplicativos (`CameraPhone-Setup.exe`, `CameraPhone.exe`, `CameraPhone.apk` e plugin OBS) iniciam em Inglês por padrão e possuem botão rápido para alternar para Português.
"""

def enable_github_pages(repo: str, token: str):
    url = f"https://api.github.com/repos/{repo}/pages"
    payload = json.dumps({"source": {"branch": "main", "path": "/docs"}}).encode("utf-8")
    for method in ("POST", "PUT"):
        req = urllib.request.Request(
            url,
            data=payload,
            headers={
                "Authorization": f"token {token}",
                "User-Agent": "CameraPhone-Release-Agent",
                "Accept": "application/vnd.github+json",
                "Content-Type": "application/json"
            },
            method=method
        )
        try:
            with urllib.request.urlopen(req) as resp:
                print(f"GitHub Pages configured ({method} -> HTTP {resp.status}): https://viniciuscostagrillo.github.io/cameraphone/")
                return
        except urllib.error.HTTPError as e:
            err = e.read().decode("utf-8", errors="ignore")
            if e.code == 409 and method == "POST":
                continue
            print(f"GitHub Pages {method} response ({e.code}): {err}")

def main():
    token = get_token()
    repo = "ViniciusCostaGrillo/cameraphone"
    tag = "v1.4.0"

    enable_github_pages(repo, token)

    zip_path = DIST / f"CameraPhone-{tag}-Windows.zip"
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
        "name": "CameraPhone 4K Pro v1.4.0 — Wireless Wi-Fi & USB NVIDIA Broadcast + English/PT UI",
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
    upload_url_template = rel_data["upload_url"]
    upload_url_base = upload_url_template.split('{')[0]

    print(f"Release ID: {release_id}. Uploading assets...")

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
