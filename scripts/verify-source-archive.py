"""Build an isolated source asset and an installed consumer without worktree input."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tarfile
import tempfile
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("folder",type=Path);a=p.parse_args()
    assets=list(a.folder.glob("maa-capture-v*-source.tar.gz"))
    if len(assets)!=1:raise RuntimeError("Expected one full source asset")
    with tempfile.TemporaryDirectory(prefix="maa-source-") as folder:
        root=Path(folder)
        with tarfile.open(assets[0]) as archive:archive.extractall(root,filter="data")
        sources=[path for path in root.iterdir() if path.is_dir()]
        if len(sources)!=1:raise RuntimeError("Invalid source asset root")
        source=sources[0];build=root/"build";prefix=root/"install";consumer=root/"consumer"
        subprocess.run(["cmake","-S",str(source),"-B",str(build)],check=True)
        subprocess.run(["cmake","--build",str(build),"--config","Release","--parallel"],check=True)
        subprocess.run(["ctest","--test-dir",str(build),"-C","Release","--output-on-failure"],check=True)
        subprocess.run(["cmake","--install",str(build),"--config","Release","--prefix",str(prefix)],check=True)
        subprocess.run(["cmake","-S",str(source/"tests/install-consumer"),"-B",str(consumer),f"-DCMAKE_PREFIX_PATH={prefix}"],check=True)
        subprocess.run(["cmake","--build",str(consumer),"--config","Release","--parallel"],check=True)
        env=os.environ.copy();env["PATH"]=str(prefix/"bin")+os.pathsep+env.get("PATH","")
        exe=consumer/"Release/maa_capture_consumer.exe" if os.name=="nt" else consumer/"maa_capture_consumer"
        subprocess.run([str(exe)],env=env,check=True)
        env["PYTHONPATH"]=str(source/"bindings/python/src")
        library=build/"Release/maa-capture.dll" if os.name=="nt" else build/("libmaa-capture.dylib" if sys.platform=="darwin" else "libmaa-capture.so")
        subprocess.run([sys.executable,str(source/"bindings/python/tests/test_binding.py"),str(library)],env=env,check=True)
