#!/usr/bin/env python3
"""Build ClassicCars Studio and keep VERSION in step with the binary.

From the ClassicCars Studio repository:

    Linux or macOS: ./build.sh
    Windows: build.bat

Both launchers run this file. A normal run increments the patch number
(0.0.1 -> 0.0.2). Edit VERSION by hand to change MAJOR or MINOR;
the next build still increments PATCH. --rebuild deletes build output
first, then increments and compiles from scratch. --no-bump compiles the
version already stored in VERSION.

The native binary is classiccars-studio on Linux and macOS, and
classiccars-studio.exe on Windows. When x86_64-w64-mingw32-gcc is on PATH
during a Linux or macOS build, classiccars-studio.exe is updated too.

On Linux, the build also refreshes
~/.local/share/applications/classiccars-studio.desktop and the desktop
menu cache, so ClassicCars Studio stays in the Start menu.

On Linux, make and the compiler run with Conda's toolchain variables
removed and CC set to /usr/bin/cc, so a `(base)` shell cannot inject its
older sysroot or an rpath into the Conda lib directory.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


HELP = """\
Usage: build.py [--rebuild] [--no-bump]

  (no flags)  Increment PATCH in VERSION and build.
  --rebuild   Remove build output, then increment PATCH and rebuild.
  --no-bump   Build the version already in VERSION.
