#!/usr/bin/env python3
"""Release bundle builder and packager for ClassicCars Studio.

Packages the built Linux and Windows binaries, runtime web/preview/assets
directories, launchers, and cross-platform installers into distributable
GitHub Release archives:
  - dist/ClassicCarsStudio-<version>-linux-x86_64.tar.gz
  - dist/ClassicCarsStudio-<version>-windows-x86_64.zip
  - dist/SHA256SUMS
"""

from __future__ import annotations

import argparse
import hashlib
import os
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

COMMON_FILES = (
    "VERSION",
    "README.md",
    "launcher.py",
    "launcher.sh",
    "launcher.bat",
    "launch-studio.sh",
)

LINUX_BINARY = "classiccars-studio"
WINDOWS_BINARY = "classiccars-studio.exe"
WINDOWS_SETUP_BINARY = "ClassicCarsStudio-Setup.exe"

BUNDLE_DIRS = (
    "web",
    "preview",
    "assets",
    "installers",
)

EXECUTABLE_NAMES = {
    "classiccars-studio",
    "classiccars-studio.exe",
    "ClassicCarsStudio-Setup.exe",
    "launcher.py",
    "launcher.sh",
    "launch-studio.sh",
    "install.py",
    "install-linux.sh",
    "package_release.py",
}

IGNORED_NAMES = {
    "__pycache__",
    ".DS_Store",
}

IGNORED_SUFFIXES = (
    ".pyc",
    ".pyo",
    ".tmp",
    ".pdb",
)


def repo_root() -> Path:
    here = Path(__file__).resolve().parent
    if here.name == "installers":
        return here.parent
    return here


def read_version(root: Path) -> str:
    version_file = root / "VERSION"
    if not version_file.is_file():
        raise SystemExit(f"Error: VERSION file not found at {version_file}")
    version = version_file.read_text(encoding="utf-8-sig").strip()
    if not version:
        raise SystemExit(f"Error: VERSION file is empty at {version_file}")
    return version


def should_skip(path: Path) -> bool:
    if path.name in IGNORED_NAMES:
        return True
    if any(path.name.endswith(suffix) for suffix in IGNORED_SUFFIXES):
        return True
    return False


def iter_directory_files(base_dir: Path) -> list[Path]:
    collected: list[Path] = []
    for item in sorted(base_dir.rglob("*")):
        if any(part in IGNORED_NAMES for part in item.parts):
            continue
        if item.is_file() and not should_skip(item):
            collected.append(item)
    return collected


def collect_bundle_entries(root: Path, include_linux: bool, include_windows: bool) -> list[tuple[Path, str]]:
    entries: list[tuple[Path, str]] = []
    if include_linux:
        linux_bin = root / LINUX_BINARY
        if linux_bin.is_file():
            entries.append((linux_bin, LINUX_BINARY))
    if include_windows:
        win_bin = root / WINDOWS_BINARY
        if win_bin.is_file():
            entries.append((win_bin, WINDOWS_BINARY))
        win_setup = root / WINDOWS_SETUP_BINARY
        if win_setup.is_file():
            entries.append((win_setup, WINDOWS_SETUP_BINARY))

    for rel in COMMON_FILES:
        src = root / rel
        if src.is_file():
            entries.append((src, rel))

    for dirname in BUNDLE_DIRS:
        dir_path = root / dirname
        if not dir_path.is_dir():
            continue
        for file_path in iter_directory_files(dir_path):
            rel_path = file_path.relative_to(root).as_posix()
            entries.append((file_path, rel_path))

    return entries


def build_tar_gz(
    archive_path: Path,
    prefix: str,
    entries: list[tuple[Path, str]],
) -> None:
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive_path, "w:gz") as tar:
        for src_path, rel_path in entries:
            arcname = f"{prefix}/{rel_path}"
            info = tar.gettarinfo(str(src_path), arcname=arcname)
            info.uid = 0
            info.gid = 0
            info.uname = "root"
            info.gname = "root"
            if src_path.name in EXECUTABLE_NAMES or src_path.suffix in (".sh", ".py"):
                info.mode = 0o755
            else:
                info.mode = 0o644
            with src_path.open("rb") as fh:
                tar.addfile(info, fh)


def build_zip(
    archive_path: Path,
    prefix: str,
    entries: list[tuple[Path, str]],
) -> None:
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for src_path, rel_path in entries:
            arcname = f"{prefix}/{rel_path}"
            zinfo = zipfile.ZipInfo.from_file(str(src_path), arcname=arcname)
            zinfo.compress_type = zipfile.ZIP_DEFLATED
            if src_path.name in EXECUTABLE_NAMES or src_path.suffix in (".sh", ".py"):
                zinfo.external_attr = (stat.S_IFREG | 0o755) << 16
            else:
                zinfo.external_attr = (stat.S_IFREG | 0o644) << 16
            zf.writestr(zinfo, src_path.read_bytes())


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_sha256sums(out_dir: Path, archives: list[Path]) -> Path:
    lines: list[str] = []
    for archive in archives:
        if archive.is_file():
            lines.append(f"{sha256_file(archive)}  {archive.name}")
    sums_path = out_dir / "SHA256SUMS"
    sums_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return sums_path


