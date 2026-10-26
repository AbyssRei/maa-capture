"""Compile an original ABI 2 C caller/header snapshot against the current SDK.

This checks C compatibility only. C++ consumers must use the matching SDK.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--ref", default="9d40633ffa4e6da1c507cdd30ba61bd61a4664dc")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    with tempfile.TemporaryDirectory(prefix="maa-legacy-c-") as directory:
        source = Path(directory)
        for name in ("include/maa-capture/CaptureTypes.h", "include/maa-capture/MaaWSCustomScreencap.h", "sample/external_c_manager.c"):
            content = subprocess.check_output(["git", "show", f"{args.ref}:{name}"], cwd=ROOT)
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(legacy_c LANGUAGES C)
find_package(maa-capture CONFIG REQUIRED)
add_executable(legacy_c sample/external_c_manager.c)
target_include_directories(legacy_c BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(legacy_c PRIVATE maa-capture::maa-capture)
''', encoding="utf-8")
        build = source / "build"
        subprocess.run(["cmake", "-S", str(source), "-B", str(build), f"-DCMAKE_PREFIX_PATH={prefix}"], check=True)
        subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel"], check=True)
        env = os.environ.copy()
        env["PATH"] = str(prefix / "bin") + os.pathsep + env.get("PATH", "")
        env["LD_LIBRARY_PATH"] = str(prefix / "lib") + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        executable = build / "Release/legacy_c.exe" if os.name == "nt" else build / "legacy_c"
        subprocess.run([str(executable)], env=env, check=True)
    print("Original ABI 2 C headers/caller with current library: OK")
