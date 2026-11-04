import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def find_compiler():
    for candidate in ["g++", "clang++"]:
        path = shutil.which(candidate)
        if path:
            return [path, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror"]
    # Check local toolchains or winget/mingw paths
    winget_pkgs = Path.home() / "AppData/Local/Microsoft/WinGet/Packages"
    if winget_pkgs.exists():
        for gpp in winget_pkgs.rglob("g++.exe"):
            return [str(gpp), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-static"]
    extra_candidates = [
        r"C:\msys64\mingw64\bin\g++.exe",
        r"C:\msys64\ucrt64\bin\g++.exe",
        r"C:\Program Files\LLVM\bin\clang++.exe",
        str(Path.home() / "AppData/Local/Microsoft/WinGet/Links/g++.exe"),
        str(Path(__file__).resolve().parent.parent / ".toolchains/mingw64/bin/g++.exe"),
    ]
    for p in extra_candidates:
        if os.path.exists(p):
            return [p, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror"]
    raise RuntimeError("No C++20 compiler found.")

def main():
    cmd = find_compiler()
    out_bin = ROOT / ("test_protocol.exe" if os.name == "nt" else "test_protocol")
    compile_cmd = cmd + [str(ROOT / "test_protocol.cpp"), "-o", str(out_bin)]
    print("Compiling:", " ".join(compile_cmd))
    subprocess.run(compile_cmd, check=True)
    print("Running:", str(out_bin))
    subprocess.run([str(out_bin)], check=True)

if __name__ == "__main__":
    main()
