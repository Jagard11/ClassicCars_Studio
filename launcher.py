#!/usr/bin/env python3
"""Start ClassicCars Studio in its own window.

Linux, macOS, and Git Bash: ./launcher.sh
Windows: launcher.bat

Extra arguments are forwarded to classiccars-studio. If the binary is
missing, the matching build script runs first. The program opens a
standalone window unless the caller passes --headless, --no-window,
--server-only, or --no-open.

On Linux, Conda entries are removed from LD_LIBRARY_PATH before the binary
starts so GTK 3 and WebKitGTK load the system libglib and libstdc++.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def studio_dir() -> Path:
    return Path(__file__).resolve().parent


def binary_path(studio: Path) -> Path:
    name = "classiccars-studio.exe" if os.name == "nt" else "classiccars-studio"
    return studio / name


def conda_lib_entry(entry: str) -> bool:
    parts = [part.lower() for part in entry.replace("\\", "/").split("/") if part]
    for name in ("miniconda3", "anaconda3"):
        try:
            index = parts.index(name)
        except ValueError:
            continue
        if "lib" in parts[index + 1:]:
            return True
    return False


def scrub_conda_ld_library_path(env: dict[str, str]) -> None:
    """Drop miniconda3/lib and anaconda3/lib from LD_LIBRARY_PATH on Linux."""
    if not sys.platform.startswith("linux"):
        return
    raw = env.get("LD_LIBRARY_PATH")
    if not raw:
        return
    pieces = [part for part in raw.split(os.pathsep) if part]
    kept = [part for part in pieces if not conda_lib_entry(part)]
    if len(kept) == len(pieces):
        return
    if kept:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(kept)
    else:
        env.pop("LD_LIBRARY_PATH", None)


def invoke_build(studio: Path) -> int:
    if os.name == "nt":
        script = studio / "build.bat"
        completed = subprocess.run(["cmd", "/c", str(script)], cwd=str(studio))
    else:
        script = studio / "build.sh"
        completed = subprocess.run([str(script)], cwd=str(studio))
    return completed.returncode


def main(argv: list[str]) -> int:
    studio = studio_dir()
    binary = binary_path(studio)
    if not binary.is_file():
        print(f"{binary.name} is not built yet. Running the build script.")
        code = invoke_build(studio)
        if code != 0:
            return code
    if not binary.is_file():
        print(f"Error: {binary} was not produced.", file=sys.stderr)
        return 1
    try:
        os.chdir(studio)
    except OSError as exc:
        print(f"Error: could not use {studio} as the working directory: {exc}", file=sys.stderr)
        return 1
    env = os.environ.copy()
    scrub_conda_ld_library_path(env)
    try:
        os.execve(str(binary), [str(binary), *argv], env)
    except OSError as exc:
        print(f"Error: could not start {binary}: {exc}", file=sys.stderr)
        return 1
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
