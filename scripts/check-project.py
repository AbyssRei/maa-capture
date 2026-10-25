"""Validate public package versions, project names and Conventional Commits."""
from pathlib import Path
import json
import re
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)")
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
version = re.search(r"project\(maa-capture\s+VERSION\s+(\S+)", cmake)[1]
assert VERSION.fullmatch(version), f"Invalid SemVer: {version}"
pyproject = tomllib.loads((ROOT / "bindings/python/pyproject.toml").read_text(encoding="utf-8"))
cargo = tomllib.loads((ROOT / "bindings/rust/Cargo.toml").read_text(encoding="utf-8"))
node = json.loads((ROOT / "bindings/nodejs/package.json").read_text(encoding="utf-8"))
init = (ROOT / "bindings/python/src/maa_capture/__init__.py").read_text(encoding="utf-8")
csproj = (ROOT / "bindings/csharp/test/test.csproj").read_text(encoding="utf-8")
versions = {
    "CMake": version,
    "Python": pyproject["project"]["version"],
    "Python import": re.search(r'__version__ = "([^"]+)"', init)[1],
    "Rust": cargo["package"]["version"],
    "Node.js": node["version"],
    "C#": re.search(r"<Version>([^<]+)</Version>", csproj)[1],
}
assert set(versions.values()) == {version}, f"Package versions differ: {versions}"
assert node["dependencies"]["koffi"].startswith("^")
assert pyproject["build-system"]["requires"][0].startswith("uv_build>=")
paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
# Construct legacy spellings so this validator does not introduce them into the tree.
legacy = re.compile(r"Maa" + r"Win32Screencap|maa_" + r"win32_screencap|maa-" + r"win32-screencap|maa\." + r"win32\.screencap", re.I)
for name in filter(None, paths):
    path = ROOT / name
    if not path.exists():
        continue
    assert not legacy.search(name), f"Legacy path: {name}"
    try:
        contents = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue
    assert not legacy.search(contents), f"Legacy project name in {name}"
log = subprocess.check_output(["git", "log", "--format=%s", "HEAD"], cwd=ROOT).decode().splitlines()
conventional = re.compile(r"(?:feat|fix|build|chore|ci|docs|style|refactor|perf|test|revert)(?:\([\w.-]+\))?!?: .+")
assert all(conventional.fullmatch(subject) for subject in log), "Invalid Conventional Commit subject"
print(f"maa-capture {version}: package versions, names and {len(log)} commit subjects OK")
