import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
TESTS = ROOT / "tests"
BUILD = ROOT / "build"

def find_compiler():
    for candidate in ["g++", "clang++"]:
        path = shutil.which(candidate)
        if path:
            return path
    winget_pkgs = Path.home() / "AppData/Local/Microsoft/WinGet/Packages"
    if winget_pkgs.exists():
        for gpp in winget_pkgs.rglob("g++.exe"):
            return str(gpp)
    raise RuntimeError("No C++20 compiler found.")

def find_windres():
    path = shutil.which("windres")
    if path:
        return path
    winget_pkgs = Path.home() / "AppData/Local/Microsoft/WinGet/Packages"
    if winget_pkgs.exists():
        for wr in winget_pkgs.rglob("windres.exe"):
            return str(wr)
    return None

def run_tests():
    BUILD.mkdir(parents=True, exist_ok=True)
    cxx = find_compiler()
    test_exe = BUILD / ("test_client_and_discovery.exe" if os.name == "nt" else "test_client_and_discovery")
    cmd = [
        cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-static",
        "-I", str(ROOT.parent / "protocol"),
        "-I", str(SRC),
        str(TESTS / "test_client_and_discovery.cpp"),
        str(SRC / "discovery-manager.cpp"),
        str(SRC / "stream-client.cpp"),
        "-o", str(test_exe)
    ]
    if os.name == "nt":
        cmd.append("-lws2_32")
    print("[Compile Tests]", " ".join(cmd))
    subprocess.run(cmd, check=True)
    print("[Run Tests]", str(test_exe))
    subprocess.run([str(test_exe)], check=True)

def build_dll():
    BUILD.mkdir(parents=True, exist_ok=True)
    cxx = find_compiler()
    out_dll = BUILD / "cameraphone.dll"
    cmd = [
        cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-shared", "-static",
        "-I", str(ROOT.parent / "protocol"),
        "-I", str(SRC),
        str(SRC / "plugin-main.cpp"),
        str(SRC / "cameraphone-source.cpp"),
        str(SRC / "discovery-manager.cpp"),
        str(SRC / "stream-client.cpp"),
        str(SRC / "media-decoder.cpp"),
        "-lws2_32",
        "-o", str(out_dll)
    ]
    print("[Build DLL]", " ".join(cmd))
    subprocess.run(cmd, check=True)
    print(f"[Plugin Built] {out_dll} ({out_dll.stat().st_size} bytes)")

    # Build DirectShow Virtual Camera DLL (with .def for COM exports)
    vcam_dir = ROOT.parent / "vcam"
    dist_dir = ROOT.parent / "dist"
    dist_dir.mkdir(parents=True, exist_ok=True)
    out_vcam = dist_dir / "cameraphone-vcam.dll"
    vcam_cmd = [
        cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-shared", "-static",
        "-I", str(ROOT.parent / "protocol"),
        str(vcam_dir / "cameraphone-vcam.cpp"),
        str(vcam_dir / "cameraphone-vcam.def"),
        "-lole32", "-loleaut32", "-luuid",
        "-o", str(out_vcam)
    ]
    print("[Build VCam DLL]", " ".join(vcam_cmd))
    subprocess.run(vcam_cmd, check=True)
    try:
        shutil.copy2(out_vcam, ROOT.parent / "cameraphone-vcam.dll")
    except PermissionError:
        pass
    print(f"[VCam Built] {out_vcam} ({out_vcam.stat().st_size} bytes)")

def build_desktop_exe():
    dist_dir = ROOT.parent / "dist"
    dist_dir.mkdir(parents=True, exist_ok=True)
    cxx = find_compiler()
    out_exe = dist_dir / "CameraPhone.exe"

    # Compile Windows icon resource (.rc -> .res.o) if windres is available
    res_obj = BUILD / "cameraphone_res.o"
    rc_file = ROOT.parent / "desktop-app" / "cameraphone.rc"
    windres = find_windres()
    extra_objs = []
    if windres and rc_file.exists():
        subprocess.run([windres, "-i", str(rc_file), "-o", str(res_obj)], cwd=str(rc_file.parent), check=True)
        extra_objs.append(str(res_obj))

    cmd = [
        cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-static",
        "-I", str(ROOT.parent / "protocol"),
        "-I", str(SRC),
        str(ROOT.parent / "desktop-app" / "cameraphone_companion.cpp"),
        str(SRC / "discovery-manager.cpp"),
        str(SRC / "stream-client.cpp"),
        str(SRC / "media-decoder.cpp"),
    ] + extra_objs + [
        "-lws2_32", "-lgdi32", "-luser32", "-lshell32", "-ldwmapi", "-lole32", "-loleaut32",
        "-o", str(out_exe)
    ]
    print("[Build EXE]", " ".join(cmd))
    subprocess.run(cmd, check=True)
    subprocess.run([str(out_exe), "--self-test"], check=True)

    # Copy DLL and APK alongside the EXE in dist/ and root (and remove legacy *-OBS.* files)
    for old_file in [
        dist_dir / "CameraPhone-OBS.exe",
        dist_dir / "CameraPhone-OBS.apk",
        dist_dir / "cameraphone-obs.dll",
        ROOT.parent / "CameraPhone-OBS.exe",
        ROOT.parent / "CameraPhone-OBS.apk",
    ]:
        if old_file.exists():
            old_file.unlink()

    dll_src = BUILD / "cameraphone.dll"
    apk_src = ROOT.parent / "android-app" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
    if dll_src.exists():
        shutil.copy2(dll_src, dist_dir / "cameraphone.dll")
    if apk_src.exists():
        shutil.copy2(apk_src, dist_dir / "CameraPhone.apk")
        shutil.copy2(apk_src, ROOT.parent / "CameraPhone.apk")
    shutil.copy2(out_exe, ROOT.parent / "CameraPhone.exe")

    # Bundle ADB inside dist/adb/ for 100% Zero-Config USB operation
    sdk_pt = Path.home() / "AppData/Local/Android/Sdk/platform-tools"
    if (sdk_pt / "adb.exe").exists():
        adb_dist = dist_dir / "adb"
        adb_dist.mkdir(parents=True, exist_ok=True)
        for fname in ["adb.exe", "AdbWinApi.dll", "AdbWinUsbApi.dll"]:
            if (sdk_pt / fname).exists():
                shutil.copy2(sdk_pt / fname, adb_dist / fname)
    print(f"[Desktop Companion Built] {out_exe} ({out_exe.stat().st_size} bytes)")

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    if mode == "test":
        run_tests()
    elif mode == "dll":
        build_dll()
    elif mode == "exe":
        build_desktop_exe()
    else:
        run_tests()
        build_dll()
        build_desktop_exe()