"""

FALLBACK_SOURCES = (
    "src/main.c",
    "src/http.c",
    "src/router.c",
    "src/ssi.c",
    "src/proxy.c",
    "src/settings.c",
    "src/config.c",
    "src/app_config.c",
    "src/json.c",
    "src/gallery.c",
    "src/preview_draft.c",
    "src/project_search.c",
    "src/git_scm.c",
    "src/terminal.c",
    "src/fs_rename.c",
    "src/fs_ops.c",
    "src/runtime_versions.c",
    "src/updater.c",
    "src/api.c",
    "src/window.c",
)

VERSION_RE = re.compile(r"(\d+)\.(\d+)\.(\d+)")

# Exported by Conda activation and by conda's compiler packages. They select
# an older cc, add its sysroot to the preprocessor, and pass -rpath/-L into
# the Conda lib directory.
CONDA_COMPILER_VARS = (
    "CC",
    "CXX",
    "CFLAGS",
    "CXXFLAGS",
    "CPPFLAGS",
    "LDFLAGS",
    "AR",
    "LD",
    "RANLIB",
    "NM",
    "STRIP",
    "CONDA_BUILD_SYSROOT",
    "HOST",
    "build_alias",
    "host_alias",
)


def conda_bin_dir(entry: str) -> bool:
    parts = [part for part in entry.replace("\\", "/").split("/") if part]
    if not parts or parts[-1] != "bin":
        return False
    return "miniconda3" in parts or "anaconda3" in parts


def prefer_usr_bin(path_value: str) -> str:
    """Put /usr/bin ahead of any miniconda3/bin or anaconda3/bin entry."""
    entries = [part for part in path_value.split(os.pathsep) if part]
    usr_bin = "/usr/bin"
    rest = [part for part in entries if part.rstrip("/") != usr_bin]
    plain = [part for part in rest if not conda_bin_dir(part)]
    conda = [part for part in rest if conda_bin_dir(part)]
    return os.pathsep.join([usr_bin, *plain, *conda])


def linux_build_env() -> dict[str, str] | None:
    """Environment for make and the C compiler.

    Linux children drop Conda compiler variables and compile with the system
    cc. Other platforms inherit the parent environment.
    """
    if not sys.platform.startswith("linux"):
        return None
    env = os.environ.copy()
    for name in CONDA_COMPILER_VARS:
        env.pop(name, None)
    for candidate in ("/usr/bin/cc", "/usr/bin/gcc"):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            env["CC"] = candidate
            break
    env["PATH"] = prefer_usr_bin(env.get("PATH", ""))
    return env


def native_compiler() -> str | None:
    if sys.platform.startswith("linux"):
        for candidate in ("/usr/bin/cc", "/usr/bin/gcc"):
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                return candidate
    return shutil.which("gcc") or shutil.which("cc") or shutil.which("clang")


def fail(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(code)


def run(cmd: list[str], cwd: Path) -> None:
    print("+ " + " ".join(cmd))
    completed = subprocess.run(cmd, cwd=str(cwd), env=linux_build_env())
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def write_text(path: Path, text: str) -> None:
    tmp = path.with_name(path.name + ".tmp")
    try:
        tmp.write_bytes(text.encode("utf-8"))
        tmp.replace(path)
    except OSError as exc:
        try:
            tmp.unlink()
        except OSError:
            pass
        fail(f"Could not write {path}: {exc}")


def write_if_changed(path: Path, text: str) -> bool:
    if path.is_file():
        current = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        if current == text.encode("utf-8"):
            return False
    write_text(path, text)
    return True


def format_version(parts: tuple[int, int, int]) -> str:
    return f"{parts[0]}.{parts[1]}.{parts[2]}"


def read_version(path: Path) -> tuple[int, int, int]:
    if not path.is_file():
        return (0, 0, 0)
    text = path.read_text(encoding="utf-8-sig").strip()
    if text == "":
        return (0, 0, 0)
    match = VERSION_RE.fullmatch(text)
    if not match:
        fail(
            f"{path} must contain MAJOR.MINOR.PATCH, for example 0.0.1.\n"
            f"Found: {text!r}"
        )
    return (int(match.group(1)), int(match.group(2)), int(match.group(3)))


def header_text(version: str) -> str:
    return (
        "/* Generated by build.py from VERSION. Do not edit. */\n"
        "#ifndef STUDIO_VERSION_H\n"
        "#define STUDIO_VERSION_H\n"
        "\n"
        f'#define STUDIO_VERSION "{version}"\n'
        "\n"
        "#endif\n"
    )


def find_make() -> str | None:
    for name in ("make", "gmake", "mingw32-make"):
        found = shutil.which(name)
        if found:
            return found
    return None


def use_make(studio: Path) -> bool:
    if not (studio / "Makefile").is_file() or not find_make():
        return False
    if os.name == "nt" and not shutil.which("sh") and not shutil.which("rm"):
        return False
    return True


def remove_file(path: Path) -> None:
    if not path.is_file() and not path.is_symlink():
        return
    try:
        path.unlink()
    except OSError as exc:
        fail(
            f"Could not remove {path}: {exc}\n"
            "Close ClassicCars Studio if it is running, then rebuild."
        )


def remove_tree(path: Path) -> None:
    if not path.exists():
        return
    try:
        shutil.rmtree(path)
    except OSError as exc:
        fail(f"Could not remove {path}: {exc}")


def clean(studio: Path) -> None:
    print("Cleaning build output")
    make = find_make()
    if make and (studio / "Makefile").is_file() and use_make(studio):
        completed = subprocess.run(
            [make, "-C", str(studio), "clean"],
            env=linux_build_env(),
        )
        if completed.returncode != 0:
            print("make clean did not succeed; removing build directly.", file=sys.stderr)
    remove_tree(studio / "build")
    remove_file(studio / "classiccars-studio")
    remove_file(studio / "classiccars-studio.exe")


def parse_sources(makefile: Path) -> list[str]:
    if not makefile.is_file():
        return list(FALLBACK_SOURCES)
    capturing = False
    collected: list[str] = []
    for line in makefile.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not capturing:
            if not stripped.startswith("SRCS"):
                continue
            capturing = True
            _, _, rest = stripped.partition("=")
            rest = rest.lstrip(":").strip()
            if rest.endswith("\\"):
                rest = rest[:-1].strip()
            if rest:
                collected.append(rest)
            if not stripped.endswith("\\"):
                break
            continue
        continued = stripped.endswith("\\")
        chunk = stripped[:-1].strip() if continued else stripped
        if chunk:
            collected.append(chunk)
        if not continued:
            break
    text = " ".join(collected)
    text = text.replace("$(SRC_DIR)", "src").replace("${SRC_DIR}", "src")
    sources = [part for part in text.split() if part.endswith(".c")]
    return sources or list(FALLBACK_SOURCES)


def native_output_name() -> str:
    return "classiccars-studio.exe" if os.name == "nt" else "classiccars-studio"


def find_built_binary(studio: Path) -> Path:
    names = ["classiccars-studio.exe", "classiccars-studio"] if os.name == "nt" else ["classiccars-studio"]
    roots = (
        studio / "build",
        studio / "build" / "Debug",
        studio / "build" / "Release",
        studio / "build" / "RelWithDebInfo",
        studio / "build" / "MinSizeRel",
        studio,
    )
    for root in roots:
        for name in names:
            path = root / name
            if path.is_file():
                return path
    fail("The build finished, but classiccars-studio was not found.")
    raise AssertionError("unreachable")


def publish(studio: Path) -> Path:
    src = find_built_binary(studio)
    dest = studio / native_output_name()
    if src.resolve() != dest.resolve():
        remove_file(dest)
        shutil.copy2(src, dest)
    dest.chmod(dest.stat().st_mode | 0o111)
    print(f"Built {dest}")
    return dest


def compile_with(cc: str, studio: Path, output_name: str, windows: bool) -> None:
    sources = parse_sources(studio / "Makefile")
    missing = [rel for rel in sources if not (studio / rel).is_file()]
    if missing:
        fail("Missing sources: " + ", ".join(missing))
    output = studio / output_name
    remove_file(output)
    cmd = [cc, "-std=c11", "-Wall", "-Wextra", "-O2"]
    if windows:
        cmd.extend([
            "-DWIN32_LEAN_AND_MEAN",
            "-D_WIN32_WINNT=0x0600",
            "-mwindows",
            "-Wl,--subsystem,windows",
            "-Wl,-e,mainCRTStartup",
        ])
    else:
        cmd.extend([
            "-D_POSIX_C_SOURCE=200809L",
            "-D_DEFAULT_SOURCE",
            "-D_XOPEN_SOURCE=700",
            "-pthread",
        ])
    cmd.extend(["-o", output_name])
    cmd.extend(sources)
    if windows:
        # -static drops the MinGW runtime DLLs. The libraries below are Windows itself.
        cmd.extend([
            "-static",
            "-lws2_32",
            "-lgdi32",
            "-luser32",
            "-lole32",
            "-loleaut32",
            "-luuid",
            "-lshlwapi",
            "-ladvapi32",
        ])
    else:
        cmd.extend(["-pthread", "-ldl"])
    run(cmd, studio)
    output.chmod(output.stat().st_mode | 0o111)
    print(f"Built {output}")


def compile_native(studio: Path) -> None:
    # `make all` is the POSIX build. On Windows, compile the .exe directly.
    if os.name != "nt" and use_make(studio):
        make = find_make()
        assert make is not None
        run([make, "-C", str(studio)], studio)
        publish(studio)
        return
    cc = native_compiler()
    if not cc:
        fail("No C compiler found. Install gcc, or build from a shell that has make and gcc.")
    compile_with(cc, studio, native_output_name(), windows=os.name == "nt")


def compile_windows_cross(studio: Path) -> None:
    if os.name == "nt":
        return
    cc = shutil.which("x86_64-w64-mingw32-gcc")
    if not cc:
        local_mingw = Path.home() / ".local/opt/llvm-mingw/bin/x86_64-w64-mingw32-gcc"
        if local_mingw.is_file() and os.access(local_mingw, os.X_OK):
            cc = str(local_mingw)
    if cc:
        compile_with(cc, studio, "classiccars-studio.exe", windows=True)
        return
    zig = shutil.which("zig")
    if not zig:
        print("x86_64-w64-mingw32-gcc not found; skipping classiccars-studio.exe")
        print("Install gcc-mingw-w64 or zig, or run: make windows")
        return
    sources = parse_sources(studio / "Makefile")
    output = studio / "classiccars-studio.exe"
    remove_file(output)
    cmd = [
        zig, "cc", "-target", "x86_64-windows-gnu",
        "-std=c11", "-Wall", "-Wextra", "-O2",
        "-DWIN32_LEAN_AND_MEAN",
        "-Wl,--subsystem,windows",
        "-Wl,-e,mainCRTStartup",
        "-o", "classiccars-studio.exe",
        *sources,
        "-static",
        "-lws2_32",
        "-lgdi32",
        "-luser32",
        "-lole32",
        "-loleaut32",
        "-luuid",
        "-lshlwapi",
        "-ladvapi32",
    ]
    run(cmd, studio)
    output.chmod(output.stat().st_mode | 0o111)
    print(f"Built {output}")


def desktop_quote(path: str) -> str:
    if any(ch in path for ch in ' \t"'):
        return '"' + path.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return path


def run_optional(cmd: list[str]) -> int:
    print("+ " + " ".join(cmd))
    completed = subprocess.run(cmd)
    if completed.returncode != 0:
        print(f"{Path(cmd[0]).name} exited {completed.returncode}.", file=sys.stderr)
    return completed.returncode


def scale_png(src: Path, dest: Path, size: int) -> bool:
    try:
        from PIL import Image
    except ImportError:
        return False
    resample = getattr(getattr(Image, "Resampling", Image), "NEAREST")
    try:
        with Image.open(src) as image:
            large = image.convert("RGBA").resize((size, size), resample)
            dest.parent.mkdir(parents=True, exist_ok=True)
            large.save(dest, format="PNG")
    except OSError as exc:
        print(f"Could not scale {src} to {size}px: {exc}", file=sys.stderr)
        return False
    return dest.is_file() and dest.stat().st_size > 0


def favicon_file(studio: Path) -> Path | None:
    for rel in ("assets/favicon-32x32.png", "favicon-32x32.png"):
        path = studio / rel
        if path.is_file():
            return path.resolve()
    return None


def install_menu_icons(studio: Path) -> str:
    """Install the favicon where Linux menus look, and return the Icon= value.

    The desktop entry points at a readable PNG. Copies also go in the user
    hicolor theme at 32px and 256px so docks can pick a sharper size.
    """
    favicon = favicon_file(studio)
    if favicon is None:
        print(f"Menu icon not found under {studio / 'assets'}", file=sys.stderr)
        return "classiccars-studio"
    root = Path.home() / ".local/share/icons/hicolor"
    dest32 = root / "32x32/apps/classiccars-studio.png"
    dest256 = root / "256x256/apps/classiccars-studio.png"
    dest32.parent.mkdir(parents=True, exist_ok=True)
    dest256.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(favicon, dest32)
    dest32.chmod(0o644)
    owned = studio / "assets" / "icon-256.png"
    if owned.is_file():
        shutil.copyfile(owned, dest256)
        dest256.chmod(0o644)
        return str(dest256.resolve())
    if scale_png(favicon, dest256, 256):
        dest256.chmod(0o644)
        return str(dest256.resolve())
    print("Pillow could not scale the menu icon; the desktop entry uses favicon-32x32.png.")
    return str(favicon)


def desktop_file_text(studio: Path, icon: str) -> str:
    launcher = studio / "launcher.sh"
    lines = [
        "[Desktop Entry]",
        "Version=1.0",
        "Type=Application",
        "Name=ClassicCars Studio",
        "GenericName=Site preview",
        "Comment=Open ClassicCars Studio in its own window",
        "Exec=" + desktop_quote(str(launcher)),
        "TryExec=" + desktop_quote(str(launcher)),
        "Path=" + desktop_quote(str(studio.resolve())),
        "Icon=" + desktop_quote(icon),
        "Terminal=false",
        "Categories=Development;WebDevelopment;Utility;Graphics;",
        "StartupWMClass=classiccars-studio",
        "StartupNotify=true",
        "",
    ]
    return "\n".join(lines)


def mark_desktop_trusted(path: Path) -> None:
    gio = shutil.which("gio")
    if not gio or not path.is_file():
        return
    completed = subprocess.run(
        [gio, "set", str(path), "metadata::trusted", "true"],
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout).strip()
        print(f"Could not mark {path} trusted.", file=sys.stderr)
        if detail:
            print(detail, file=sys.stderr)
        return
    print(f"Trusted {path}")


def install_linux_menu(desktop: Path) -> None:
    if not sys.platform.startswith("linux"):
        return
    apps = Path.home() / ".local/share/applications"
    apps.mkdir(parents=True, exist_ok=True)
    installed = apps / "classiccars-studio.desktop"
    text = desktop.read_text(encoding="utf-8")
    if not text.endswith("\n"):
        text += "\n"
    if write_if_changed(installed, text):
        print(f"Wrote {installed}")
    else:
        print(f"Menu entry already current: {installed}")
    desktop.chmod(0o755)
    installed.chmod(0o755)
    xdg = shutil.which("xdg-desktop-menu")
    if xdg:
        run_optional([xdg, "install", "--novendor", str(desktop.resolve())])
    # xdg-desktop-menu copies using the source basename. Keep a single
    # classiccars-studio.desktop entry so the menu does not list Studio twice.
    duplicate = apps / desktop.name
    if duplicate.is_file() and duplicate.resolve() != installed.resolve():
        duplicate.unlink()
        print(f"Removed duplicate menu entry {duplicate.name}")
    installed.chmod(0o755)
    validator = shutil.which("desktop-file-validate")
    if validator:
        run_optional([validator, str(installed)])
    updater = shutil.which("update-desktop-database")
    if updater:
        run_optional([updater, str(apps)])
    if xdg:
        run_optional([xdg, "forceupdate"])
    # gtk-update-icon-cache needs a local index.theme, and writing one would
    # shadow the system hicolor theme. Those size folders are already in the
    # system index, so GTK finds the icons without a user cache.
    icon_cache = shutil.which("gtk-update-icon-cache")
    hicolor = Path.home() / ".local/share/icons/hicolor"
    if icon_cache and (hicolor / "index.theme").is_file():
        run_optional([icon_cache, "-f", "-t", str(hicolor)])
    mark_desktop_trusted(desktop)
    mark_desktop_trusted(installed)


def ensure_desktop(studio: Path) -> None:
    desktop = studio / "ClassicCars-Studio.desktop"
    icon_path = favicon_file(studio)
    icon = str(icon_path) if icon_path else "classiccars-studio"
    if sys.platform.startswith("linux"):
        icon = install_menu_icons(studio)
    content = desktop_file_text(studio, icon)
    if write_if_changed(desktop, content):
        print(f"Wrote {desktop}")
    try:
        desktop.chmod(0o755)
    except OSError as exc:
        print(f"Could not mark {desktop} executable: {exc}", file=sys.stderr)
    install_linux_menu(desktop)


def apply_version(studio: Path, bump: bool) -> str:
    path = studio / "VERSION"
    parts = read_version(path)
    if bump:
        old = format_version(parts)
        parts = (parts[0], parts[1], parts[2] + 1)
        print(f"Version {old} -> {format_version(parts)}")
    else:
        print(f"Version {format_version(parts)} (not incremented)")
    version = format_version(parts)
    write_if_changed(path, version + "\n")
    header = studio / "src" / "version.h"
    if write_if_changed(header, header_text(version)):
        print(f"Wrote {header}")
    return version


def main(argv: list[str]) -> int:
    rebuild = False
    bump = True
    for arg in argv:
        if arg == "--rebuild":
            rebuild = True
        elif arg == "--no-bump":
            bump = False
        elif arg in ("-h", "--help"):
            print(HELP, end="")
            return 0
        else:
            print(f"Unknown argument: {arg}", file=sys.stderr)
            print(HELP, end="", file=sys.stderr)
            return 2
    studio = Path(__file__).resolve().parent
    if rebuild:
        clean(studio)
    apply_version(studio, bump)
    compile_native(studio)
    compile_windows_cross(studio)
    ensure_desktop(studio)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
