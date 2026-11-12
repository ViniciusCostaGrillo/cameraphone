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

RELEASE_NOTES = """## 📱🎥 CameraPhone 4K Pro — v1.5.0 (True 4K Studio Pipeline & 4K30 Sensor Support)

🌐 **Official Website:** [https://viniciuscostagrillo.github.io/cameraphone/](https://viniciuscostagrillo.github.io/cameraphone/)

---

# 🇺🇸 English Release Notes

**CameraPhone 4K Pro v1.5.0** delivers a true end-to-end **4K UHD (`3840x2160`)** studio pipeline across **NVIDIA Broadcast**, **OBS Studio**, and **Android**, eliminating silent 1080p downgrades and low-bitrate compression artifacts.

### ✨ What's New in v1.5.0
1. **🎥 True 4K Default Across NVIDIA Broadcast & OBS Studio:**
   - `CameraPhone.exe` (Desktop Companion) now requests `3840x2160` instead of hardcoded `1920x1080`.
   - `cameraphone-vcam.dll` (DirectShow Virtual Camera) advertises `4K60 NV12` and `4K30 NV12` as primary formats and uses smooth bilinear Y-plane scaling instead of nearest-neighbor.
   - `cameraphone.dll` (OBS Studio Plugin) defaults new sources to `4K UHD (3840x2160)`.
2. **📱 Smart 4K30 Device Detection & Studio Bitrates (Android):**
   - `Camera2Controller` inspects `getOutputMinFrameDuration` per resolution so 4K30-only smartphones negotiate `3840x2160 @ 30fps` cleanly instead of dropping to 1080p.
   - Raised encoding bitrates: **100 Mbps** (USB 4K) / **50 Mbps** (Wi-Fi 4K) and **35 Mbps** (USB <=1080p) / **20 Mbps** (Wi-Fi <=1080p).
   - Fixed `HardwareVideoEncoder` fallback ladder to try `4K @ 30fps` and `1440p` with high bitrate before falling back to 1080p.

---

# 🇧🇷 Notas de Lançamento (Português)

O **CameraPhone 4K Pro v1.5.0** entrega um pipeline **4K UHD (`3840x2160`) real de ponta a ponta** no **NVIDIA Broadcast**, **OBS Studio** e **Android**, eliminando quedas silenciosas para 1080p e artefatos de compressão.

### ✨ Novidades da v1.5.0
1. **🎥 4K Real como Padrão no NVIDIA Broadcast e OBS Studio:**
   - `CameraPhone.exe` agora solicita `3840x2160` em vez de `1920x1080` fixo.
   - `cameraphone-vcam.dll` prioriza `4K60 NV12` e `4K30 NV12` e usa escalonamento bilinear suave no plano Y.
   - `cameraphone.dll` define `4K UHD (3840x2160)` como padrão ao adicionar a fonte no OBS.
2. **📱 Detecção de Celulares 4K30 & Bitrates de Estúdio (Android):**
   - Detecta o FPS máximo suportado por resolução (`getOutputMinFrameDuration`) para que celulares `4K@30fps` negociem `3840x2160 @ 30fps` sem cair para 1080p.
   - Bitrates elevados: **100 Mbps** (USB 4K) / **50 Mbps** (Wi-Fi 4K) e **35 Mbps** (USB <=1080p) / **20 Mbps** (Wi-Fi <=1080p).
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
    tag = "v1.5.0"

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
        "name": "CameraPhone 4K Pro v1.5.0 — True 4K Studio Pipeline & 4K30 Sensor Support",
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
