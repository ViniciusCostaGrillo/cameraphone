import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
INSTALLER_DIR = ROOT / "installer"

def find_compiler():
    for candidate in ["g++", "clang++"]:
        path = shutil.which(candidate)
        if path:
            return path
    winget_pkgs = Path.home() / "AppData/Local/Microsoft/WinGet/Packages"
    if winget_pkgs.exists():
        for gpp in winget_pkgs.rglob("g++.exe"):
            return str(gpp)
    raise RuntimeError("No C++ compiler found.")

def find_windres():
    path = shutil.which("windres")
    if path:
        return path
    winget_pkgs = Path.home() / "AppData/Local/Microsoft/WinGet/Packages"
    if winget_pkgs.exists():
        for wr in winget_pkgs.rglob("windres.exe"):
            return str(wr)
    return None

def package_payload():
    payload_zip = INSTALLER_DIR / "payload.zip"
    if payload_zip.exists():
        payload_zip.unlink()

    files_to_pack = [
        ("CameraPhone.exe", DIST / "CameraPhone.exe"),
        ("cameraphone.dll", DIST / "cameraphone.dll"),
        ("cameraphone-vcam.dll", DIST / "cameraphone-vcam.dll"),
        ("CameraPhone.apk", DIST / "CameraPhone.apk"),
    ]

    adb_files = [
        ("adb/adb.exe", DIST / "adb" / "adb.exe"),
        ("adb/AdbWinApi.dll", DIST / "adb" / "AdbWinApi.dll"),
        ("adb/AdbWinUsbApi.dll", DIST / "adb" / "AdbWinUsbApi.dll"),
    ]

    print("[Payload] Creating payload.zip...")
    with zipfile.ZipFile(payload_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for arcname, path in files_to_pack:
            if not path.exists():
                raise FileNotFoundError(f"Missing required payload file: {path}")
            print(f"  + {arcname} ({path.stat().st_size} bytes)")
            zf.write(path, arcname)

        for arcname, path in adb_files:
            if path.exists():
                print(f"  + {arcname} ({path.stat().st_size} bytes)")
                zf.write(path, arcname)

    print(f"[Payload] payload.zip ready: {payload_zip.stat().st_size} bytes")

def compile_installer():
    cxx = find_compiler()
    windres = find_windres()
    if not windres:
        raise RuntimeError("windres.exe not found.")

    res_obj = INSTALLER_DIR / "installer_res.o"
    rc_file = INSTALLER_DIR / "installer.rc"

    print("[Resource] Compiling installer.rc with windres...")
    subprocess.run([windres, "-i", str(rc_file), "-o", str(res_obj)], cwd=str(INSTALLER_DIR), check=True)

    out_exe = DIST / "CameraPhone-Setup.exe"
    cmd = [
        cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-static",
        str(INSTALLER_DIR / "installer_main.cpp"),
        str(res_obj),
        "-lshell32", "-lole32", "-loleaut32", "-luuid", "-ldwmapi", "-lcomctl32", "-lgdi32", "-luser32",
        "-o", str(out_exe)
    ]
    print("[Compile Installer]", " ".join(cmd))
    subprocess.run(cmd, check=True)

    # Copy to workspace root for instant access
    root_setup = ROOT / "CameraPhone-Setup.exe"
    shutil.copy2(out_exe, root_setup)
    print(f"[Success] Setup executable created:")
    print(f"  * {out_exe} ({out_exe.stat().st_size} bytes)")
    print(f"  * {root_setup} ({root_setup.stat().st_size} bytes)")

def main():
    package_payload()
    compile_installer()

if __name__ == "__main__":
    main()
