"""Package installed SDK files, manifests and tracked consumer/documentation sources."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import tarfile
import tempfile
ROOT=Path(__file__).resolve().parents[1]
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build",type=Path,default=Path("build"))
    p.add_argument("--platform",required=True,choices=("windows-x64","linux-x64","macos-arm64"))
    p.add_argument("--output",type=Path,default=Path("dist"))
    p.add_argument("--native-output",type=Path,default=Path("native"))
    a=p.parse_args()
    manifest=a.build/"maa-capture-build-Release.json"
    info=json.loads(manifest.read_text(encoding="utf-8"))
    commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    status=subprocess.check_output(["git","status","--porcelain","--untracked-files=normal"],cwd=ROOT,text=True)
    if info["commit"]!=commit or status.strip():
        raise RuntimeError("SDK packaging requires a clean committed source tree and matching binary manifest")
    a.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as folder:
        stage=Path(folder)/"maa-capture"
        subprocess.run(["cmake","--install",str(a.build),"--config","Release","--prefix",str(stage)],check=True)
        # These files must come from the same commit as the binary.
        raw=subprocess.check_output(["git","archive","HEAD","bindings","sample","docs","README.md","LICENSE","CHANGELOG.md"],cwd=ROOT)
        import io
        with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
            archive.extractall(stage,filter="data")
        name=f"maa-capture-v{info['version']}-{a.platform}"
        if a.platform=="windows-x64":
            archive=a.output/(name+".7z")
            subprocess.run(["7z","a","-t7z",str(archive.resolve()),str(stage)],check=True)
            subprocess.run(["7z","t",str(archive)],check=True)
        else:
            archive=a.output/(name+".tar.gz")
            with tarfile.open(archive,"w:gz",dereference=False) as target:
                target.add(stage,arcname="maa-capture")
        native=a.native_output/a.platform
        native.mkdir(parents=True,exist_ok=True)
        filename="maa-capture.dll" if a.platform=="windows-x64" else "libmaa-capture.dylib" if a.platform=="macos-arm64" else "libmaa-capture.so"
        binary=stage/("bin" if a.platform=="windows-x64" else "lib")/filename
        if not binary.exists():
            matches=list(stage.rglob(filename))
            if len(matches)!=1:raise RuntimeError("Cannot locate installed native library")
            binary=matches[0]
        shutil.copy2(binary,native/filename)
        shutil.copy2(manifest,native/"build-manifest.json")
    print(archive)
