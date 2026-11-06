import ctypes
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OBS_BIN = r"C:\Program Files\obs-studio\bin\64bit"
os.add_dll_directory(OBS_BIN)

avutil = ctypes.CDLL(os.path.join(OBS_BIN, "avutil-60.dll"))
avcodec = ctypes.CDLL(os.path.join(OBS_BIN, "avcodec-62.dll"))
obs_dll = ctypes.CDLL(os.path.join(OBS_BIN, "obs.dll"))

avcodec.avcodec_find_encoder_by_name.argtypes = [ctypes.c_char_p]
avcodec.avcodec_find_encoder_by_name.restype = ctypes.c_void_p
avcodec.avcodec_alloc_context3.argtypes = [ctypes.c_void_p]
avcodec.avcodec_alloc_context3.restype = ctypes.c_void_p
avcodec.avcodec_open2.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
avcodec.avcodec_open2.restype = ctypes.c_int
avcodec.avcodec_send_frame.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
avcodec.avcodec_send_frame.restype = ctypes.c_int
avcodec.avcodec_receive_packet.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
avcodec.avcodec_receive_packet.restype = ctypes.c_int
avcodec.av_packet_alloc.argtypes = []
avcodec.av_packet_alloc.restype = ctypes.c_void_p

avutil.av_frame_alloc.argtypes = []
avutil.av_frame_alloc.restype = ctypes.c_void_p
avutil.av_frame_get_buffer.argtypes = [ctypes.c_void_p, ctypes.c_int]
avutil.av_frame_get_buffer.restype = ctypes.c_int
avutil.av_opt_set.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
avutil.av_opt_set.restype = ctypes.c_int

class AVFramePartial(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p * 8),
        ("linesize", ctypes.c_int * 8),
        ("extended_data", ctypes.c_void_p),
        ("width", ctypes.c_int),
        ("height", ctypes.c_int),
        ("nb_samples", ctypes.c_int),
        ("format", ctypes.c_int),
        ("key_frame_deprecated", ctypes.c_int),
        ("pict_type", ctypes.c_int),
        ("sample_aspect_ratio_num", ctypes.c_int),
        ("sample_aspect_ratio_den", ctypes.c_int),
        ("pts", ctypes.c_int64),
    ]

class AVPacketPartial(ctypes.Structure):
    _fields_ = [
        ("buf", ctypes.c_void_p),
        ("pts", ctypes.c_int64),
        ("dts", ctypes.c_int64),
        ("data", ctypes.c_void_p),
        ("size", ctypes.c_int),
    ]

def generate_encoded_frames_file(encoder_name: bytes, out_path: Path, count: int = 30):
    enc = avcodec.avcodec_find_encoder_by_name(encoder_name)
    assert enc, f"Encoder {encoder_name} not found in avcodec-62.dll"
    ctx = avcodec.avcodec_alloc_context3(enc)
    avutil.av_opt_set(ctx, b"video_size", b"1920x1080", 0)
    avutil.av_opt_set(ctx, b"pixel_format", b"yuv420p", 0)
    avutil.av_opt_set(ctx, b"time_base", b"1/60", 0)
    if encoder_name == b"hevc_nvenc":
        avutil.av_opt_set(ctx, b"delay", b"0", 1)
        avutil.av_opt_set(ctx, b"zerolatency", b"1", 1)
        avutil.av_opt_set(ctx, b"bf", b"0", 0)
    else:
        avutil.av_opt_set(ctx, b"preset", b"ultrafast", 1)
        avutil.av_opt_set(ctx, b"tune", b"zerolatency", 1)

    ret = avcodec.avcodec_open2(ctx, enc, None)
    assert ret == 0, f"avcodec_open2({encoder_name}) failed with {ret}"

    frm_ptr = avutil.av_frame_alloc()
    frm = AVFramePartial.from_address(frm_ptr)
    frm.width = 1920
    frm.height = 1080
    frm.format = 0
    avutil.av_frame_get_buffer(frm_ptr, 32)
    pkt_ptr = avcodec.av_packet_alloc()
    pkt = AVPacketPartial.from_address(pkt_ptr)

    chunks = []
    for i in range(count):
        frm.pts = i
        ctypes.memset(frm.data[0], (60 + (i * 7) % 160) & 0xFF, frm.linesize[0] * 1080)
        ctypes.memset(frm.data[1], 120, frm.linesize[1] * 540)
        ctypes.memset(frm.data[2], 140, frm.linesize[2] * 540)
        avcodec.avcodec_send_frame(ctx, frm_ptr)
        if avcodec.avcodec_receive_packet(ctx, pkt_ptr) == 0:
            payload = ctypes.string_at(pkt.data, pkt.size)
            flags = 0x03 if i == 0 else 0x00 # FlagKeyframe | FlagCodecConfig on frame 0
            chunks.append(struct.pack(">IB", len(payload), flags) + payload)

    out_path.write_bytes(b"".join(chunks))
    print(f"[Encoder] Generated {len(chunks)} real 1080p60 {encoder_name.decode()} frames ({out_path.stat().st_size} bytes)")

