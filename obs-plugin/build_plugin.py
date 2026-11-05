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
    out_dll = BUILD / "cameraphone-obs.dll"
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

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    if mode == "test":
        run_tests()
    elif mode == "dll":
        build_dll()
    else:
        run_tests()
        build_dll()
