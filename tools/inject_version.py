# Reads the top-level VERSION file and appends the current git short SHA,
# then exposes the result to firmware as the FW_VERSION_STR preprocessor
# define.  Without this, FW_VERSION was hardcoded in display.cpp and stayed
# stale through every update.
#
# Registered from platformio.ini:
#   extra_scripts = pre:tools/inject_version.py
#
# Usage in C++: include the macro directly, e.g.
#   const char* FW_VERSION = FW_VERSION_STR;

import subprocess
from pathlib import Path

Import("env")  # noqa: F821 — provided by PlatformIO SCons harness

def _load_version(project_dir: Path) -> str:
    version_file = project_dir / "VERSION"
    try:
        return version_file.read_text().strip() or "unknown"
    except OSError:
        return "unknown"

def _load_git_sha(project_dir: Path) -> str:
    try:
        sha = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
        ).decode().strip()
        return sha or ""
    except (OSError, subprocess.CalledProcessError):
        return ""

def _load_git_dirty(project_dir: Path) -> bool:
    try:
        status = subprocess.check_output(
            ["git", "status", "--porcelain"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
        ).decode()
        return bool(status.strip())
    except (OSError, subprocess.CalledProcessError):
        return False

project_dir = Path(env["PROJECT_DIR"])  # noqa: F821
version = _load_version(project_dir)
sha = _load_git_sha(project_dir)
dirty = _load_git_dirty(project_dir)

label = f"v{version}"
if sha:
    label = f"{label}+{sha}"
if dirty:
    label = f"{label}-dirty"

# CPPDEFINES wants the value already quoted so the string reaches C++
# intact through the preprocessor.
env.Append(CPPDEFINES=[("FW_VERSION_STR", env.StringifyMacro(label))])  # noqa: F821

print(f"FW_VERSION_STR = {label}")
