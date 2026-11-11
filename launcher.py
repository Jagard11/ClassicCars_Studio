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

import json
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

STAGED_WHITELIST = {
    "classiccars-studio",
    "classiccars-studio.exe",
    "VERSION",
    "README.md",
    "launcher.py",
    "launcher.sh",
    "launcher.bat",
    "launch-studio.sh",
    "web",
    "preview",
    "assets",
    "installers",
}


def studio_dir() -> Path:
    return Path(__file__).resolve().parent


def binary_path(studio: Path) -> Path:
    name = "classiccars-studio.exe" if os.name == "nt" else "classiccars-studio"
    return studio / name


def parse_listen_port(argv: list[str]) -> int:
    for i, arg in enumerate(argv):
        if arg == "--port" and i + 1 < len(argv):
            try:
                return int(argv[i + 1])
            except ValueError:
                break
    return 8770


def port_in_use(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.25):
            return True
    except OSError:
        return False


def apply_pending_update(studio: Path, argv: list[str]) -> None:
    old_exe = studio / "classiccars-studio.exe.old"
    if old_exe.exists():
        try:
            old_exe.unlink()
        except OSError:
            pass
    pending = studio / "state" / "pending-update"
    ready_file = pending / "ready.json"
    staged_dir = pending / "staged"
    if not ready_file.is_file() or not staged_dir.is_dir():
        return
    if any(arg in ("--help", "-h", "--version", "--check") for arg in argv):
        return
    if port_in_use(parse_listen_port(argv)):
        return
    try:
        meta = json.loads(ready_file.read_text(encoding="utf-8"))
        version = meta.get("version") or "update"
    except Exception:
        version = "update"
    print(f"Applying staged ClassicCars Studio update (v{version})...")
    try:
        for root_str, _, files in os.walk(staged_dir):
            root_path = Path(root_str)
            rel_dir = root_path.relative_to(staged_dir)
            parts = () if str(rel_dir) == "." else rel_dir.parts
            if parts and (parts[0] == "state" or parts[0] not in STAGED_WHITELIST):
                continue
            target_dir = studio if not parts else studio.joinpath(*parts)
            target_dir.mkdir(parents=True, exist_ok=True)
            for fname in files:
                if not parts and fname not in STAGED_WHITELIST:
                    continue
                src = root_path / fname
                dst = target_dir / fname
                if fname == "classiccars-studio.exe" and dst.exists():
                    old = dst.with_name("classiccars-studio.exe.old")
                    try:
                        if old.exists():
                            old.unlink()
                        dst.replace(old)
                    except OSError:
                        pass
                tmp = dst.with_name(fname + ".upd-tmp")
                shutil.copy2(src, tmp)
                tmp.replace(dst)
        shutil.rmtree(pending, ignore_errors=True)
    except OSError as exc:
        print(f"Warning: could not finish applying staged update: {exc}", file=sys.stderr)


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
    apply_pending_update(studio, argv)
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
