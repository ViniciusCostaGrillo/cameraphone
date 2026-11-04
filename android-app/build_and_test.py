import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
APP_MAIN = ROOT / "app" / "src" / "main"
APP_TEST = ROOT / "app" / "src" / "test" / "java"
BUILD_DIR = ROOT / "build"

KOTLINC = r"C:\Program Files\Android\Android Studio\plugins\Kotlin\kotlinc\bin\kotlinc.bat"
KOTLIN_STDLIB = r"C:\Program Files\Android\Android Studio\plugins\Kotlin\kotlinc\lib\kotlin-stdlib.jar"
JUNIT4_JAR = r"C:\Program Files\Android\Android Studio\lib\junit4.jar"
ANDROID_JAR = r"C:\Users\Vinic\AppData\Local\Android\Sdk\platforms\android-34\android.jar"
BUILD_TOOLS = Path(r"C:\Users\Vinic\AppData\Local\Android\Sdk\build-tools\34.0.0")

def collect_kt_files(folder: Path):
    if not folder.exists():
        return []
    return [str(p) for p in folder.rglob("*.kt")]

def run_tests(filter_class: str = ""):
    main_sources = collect_kt_files(APP_MAIN / "java")
    test_sources = collect_kt_files(APP_TEST)
    if not test_sources:
        print("No test sources found.", file=sys.stderr)
        sys.exit(1)

    classes_dir = BUILD_DIR / "test-classes"
    if classes_dir.exists():
        shutil.rmtree(classes_dir)
    classes_dir.mkdir(parents=True, exist_ok=True)

    classpath = os.pathsep.join([ANDROID_JAR, JUNIT4_JAR, KOTLIN_STDLIB])
    compile_cmd = [
        KOTLINC,
        "-classpath", classpath,
        "-d", str(classes_dir),
    ] + main_sources + test_sources

    print("[Kotlin Compile]", len(main_sources), "main files +", len(test_sources), "test files")
    res = subprocess.run(compile_cmd)
    if res.returncode != 0:
        sys.exit(res.returncode)

    # Discover test classes (*Test.class)
    test_classes = []
    for cls_file in classes_dir.rglob("*Test.class"):
        rel = cls_file.relative_to(classes_dir).with_suffix("")
        fqcn = ".".join(rel.parts)
        if not filter_class or filter_class in fqcn:
            test_classes.append(fqcn)

    if not test_classes:
        print("No matching *Test classes found.", file=sys.stderr)
        sys.exit(1)

    runner_java = BUILD_DIR / "TestRunner.java"
    runner_java.write_text("""
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import org.junit.Test;

public class TestRunner {
    public static void main(String[] args) throws Exception {
        int passed = 0;
        int failed = 0;
        for (String className : args) {
            Class<?> cls = Class.forName(className);
            for (Method m : cls.getDeclaredMethods()) {
                Test ann = m.getAnnotation(Test.class);
                if (ann == null) continue;
                Object instance = cls.getDeclaredConstructor().newInstance();
                Class<? extends Throwable> expected = ann.expected();
                try {
                    m.invoke(instance);
                    if (expected != Test.None.class) {
                        System.err.println("FAIL: " + className + "." + m.getName() + " (expected " + expected.getName() + ")");
                        failed++;
                    } else {
                        System.out.println("PASS: " + className + "." + m.getName());
                        passed++;
                    }
                } catch (InvocationTargetException e) {
                    Throwable cause = e.getCause();
                    if (expected != Test.None.class && expected.isInstance(cause)) {
                        System.out.println("PASS: " + className + "." + m.getName() + " (threw expected " + expected.getSimpleName() + ")");
                        passed++;
                    } else {
                        System.err.println("FAIL: " + className + "." + m.getName() + " -> " + cause);
                        cause.printStackTrace();
                        failed++;
                    }
                }
            }
        }
        System.out.println("Total: " + (passed + failed) + ", Passed: " + passed + ", Failed: " + failed);
        if (failed > 0) System.exit(1);
    }
}
""", encoding="utf-8")
    subprocess.run(["javac", "-cp", JUNIT4_JAR, "-d", str(classes_dir), str(runner_java)], check=True)

    run_cp = os.pathsep.join([str(classes_dir), JUNIT4_JAR, KOTLIN_STDLIB, ANDROID_JAR])
    java_cmd = ["java", "-cp", run_cp, "TestRunner"] + sorted(test_classes)
    print("[JUnit4 Run]", " ".join(sorted(test_classes)))
    res = subprocess.run(java_cmd)
    sys.exit(res.returncode)