def launch_visual_android_emulator_window(pin_holder: dict, status_holder: dict, stop_evt: threading.Event):
    try:
        import tkinter as tk
        root = tk.Tk()
        root.title("Android Emulator - CameraPhone 4K Pro (com.cameraphone.app)")
        root.geometry("420x680+80+80")
        root.configure(bg="#090D16")
        root.attributes("-topmost", True)

        lbl_top = tk.Label(root, text="ANDROID 14 EMULATOR • API 34", fg="#64748B", bg="#090D16", font=("Segoe UI", 9, "bold"))
        lbl_top.pack(pady=(12, 4))

        lbl_title = tk.Label(root, text="CameraPhone 4K Pro", fg="#F8FAFC", bg="#090D16", font=("Segoe UI", 18, "bold"))
        lbl_title.pack(pady=(0, 6))

        lbl_badge = tk.Label(root, text="● AGUARDANDO CONEXÃO OBS...", fg="#10B981", bg="#111827", font=("Segoe UI", 11, "bold"), padx=14, pady=6)
        lbl_badge.pack(pady=6)

        card = tk.Frame(root, bg="#111827", padx=16, pady=14)
        card.pack(fill="x", padx=20, pady=8)

        lbl_ip = tk.Label(card, text=f"IP Wi-Fi / LAN: {status_holder.get('lan_ip', '127.0.0.1')} : 4748\nUSB Zero-Config: 127.0.0.1 : 4748", fg="#38BDF8", bg="#111827", font=("Consolas", 11, "bold"))
        lbl_ip.pack()

        lbl_pin = tk.Label(card, text="PIN: ------", fg="#F8FAFC", bg="#111827", font=("Consolas", 22, "bold"))
        lbl_pin.pack(pady=(8, 2))

        preview = tk.Canvas(root, width=360, height=202, bg="#1E293B", highlightthickness=1, highlightbackground="#334155")
        preview.pack(pady=10)

        log_box = tk.Text(root, height=10, bg="#05080F", fg="#38BDF8", font=("Consolas", 8), bd=0)
        log_box.pack(fill="both", expand=True, padx=20, pady=(4, 16))

        frame_tick = [0]
        def update_ui():
            if stop_evt.is_set():
                root.quit()
                return
            frame_tick[0] += 1
            pin = pin_holder.get("pin", "------")
            lbl_pin.config(text=f"PIN: {pin[:3]} {pin[3:]}" if len(pin) == 6 else f"PIN: {pin}")
            st = status_holder.get("state", "STANDBY")
            if st == "STREAMING":
                lbl_badge.config(text="● NO AR • 1920x1080 @ 60fps • OBS CONECTADO", fg="#FFFFFF", bg="#EF4444")
            else:
                lbl_badge.config(text="● PRONTO (USB & WI-FI ATIVOS NA PORTA 4748)", fg="#10B981", bg="#111827")

            preview.delete("all")
            bar_x = (frame_tick[0] * 12) % 320
            preview.create_rectangle(bar_x, 0, bar_x + 40, 202, fill="#0284C7", outline="")
            preview.create_text(180, 90, text="CameraPhone 4K60 Live Sensor", fill="#F8FAFC", font=("Segoe UI", 12, "bold"))
            preview.create_text(180, 118, text=f"Frame #{frame_tick[0]} • {status_holder.get('codec', 'HEVC/AVC')}", fill="#38BDF8", font=("Consolas", 10))

            logs = status_holder.get("logs", [])
            log_box.delete("1.0", "end")
            log_box.insert("end", "\n".join(logs[-12:]))
            root.after(80, update_ui)

        root.after(80, update_ui)
        root.mainloop()
        try:
            root.destroy()
        except Exception:
            pass
    except Exception as e:
        print(f"[GUI] Emulator window note: {e}")

def get_local_lan_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"

