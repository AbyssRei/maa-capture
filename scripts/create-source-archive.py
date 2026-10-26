"""Archive a tracked commit, including all native sources, without worktree files."""
from pathlib import Path
import argparse
import gzip
import io
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
def create(ref, output):
    commit = subprocess.check_output(["git","rev-parse",f"{ref}^{{commit}}"],cwd=ROOT,text=True).strip()
    cmake = subprocess.check_output(["git","show",f"{commit}:CMakeLists.txt"],cwd=ROOT,text=True)
    version = re.search(r"project\(maa-capture\s+VERSION\s+(\d+\.\d+\.\d+)",cmake)[1]
    prefix = f"maa-capture-v{version}/"
    raw = subprocess.check_output(["git","archive","--format=tar",f"--prefix={prefix}",commit],cwd=ROOT)
    timestamp = int(subprocess.check_output(["git","show","-s","--format=%ct",commit],cwd=ROOT,text=True))
    data = io.BytesIO()
    with tarfile.open(fileobj=data,mode="w") as target,tarfile.open(fileobj=io.BytesIO(raw)) as source:
        for member in source:
            target.addfile(member,source.extractfile(member) if member.isfile() else None)
        marker = tarfile.TarInfo(prefix+"SOURCE_COMMIT")
        content = (commit+"\n").encode()
        marker.size,marker.mtime,marker.mode = len(content),timestamp,0o644
        target.addfile(marker,io.BytesIO(content))
    output.mkdir(parents=True,exist_ok=True)
    path = output/f"maa-capture-v{version}-source.tar.gz"
    # Reproducible gzip header, no absolute filename or wall-clock timestamp.
    payload = gzip.compress(data.getvalue(),mtime=timestamp)
    if path.exists() and path.read_bytes()!=payload:
        raise RuntimeError(f"Refusing to overwrite a different source asset: {path}")
    path.write_bytes(payload)
    return path
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ref",default="HEAD")
    p.add_argument("--output",type=Path,default=Path("dist"))
    a=p.parse_args();print(create(a.ref,a.output))
