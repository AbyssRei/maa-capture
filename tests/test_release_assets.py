"""Provenance graph regression fixtures; these are not runnable SDK assets."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("release_check",ROOT/"scripts/check-release-assets.py")
release=importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="maa-provenance-test-")
        self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name)
        self.native=self.folder/"native"
        self.commit="a"*40
        version="2.0.0"
        for name in (f"maa-capture-v{version}-windows-x64.7z",f"maa-capture-v{version}-linux-x64.tar.gz",f"maa-capture-v{version}-macos-arm64.tar.gz",f"maa_capture-{version}.tar.gz"):
            (self.folder/name).write_bytes(b"hash graph fixture")
        with tarfile.open(self.folder/f"maa-capture-v{version}-source.tar.gz","w:gz") as archive:
            paths=("source/ScreencapMgr.cpp","include/maa-capture/CaptureTypes.h","CMakeLists.txt","cmake/write-provenance.cmake","tests/capture_contracts.cpp","scripts/generate-ffi.py","bindings/python/pyproject.toml","sample/external_c_manager.c","LICENSE")
            for path in paths+("SOURCE_COMMIT",):
                data=(self.commit+"\n").encode() if path=="SOURCE_COMMIT" else (ROOT/path).read_bytes()
                member=tarfile.TarInfo(f"maa-capture-v{version}/{path}");member.size=len(data);archive.addfile(member,io.BytesIO(data))
        with zipfile.ZipFile(self.folder/f"maa_capture-{version}-py3-none-any.whl","w") as wheel:
            for platform,filename in (("windows-x64","maa-capture.dll"),("linux-x64","libmaa-capture.so"),("macos-arm64","libmaa-capture.dylib")):
                base=self.native/platform;base.mkdir(parents=True)
                data=platform.encode()
                info={"version":version,"abi":2,"commit":self.commit,"native_file":filename,"native_sha256":hashlib.sha256(data).hexdigest()}
                info["system"]={"windows-x64":"Windows","linux-x64":"Linux","macos-arm64":"Darwin"}[platform]
                info["architecture"]="arm64" if platform=="macos-arm64" else "x64"
                (base/filename).write_bytes(data)
                (base/"build-manifest.json").write_text(json.dumps(info))
                wheel.writestr(f"maa_capture/_native/{platform}/{filename}",data)
                wheel.writestr(f"maa_capture/_native/{platform}/build-manifest.json",json.dumps(info))
    def test_roundtrip_and_tamper(self):
        release.check(self.folder,"2.0.0",True,self.native)
        release.check(self.folder,"2.0.0")
        (self.folder/"maa_capture-2.0.0.tar.gz").write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError,"hash mismatch"):
            release.check(self.folder,"2.0.0")
    def test_source_binary_commit_mismatch(self):
        path=self.native/"windows-x64/build-manifest.json"
        info=json.loads(path.read_text());info["commit"]="b"*40;path.write_text(json.dumps(info))
        with self.assertRaisesRegex(RuntimeError,"Native provenance mismatch"):
            release.check(self.folder,"2.0.0",True,self.native)
    def test_missing_asset(self):
        (self.folder/"maa-capture-v2.0.0-windows-x64.7z").unlink()
        with self.assertRaisesRegex(RuntimeError,"Missing or empty"):
            release.check(self.folder,"2.0.0",True,self.native)
if __name__=="__main__":
    unittest.main()
