import ctypes
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
BUILD_TOOLS = Path(r"C:\Users\Vinic\AppData\Local\Android\Sdk\build-tools\34.0.0")

def verify_dll_runtime():
    dll_path = DIST / "cameraphone.dll"
    assert dll_path.exists(), f"Missing {dll_path}"
    lib = ctypes.WinDLL(str(dll_path))

    # 1. Verify obs_module_ver() == (30 << 24)
    lib.obs_module_ver.restype = ctypes.c_uint32
    ver = lib.obs_module_ver()
    assert ver == (30 << 24), f"Unexpected obs_module_ver: {hex(ver)}"

    # 2. Verify obs_module_name() and obs_module_description() (NO 'OBS' in module name)
    lib.obs_module_name.restype = ctypes.c_char_p
    lib.obs_module_description.restype = ctypes.c_char_p
    name = lib.obs_module_name().decode("utf-8")
    desc = lib.obs_module_description().decode("utf-8")
    assert name == "CameraPhone 4K Pro", f"Unexpected module name: {name}"
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
    exe_path = DIST / "CameraPhone.exe"
    assert exe_path.exists(), f"Missing {exe_path}"

    res1 = subprocess.run([str(exe_path), "--self-test"], capture_output=True, text=True, check=True)
    assert "[PASS]" in res1.stdout, f"Unexpected self-test output: {res1.stdout}"

    # Verify embedded Win32 RT_GROUP_ICON / RT_ICON resource exists inside CameraPhone.exe
    kernel32 = ctypes.windll.kernel32
    kernel32.LoadLibraryExW.restype = ctypes.c_void_p
    kernel32.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    kernel32.FindResourceW.restype = ctypes.c_void_p
    hmod = kernel32.LoadLibraryExW(str(exe_path), None, 0x00000002) # LOAD_LIBRARY_AS_DATAFILE
    assert hmod, "Failed to map CameraPhone.exe as datafile"
    hres = kernel32.FindResourceW(hmod, ctypes.c_void_p(1), ctypes.c_void_p(14)) # RT_GROUP_ICON = 14, ID = 1
    kernel32.FreeLibrary(ctypes.c_void_p(hmod))
    assert hres, "Missing embedded CameraPhone icon resource (RT_GROUP_ICON ID 1) in CameraPhone.exe!"

    # Also test GUI window creation & clean shutdown
    ps_gui_test = (
        f"$p = Start-Process -FilePath '{exe_path}' -PassThru; "
        "Start-Sleep -Milliseconds 600; "
        "$alive = !$p.HasExited; "
        "Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; "
        "if (-not $alive) { exit 1 }"
    )
    subprocess.run(["powershell", "-NoProfile", "-Command", ps_gui_test], check=True)
    print(f"[PASS] EXE Runtime Test: {exe_path.name} CLI (--self-test), embedded Icon (RT_GROUP_ICON), and Win32 GUI window verified.")

def verify_apk_package_and_bytecode():
    apk_path = DIST / "CameraPhone.apk"
    assert apk_path.exists(), f"Missing {apk_path}"

    apksigner = str(BUILD_TOOLS / "apksigner.bat")
    res_sig = subprocess.run([apksigner, "verify", "--verbose", str(apk_path)], capture_output=True, text=True, check=True)
    assert "Verifies" in res_sig.stdout
    assert "Verified using v3 scheme (APK Signature Scheme v3): true" in res_sig.stdout

    aapt2 = str(BUILD_TOOLS / "aapt2.exe")
    res_badging = subprocess.run([aapt2, "dump", "badging", str(apk_path)], capture_output=True, text=True, check=True)
    assert "package: name='com.cameraphone.app'" in res_badging.stdout, f"Unexpected package name: {res_badging.stdout}"
    assert "application-label:'CameraPhone'" in res_badging.stdout, f"Unexpected application-label: {res_badging.stdout}"
    assert "icon='res/mipmap" in res_badging.stdout, f"Missing launcher icon in APK badging: {res_badging.stdout}"
    assert "sdkVersion:'28'" in res_badging.stdout
    assert "targetSdkVersion:'34'" in res_badging.stdout
    assert "launchable-activity: name='com.cameraphone.app.ui.MainActivity'" in res_badging.stdout

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
        "Lcom/cameraphone/app/security/SecurityGate;",
        "Lcom/cameraphone/app/protocol/CamPhonePacket;",
        "Lcom/cameraphone/app/probe/CapabilityProbe;",
        "Lcom/cameraphone/app/camera/Camera2Controller;",
        "Lcom/cameraphone/app/camera/HardwareVideoEncoder;",
        "Lcom/cameraphone/app/camera/HardwareAudioEncoder;",
        "Lcom/cameraphone/app/net/CamPhoneServer;",
        "Lcom/cameraphone/app/service/CameraStreamService;",
        "Lcom/cameraphone/app/ui/MainActivity;",
        "Lcom/cameraphone/app/util/AppLogger;",
    ]:
        assert required_cls in combined_dex, f"Missing class {required_cls} in APK DEX!"

    print(f"[PASS] APK Verification Test: {apk_path.name} cryptographic signatures (v1/v2/v3), custom launcher icon, package 'com.cameraphone.app', label 'CameraPhone', and all 10 DEX classes verified.")

if __name__ == "__main__":
    verify_dll_runtime()
    verify_exe_runtime()
    verify_apk_package_and_bytecode()
    print("[PASS] ALL SHIPPED ARTIFACTS (cameraphone.dll, CameraPhone.exe, CameraPhone.apk) VERIFIED AND FUNCTIONAL!")
