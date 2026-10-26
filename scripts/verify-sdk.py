"""Verify an unpacked SDK using only its installed headers, library and samples."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tarfile
import tempfile
def consume(sdk,work,major):
    prefix=sdk.as_posix()
    (work/"CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.21)
project(sdk_consumer LANGUAGES C CXX)
find_package(maa-capture {major} CONFIG REQUIRED)
add_executable(c_consumer "{prefix}/sample/external_c_manager.c")
target_link_libraries(c_consumer PRIVATE maa-capture::maa-capture)
add_executable(cpp_consumer "{prefix}/sample/external_cpp_interface.cpp")
target_link_libraries(cpp_consumer PRIVATE maa-capture::maa-capture)
''',encoding="utf-8")
    build=work/"build"
    subprocess.run(["cmake","-S",str(work),"-B",str(build),f"-DCMAKE_PREFIX_PATH={sdk}"],check=True)
    subprocess.run(["cmake","--build",str(build),"--config","Release","--parallel"],check=True)
    env=os.environ.copy()
    env["PATH"]=str(sdk/"bin")+os.pathsep+env.get("PATH","")
    for name in ("c_consumer","cpp_consumer"):
        binary=build/"Release"/(name+".exe") if os.name=="nt" else build/name
        subprocess.run([str(binary)],env=env,check=True)
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("folder",type=Path);p.add_argument("--platform",required=True);a=p.parse_args()
    assets=list(a.folder.glob(f"maa-capture-v*-{a.platform}.*"))
    if len(assets)!=1:raise RuntimeError("Expected one SDK asset")
    with tempfile.TemporaryDirectory(prefix="maa-sdk-") as folder:
        root=Path(folder)
        if assets[0].suffix==".7z":subprocess.run(["7z","x",str(assets[0].resolve()),"-o"+str(root)],check=True)
        else:
            with tarfile.open(assets[0]) as archive:archive.extractall(root,filter="data")
        sdk=root/"maa-capture"
        manifests=list(sdk.rglob("build-manifest.json"))
        if len(manifests)!=1:raise RuntimeError("SDK build manifest is missing")
        info=json.loads(manifests[0].read_text(encoding="utf-8"))
        binaries=list(sdk.rglob(info["native_file"]))
        if len(binaries)!=1 or hashlib.sha256(binaries[0].read_bytes()).hexdigest()!=info["native_sha256"]:
            raise RuntimeError("SDK binary hash differs from build manifest")
        work=root/"consumer";work.mkdir();consume(sdk,work,info["version"].split(".")[0])
