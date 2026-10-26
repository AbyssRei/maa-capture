"""Create/verify an acyclic release provenance and SHA256SUMS asset set."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import tarfile
import zipfile
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def check(folder,version,create=False,native=None):
    names=[f"maa-capture-v{version}-{platform}.{extension}" for platform,extension in (("windows-x64","7z"),("linux-x64","tar.gz"),("macos-arm64","tar.gz"))]
    names += [f"maa_capture-{version}-py3-none-any.whl",f"maa_capture-{version}.tar.gz",f"maa-capture-v{version}-source.tar.gz"]
    for name in names:
        if not (folder/name).is_file() or not (folder/name).stat().st_size:
            raise RuntimeError(f"Missing or empty release asset: {name}")
    with tarfile.open(folder/names[-1]) as source:
        prefix=f"maa-capture-v{version}/"
        members=set(source.getnames())
        for required in ("source/ScreencapMgr.cpp","include/maa-capture/CaptureTypes.h","CMakeLists.txt","cmake/write-provenance.cmake","tests/capture_contracts.cpp","scripts/generate-ffi.py","bindings/python/pyproject.toml","sample/external_c_manager.c","LICENSE"):
            if prefix+required not in members:raise RuntimeError(f"Source archive missing {required}")
        commit=source.extractfile(prefix+"SOURCE_COMMIT").read().decode().strip()
    if not re.fullmatch(r"[0-9a-f]{40}",commit):raise RuntimeError("Source asset must name a committed snapshot")
    provenance=folder/f"maa-capture-v{version}-provenance.json"
    if create:
        builds={}
        if native is None:raise RuntimeError("--native is required to create provenance")
        with zipfile.ZipFile(folder/names[3]) as wheel:
            for platform in ("windows-x64","linux-x64","macos-arm64"):
                base=native/platform
                info=json.loads((base/"build-manifest.json").read_text(encoding="utf-8"))
                system={"windows-x64":"Windows","linux-x64":"Linux","macos-arm64":"Darwin"}[platform]
                architectures={"arm64","aarch64"} if platform=="macos-arm64" else {"amd64","x64","x86_64"}
                if info.get("abi")!=2 or info.get("system")!=system or info.get("architecture","").lower() not in architectures:
                    raise RuntimeError(f"Native platform/ABI mismatch: {platform}")
                libraries=[f for f in base.iterdir() if f.suffix in (".dll",".so",".dylib")]
                if len(libraries)!=1 or info["commit"]!=commit or info["version"]!=version or sha(libraries[0])!=info["native_sha256"]:
                    raise RuntimeError(f"Native provenance mismatch: {platform}")
                bundled=wheel.read(f"maa_capture/_native/{platform}/{libraries[0].name}")
                manifest=json.loads(wheel.read(f"maa_capture/_native/{platform}/build-manifest.json"))
                if hashlib.sha256(bundled).hexdigest()!=info["native_sha256"] or manifest!=info:
                    raise RuntimeError(f"Wheel provenance mismatch: {platform}")
                builds[platform]=info
        info={"version":version,"commit":commit,"assets":{name:sha(folder/name) for name in names},"builds":builds}
        provenance.write_text(json.dumps(info,indent=2)+"\n",encoding="utf-8")
        (folder/"SHA256SUMS").write_text("".join(f"{sha(folder/name)}  {name}\n" for name in names+[provenance.name]),encoding="utf-8")
    info=json.loads(provenance.read_text(encoding="utf-8"))
    if info["version"]!=version or info["commit"]!=commit:raise RuntimeError("Provenance version/commit mismatch")
    for name in names:
        if info["assets"].get(name)!=sha(folder/name):raise RuntimeError(f"Asset hash mismatch: {name}")
    sums={}
    for line in (folder/"SHA256SUMS").read_text().splitlines():
        digest,name=line.split("  ",1)
        if Path(name).name!=name or name in sums:raise RuntimeError("Invalid or duplicate checksum filename")
        sums[name]=digest
    if set(sums)!=set(names+[provenance.name]):raise RuntimeError("Incomplete checksum set")
    for name,digest in sums.items():
        if digest!=sha(folder/name):raise RuntimeError(f"Checksum mismatch: {name}")
    print(f"Release {version}: all eight assets, binary/source provenance and checksums verified")
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("folder",type=Path);p.add_argument("--version",required=True);p.add_argument("--create",action="store_true");p.add_argument("--native",type=Path)
    a=p.parse_args();check(a.folder,a.version,a.create,a.native)