def build_apk():
    main_sources = collect_kt_files(APP_MAIN / "java")
    if not main_sources:
        print("No main sources found.", file=sys.stderr)
        sys.exit(1)

    apk_work = BUILD_DIR / "apk-work"
    if apk_work.exists():
        shutil.rmtree(apk_work)
    apk_work.mkdir(parents=True, exist_ok=True)

    res_dir = APP_MAIN / "res"
    manifest = APP_MAIN / "AndroidManifest.xml"
    r_java_dir = apk_work / "r_src"
    r_java_dir.mkdir(parents=True, exist_ok=True)
    unaligned_apk = apk_work / "unaligned.apk"

    aapt2 = str(BUILD_TOOLS / "aapt2.exe")
    compiled_res_zip = apk_work / "compiled_res.zip"
    if res_dir.exists() and any(res_dir.rglob("*")):
        subprocess.run([aapt2, "compile", "--dir", str(res_dir), "-o", str(compiled_res_zip)], check=True)
        subprocess.run([
            aapt2, "link",
            "-I", ANDROID_JAR,
            "--manifest", str(manifest),
            "-o", str(unaligned_apk),
            "--java", str(r_java_dir),
            str(compiled_res_zip)
        ], check=True)
    else:
        subprocess.run([
            aapt2, "link",
            "-I", ANDROID_JAR,
            "--manifest", str(manifest),
            "-o", str(unaligned_apk),
            "--java", str(r_java_dir)
        ], check=True)

    classes_dir = apk_work / "classes"
    classes_dir.mkdir(parents=True, exist_ok=True)
    r_java_files = [str(p) for p in r_java_dir.rglob("*.java")]

    # Compile R.java if generated
    if r_java_files:
        subprocess.run(["javac", "-source", "17", "-target", "17", "-cp", ANDROID_JAR, "-d", str(classes_dir)] + r_java_files, check=True)

    classpath = os.pathsep.join([ANDROID_JAR, KOTLIN_STDLIB, str(classes_dir)])
    subprocess.run([KOTLINC, "-jvm-target", "17", "-classpath", classpath, "-d", str(classes_dir)] + main_sources, check=True)

    # Dex with d8
    d8 = str(BUILD_TOOLS / "d8.bat")
    class_files = [str(p) for p in classes_dir.rglob("*.class")]
    subprocess.run([
        d8,
        "--min-api", "28",
        "--lib", ANDROID_JAR,
        "--output", str(apk_work),
        KOTLIN_STDLIB
    ] + class_files, check=True)

    # Add classes.dex to unaligned.apk
    import zipfile
    with zipfile.ZipFile(unaligned_apk, "a", compression=zipfile.ZIP_DEFLATED) as zf:
        for dex in apk_work.glob("classes*.dex"):
            zf.write(dex, dex.name)

    out_dir = ROOT / "app" / "build" / "outputs" / "apk" / "debug"
    out_dir.mkdir(parents=True, exist_ok=True)
    aligned_apk = out_dir / "app-debug.apk"
    if aligned_apk.exists():
        aligned_apk.unlink()

    zipalign = str(BUILD_TOOLS / "zipalign.exe")
    subprocess.run([zipalign, "-p", "-f", "4", str(unaligned_apk), str(aligned_apk)], check=True)

    # Sign with debug keystore
    keystore = apk_work / "debug.keystore"
    subprocess.run([
        "keytool", "-genkeypair",
        "-keystore", str(keystore),
        "-storepass", "android",
        "-keypass", "android",
        "-alias", "androiddebugkey",
        "-keyalg", "RSA",
        "-keysize", "2048",
        "-validity", "10000",
        "-dname", "CN=Android Debug,O=Android,C=US"
    ], check=True)

    apksigner = str(BUILD_TOOLS / "apksigner.bat")
    subprocess.run([
        apksigner, "sign",
        "--ks", str(keystore),
        "--ks-pass", "pass:android",
        "--key-pass", "pass:android",
        str(aligned_apk)
    ], check=True)
    print(f"[APK Built & Signed] {aligned_apk} ({aligned_apk.stat().st_size} bytes)")

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "test"
    if cmd == "test":
        flt = sys.argv[2] if len(sys.argv) > 2 else ""
        run_tests(flt)
    elif cmd == "assembleDebug":
        build_apk()
    else:
        print(f"Unknown command: {cmd}", file=sys.stderr)
        sys.exit(1)