def verify_archives(
    linux_tar: Path,
    windows_zip: Path,
    sums_path: Path,
    prefix: str,
) -> None:
    required_common = {
        f"{prefix}/VERSION",
        f"{prefix}/launcher.py",
        f"{prefix}/launcher.sh",
        f"{prefix}/launcher.bat",
        f"{prefix}/web/index.html",
        f"{prefix}/preview/prepend.php",
        f"{prefix}/installers/install.py",
        f"{prefix}/installers/install-linux.sh",
        f"{prefix}/installers/install-windows.ps1",
        f"{prefix}/installers/install-windows.bat",
        f"{prefix}/installers/package_release.py",
    }

    if linux_tar.is_file():
        with tarfile.open(linux_tar, "r:gz") as tar:
            members = {m.name: m for m in tar.getmembers()}
        missing = (required_common | {f"{prefix}/{LINUX_BINARY}"}) - set(members.keys())
        if missing:
            raise SystemExit(f"Error: Linux archive is missing entries: {sorted(missing)}")
        for exe_rel in (LINUX_BINARY, "launcher.sh", "installers/install-linux.sh", "installers/install.py"):
            member = members[f"{prefix}/{exe_rel}"]
            if not (member.mode & 0o111):
                raise SystemExit(f"Error: {exe_rel} in Linux archive is not marked executable")

    if windows_zip.is_file():
        with zipfile.ZipFile(windows_zip, "r") as zf:
            names = set(zf.namelist())
        missing = (required_common | {f"{prefix}/{WINDOWS_BINARY}", f"{prefix}/{WINDOWS_SETUP_BINARY}"}) - names
        if missing:
            raise SystemExit(f"Error: Windows archive is missing entries: {sorted(missing)}")

    if not sums_path.is_file():
        raise SystemExit(f"Error: SHA256SUMS was not generated at {sums_path}")

    for line in sums_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        expected_hash, _, filename = line.partition("  ")
        target = sums_path.parent / filename.strip()
        actual_hash = sha256_file(target)
        if actual_hash != expected_hash:
            raise SystemExit(f"Error: SHA256 mismatch for {filename}: {actual_hash} != {expected_hash}")


def package_releases(root: Path, out_dir: Path) -> tuple[list[Path], Path]:
    version = read_version(root)
    prefix = f"ClassicCarsStudio-{version}"

    linux_tar = out_dir / f"ClassicCarsStudio-{version}-linux-x86_64.tar.gz"
    windows_zip = out_dir / f"ClassicCarsStudio-{version}-windows-x86_64.zip"

    created: list[Path] = []

    if (root / LINUX_BINARY).is_file():
        linux_entries = collect_bundle_entries(root, include_linux=True, include_windows=False)
        build_tar_gz(linux_tar, prefix, linux_entries)
        created.append(linux_tar)
        print(f"Packaged Linux release:   {linux_tar} ({linux_tar.stat().st_size:,} bytes)")
    else:
        print(f"Warning: {LINUX_BINARY} not found in {root}; skipping Linux archive.", file=sys.stderr)

    if (root / WINDOWS_BINARY).is_file():
        win_entries = collect_bundle_entries(root, include_linux=False, include_windows=True)
        build_zip(windows_zip, prefix, win_entries)
        created.append(windows_zip)
        print(f"Packaged Windows release: {windows_zip} ({windows_zip.stat().st_size:,} bytes)")
    else:
        print(f"Warning: {WINDOWS_BINARY} not found in {root}; skipping Windows archive.", file=sys.stderr)

    if not created:
        raise SystemExit("Error: no release archives could be created because neither binary was found.")

    sums_path = write_sha256sums(out_dir, created)
    verify_archives(linux_tar, windows_zip, sums_path, prefix)
    print(f"Wrote checksums:          {sums_path}")
    return created, sums_path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Package ClassicCars Studio Linux and Windows release bundles.",
    )
    parser.add_argument(
        "--out-dir",
        "-o",
        help="Output directory for release archives (default: <repo>/dist).",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Run build.py --no-bump before packaging.",
    )
    parser.add_argument(
        "--check",
        "--dry-run",
        dest="check",
        action="store_true",
        help="Build and verify release archives and SHA256SUMS in a temporary directory without modifying dist/.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    root = repo_root()

    if args.build:
        completed = subprocess.run([sys.executable, str(root / "build.py"), "--no-bump"], cwd=str(root))
        if completed.returncode != 0:
            return completed.returncode

    if args.check:
        with tempfile.TemporaryDirectory(prefix="ccs-release-check-") as tmp:
            tmp_out = Path(tmp)
            archives, sums = package_releases(root, tmp_out)
            print("\nRelease check passed! Verified archives and SHA256SUMS:")
            print(sums.read_text(encoding="utf-8"), end="")
            return 0

    out_dir = Path(args.out_dir).expanduser().resolve() if args.out_dir else (root / "dist")
    _, sums = package_releases(root, out_dir)
    print("\nSHA256SUMS:")
    print(sums.read_text(encoding="utf-8"), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
