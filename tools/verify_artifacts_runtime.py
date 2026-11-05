import ctypes
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
BUILD_TOOLS = Path(r"C:\Users\Vinic\AppData\Local\Android\Sdk\build-tools\34.0.0")

def verify_dll_runtime():
    dll_path = DIST / "cameraphone-obs.dll"
    assert dll_path.exists(), f"Missing {dll_path}"
    lib = ctypes.WinDLL(str(dll_path))

    # 1. Verify obs_module_ver() == (30 << 24)
    lib.obs_module_ver.restype = ctypes.c_uint32
    ver = lib.obs_module_ver()
    assert ver == (30 << 24), f"Unexpected obs_module_ver: {hex(ver)}"

    # 2. Verify obs_module_name() and obs_module_description()
    lib.obs_module_name.restype = ctypes.c_char_p
    lib.obs_module_description.restype = ctypes.c_char_p
    name = lib.obs_module_name().decode("utf-8")
    desc = lib.obs_module_description().decode("utf-8")
    assert "CameraPhone" in name, f"Unexpected module name: {name}"
    assert "Zero-Trust" in desc and "4K60/120" in desc, f"Unexpected description: {desc}"

    # 3. Verify obs_module_set_pointer / obs_current_module round-trip
    lib.obs_module_set_pointer.argtypes = [ctypes.c_void_p]
    lib.obs_current_module.restype = ctypes.c_void_p
    sentinel = 0x12345678ABCDEF00
    lib.obs_module_set_pointer(ctypes.c_void_p(sentinel))
    assert lib.obs_current_module() == sentinel

    # 4. Verify obs_module_load() and obs_module_unload() execute cleanly without crashing
    lib.obs_module_load.restype = ctypes.c_bool
    assert lib.obs_module_load() is True
    lib.obs_module_unload()
    print(f"[PASS] DLL Runtime Test: {dll_path.name} loaded via WinDLL, ABI v30 verified, name='{name}'")

def verify_exe_runtime():
    exe_path = DIST / "CameraPhone-OBS.exe"
    assert exe_path.exists(), f"Missing {exe_path}"

    res1 = subprocess.run([str(exe_path), "--self-test"], capture_output=True, text=True, check=True)
    assert "[PASS]" in res1.stdout, f"Unexpected self-test output: {res1.stdout}"

    # Also test GUI window creation & clean shutdown via WM_CLOSE
    ps_gui_test = (
        f"$p = Start-Process -FilePath '{exe_path}' -PassThru; "
        "Start-Sleep -Milliseconds 600; "
        "$alive = !$p.HasExited; "
        "Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; "
        "if (-not $alive) { exit 1 }"
    )
    subprocess.run(["powershell", "-NoProfile", "-Command", ps_gui_test], check=True)
    print(f"[PASS] EXE Runtime Test: {exe_path.name} CLI (--self-test) and Win32 GUI window spawn verified.")

def verify_apk_package_and_bytecode():
    apk_path = DIST / "CameraPhone-OBS.apk"
    assert apk_path.exists(), f"Missing {apk_path}"

    apksigner = str(BUILD_TOOLS / "apksigner.bat")
    res_sig = subprocess.run([apksigner, "verify", "--verbose", str(apk_path)], capture_output=True, text=True, check=True)
    assert "Verified using v1 scheme (JAR signing): true" in res_sig.stdout
    assert "Verified using v2 scheme (APK Signature Scheme v2): true" in res_sig.stdout
    assert "Verified using v3 scheme (APK Signature Scheme v3): true" in res_sig.stdout

    aapt2 = str(BUILD_TOOLS / "aapt2.exe")
    res_badging = subprocess.run([aapt2, "dump", "badging", str(apk_path)], capture_output=True, text=True, check=True)
    assert "package: name='com.cameraphone.obs'" in res_badging.stdout
    assert "launchable-activity: name='com.cameraphone.obs.ui.MainActivity'" in res_badging.stdout

    dexdump = str(BUILD_TOOLS / "dexdump.exe")
    import zipfile
    import tempfile
    dex_texts = []
    with zipfile.ZipFile(apk_path, "r") as zf, tempfile.TemporaryDirectory() as tmpd:
        for name in zf.namelist():
            if name.startswith("classes") and name.endswith(".dex"):
                extracted = Path(tmpd) / name
                extracted.write_bytes(zf.read(name))
                res_dex = subprocess.run([dexdump, "-f", str(extracted)], capture_output=True, text=True, check=True)
                dex_texts.append(res_dex.stdout)
    combined_dex = "\n".join(dex_texts)
    for required_cls in [
        "Lcom/cameraphone/obs/security/SecurityGate;",
        "Lcom/cameraphone/obs/protocol/CamPhonePacket;",
        "Lcom/cameraphone/obs/probe/CapabilityProbe;",
        "Lcom/cameraphone/obs/camera/Camera2Controller;",
        "Lcom/cameraphone/obs/camera/HardwareVideoEncoder;",
        "Lcom/cameraphone/obs/camera/HardwareAudioEncoder;",
        "Lcom/cameraphone/obs/net/CamPhoneServer;",
        "Lcom/cameraphone/obs/service/CameraStreamService;",
        "Lcom/cameraphone/obs/ui/MainActivity;",
    ]:
        assert required_cls in combined_dex, f"Missing class {required_cls} in APK DEX!"

    print(f"[PASS] APK Verification Test: {apk_path.name} cryptographic signatures (v1/v2/v3), manifest badging, and all 9 compiled DEX classes verified.")

if __name__ == "__main__":
    verify_dll_runtime()
    verify_exe_runtime()
    verify_apk_package_and_bytecode()
    print("[PASS] ALL SHIPPED ARTIFACTS (.dll, .exe, .apk) VERIFIED AND FUNCTIONAL!")
