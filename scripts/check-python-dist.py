"""Check the repository links actually embedded in both Python distributions."""
from email import policy
from email.parser import BytesParser
from pathlib import Path
import sys
import tarfile
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
project = tomllib.loads((ROOT / "bindings/python/pyproject.toml").read_text(encoding="utf-8"))["project"]
repository_url = "https://github.com/AbyssRei/maa-capture"
version = project["version"]
require_native = "--require-native" in sys.argv[1:]
native = ("windows-x64/maa-capture.dll", "linux-x64/libmaa-capture.so", "macos-arm64/libmaa-capture.dylib")
dist = Path(sys.argv[1] if len(sys.argv) > 1 else "dist-pypi")


def check_metadata(data, filename):
    message = BytesParser(policy=policy.default).parsebytes(data)
    assert message["Name"] == "maa-capture", filename
    assert message["Version"] == version, filename
    urls = dict(value.split(", ", 1) for value in message.get_all("Project-URL", []))
    assert urls == project["urls"], (filename, urls)
    assert all(url == repository_url for url in urls.values()), filename
    description = message.get_content()
    assert f"[maa-capture]({repository_url})" in description, filename
    assert "codeberg.org/AbyssRei/" not in description, filename


wheel = dist / f"maa_capture-{version}-py3-none-any.whl"
with zipfile.ZipFile(wheel) as archive:
    check_metadata(archive.read(f"maa_capture-{version}.dist-info/METADATA"), wheel.name)
    if require_native:
        for library in native:
            assert archive.getinfo("maa_capture/_native/" + library).file_size > 0, library

sdist = dist / f"maa_capture-{version}.tar.gz"
with tarfile.open(sdist) as archive:
    prefix = f"maa_capture-{version}/"
    check_metadata(archive.extractfile(prefix + "PKG-INFO").read(), sdist.name)
    readme = archive.extractfile(prefix + "README.md").read().decode("utf-8")
    assert f"[maa-capture]({repository_url})" in readme, sdist.name
    assert "codeberg.org/AbyssRei/" not in readme, sdist.name
    if require_native:
        for library in native:
            assert archive.getmember(prefix + "src/maa_capture/_native/" + library).size > 0, library

print(f"Python {version}: wheel and sdist metadata and description repository links OK")