def run_live_emulator_session(frames_path: Path, connect_host: str, init_codec: str, label: str):
    stdlib = r"C:\Program Files\Android\Android Studio\plugins\Kotlin\kotlinc\lib\kotlin-stdlib.jar"
    kotlinc = r"C:\Program Files\Android\Android Studio\plugins\Kotlin\kotlinc\bin\kotlinc.bat"
    android_jar = r"C:\Users\Vinic\AppData\Local\Android\Sdk\platforms\android-34\android.jar"
    test_classes_dir = ROOT / "android-app" / "build" / "test-classes"
    test_classes = str(test_classes_dir)
    cp = os.pathsep.join([test_classes, stdlib, android_jar])

    emulator_cls = test_classes_dir / "com" / "cameraphone" / "emulator" / "LiveAndroidEmulatorServer.class"
    if not emulator_cls.exists():
        subprocess.run([
            kotlinc,
            "-cp", cp,
            str(ROOT / "tools" / "LiveAndroidEmulatorServer.kt"),
            "-d", test_classes
        ], check=True)

    pin_holder = {"pin": "------"}
    status_holder = {"state": "STANDBY", "lan_ip": connect_host, "codec": init_codec.upper(), "logs": [f"[Init] Starting {label}..."]}
    stop_gui = threading.Event()
    gui_thread = threading.Thread(target=launch_visual_android_emulator_window, args=(pin_holder, status_holder, stop_gui), daemon=True)
    gui_thread.start()

    proc = subprocess.Popen(
        ["java", "-cp", cp, "com.cameraphone.emulator.LiveAndroidEmulatorServer", str(frames_path), "20"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )

    pin = None
    start_wait = time.time()
    while time.time() - start_wait < 5.0:
        line = proc.stdout.readline()
        if not line:
            break
        line = line.strip()
        status_holder["logs"].append(line)
        print(f"  [Android JVM] {line}")
        if line.startswith("EMULATOR_READY"):
            for part in line.split():
                if part.startswith("pin="):
                    pin = part.split("=", 1)[1]
                    pin_holder["pin"] = pin
            break

    assert pin and len(pin) == 6, "Android Emulator JVM server did not output a 6-digit PIN!"
    status_holder["state"] = "STREAMING"

    def drain_stdout():
        for l in proc.stdout:
            l = l.strip()
            if l:
                status_holder["logs"].append(l)
                print(f"  [Android JVM] {l}")
    threading.Thread(target=drain_stdout, daemon=True).start()

    e2e_exe = str(ROOT / "obs-plugin" / "build" / "test_live_emulator_to_obs.exe")
    effective_pin = "usb-zero-config" if connect_host == "127.0.0.1" else pin
    print(f"\n=== Running {label}: C++ OBS Plugin -> Android Emulator ({connect_host}:4748, PIN={effective_pin}, init_codec={init_codec}) ===")
    res = subprocess.run([e2e_exe, connect_host, effective_pin, init_codec], capture_output=True, text=True)
    print(res.stdout)
    if res.stderr:
        print(res.stderr)
    assert res.returncode == 0, f"{label} failed with exit code {res.returncode}"

    stop_gui.set()
    gui_thread.join(timeout=2.0)
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()
    time.sleep(0.5)

if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmpd:
        hevc_file = Path(tmpd) / "hevc_1080p60.bin"
        h264_file = Path(tmpd) / "h264_1080p60.bin"
        generate_encoded_frames_file(b"hevc_nvenc", hevc_file, count=30)
        generate_encoded_frames_file(b"libx264", h264_file, count=30)

        # Test 1: USB Cable Mode (127.0.0.1:4748) streaming real HEVC 1080p60 into OBS MediaDecoder (avcodec-62.dll)
        run_live_emulator_session(hevc_file, "127.0.0.1", "hevc", "TEST 1: USB Mode (127.0.0.1:4748) Real HEVC 1080p60 Stream")

        # Test 2: Wi-Fi LAN IP Mode (192.168.x.x:4748) with 6-digit PIN + Auto-Codec Switching (OBS init=hevc, Phone streams H.264/AVC)
        lan_ip = get_local_lan_ip()
        run_live_emulator_session(h264_file, lan_ip, "hevc", f"TEST 2: Wi-Fi LAN IP Mode ({lan_ip}:4748) + Auto-Codec Switch (HEVC->AVC)")

    print("\n[ALL LIVE ANDROID EMULATOR <-> OBS STUDIO INTEGRATION TESTS PASSED!]")
    sys.stdout.flush()
    os._exit(0)
