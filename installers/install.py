#!/usr/bin/env python3
"""Cross-platform installer and uninstaller engine for ClassicCars Studio.

Supports both an interactive GUI location picker (using tkinter when a display
is available) and an interactive terminal prompt or non-interactive CLI flags
(--dir / --prefix, --yes, --no-menu, --no-desktop, --no-path, --uninstall)
for Linux and Windows installations.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import stat
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

APP_NAME = "ClassicCars Studio"
APP_SLUG = "classiccars-studio"
DESKTOP_FILENAME = "ClassicCars-Studio.desktop"
MANIFEST_FILENAME = ".install-manifest.json"

PAYLOAD_FILES = (
    "classiccars-studio",
    "classiccars-studio.exe",
    "launcher.py",
    "launcher.sh",
    "launcher.bat",
    "launch-studio.sh",
    "VERSION",
    "README.md",
)

EXECUTABLE_FILES = (
    "classiccars-studio",
    "classiccars-studio.exe",
    "launcher.py",
    "launcher.sh",
    "launch-studio.sh",
)

PAYLOAD_DIRS = (
    "web",
    "preview",
    "assets",
    "installers",
)

STATE_USER_FILES = (
    "config.json",
    "config.backup.json",
    "app-config.json",
    "project.json",
    "session.json",
    "theme.json",
    "window.json",
    "draft.json",
)


def detect_source_root(explicit: str | None = None) -> Path:
    if explicit:
        root = Path(explicit).expanduser().resolve()
        if not (root / "web").is_dir() and not (root / "VERSION").is_file():
            raise SystemExit(f"Error: source directory does not look like ClassicCars Studio: {root}")
        return root
    here = Path(__file__).resolve().parent
    if here.name == "installers" and ((here.parent / "web").is_dir() or (here.parent / "VERSION").is_file()):
        return here.parent
    if (here / "web").is_dir() or (here / "VERSION").is_file():
        return here
    raise SystemExit(f"Error: could not locate ClassicCars Studio payload near {here}")


def read_version(source_root: Path) -> str:
    version_file = source_root / "VERSION"
    if version_file.is_file():
        text = version_file.read_text(encoding="utf-8-sig").strip()
        if text:
            return text
    return "0.0.0"


def is_root_user() -> bool:
    return hasattr(os, "geteuid") and os.geteuid() == 0


def default_install_dir() -> Path:
    if os.name == "nt":
        local_app_data = os.environ.get("LOCALAPPDATA")
        base = Path(local_app_data) if local_app_data else (Path.home() / "AppData" / "Local")
        return base / "Programs" / "ClassicCars Studio"
    if is_root_user():
        return Path("/opt/ClassicCars-Studio")
    return Path.home() / ".local" / "opt" / "ClassicCars-Studio"


def has_gui_display() -> bool:
    if os.name == "nt":
        return True
    if sys.platform == "darwin":
        return True
    return bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))


def set_executable(path: Path) -> None:
    if not path.exists():
        return
    try:
        mode = path.stat().st_mode
        path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH | stat.S_IRUSR | stat.S_IWUSR)
    except OSError:
        pass


def desktop_quote(value: str) -> str:
    if any(ch in value for ch in ' \t"'):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return value


def desktop_entry_content(install_dir: Path, icon_path: str) -> str:
    launcher = install_dir / "launcher.sh"
    lines = [
        "[Desktop Entry]",
        "Version=1.0",
        "Type=Application",
        f"Name={APP_NAME}",
        "GenericName=Site preview",
        "Comment=Open ClassicCars Studio in its own window",
        "Exec=" + desktop_quote(str(launcher)),
        "TryExec=" + desktop_quote(str(launcher)),
        "Path=" + desktop_quote(str(install_dir)),
        "Icon=" + desktop_quote(icon_path),
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
    subprocess.run(
        [gio, "set", str(path), "metadata::trusted", "true"],
        capture_output=True,
        text=True,
    )


def install_linux_icons(install_dir: Path) -> str:
    icon256 = install_dir / "assets" / "icon-256.png"
    icon32 = install_dir / "assets" / "favicon-32x32.png"
    if is_root_user():
        hicolor = Path("/usr/local/share/icons/hicolor")
    else:
        hicolor = Path.home() / ".local" / "share" / "icons" / "hicolor"

    installed_main: Path | None = None
    try:
        if icon32.is_file():
            dest32 = hicolor / "32x32" / "apps" / f"{APP_SLUG}.png"
            dest32.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(icon32, dest32)
            dest32.chmod(0o644)
            installed_main = dest32
        if icon256.is_file():
            dest256 = hicolor / "256x256" / "apps" / f"{APP_SLUG}.png"
            dest256.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(icon256, dest256)
            dest256.chmod(0o644)
            installed_main = dest256
    except OSError:
        pass

    if installed_main and installed_main.is_file():
        return str(installed_main.resolve())
    if icon256.is_file():
        return str(icon256.resolve())
    if icon32.is_file():
        return str(icon32.resolve())
    return APP_SLUG


def refresh_linux_desktop_database(apps_dir: Path) -> None:
    updater = shutil.which("update-desktop-database")
    if updater and apps_dir.is_dir():
        subprocess.run([updater, str(apps_dir)], capture_output=True, text=True)
    xdg = shutil.which("xdg-desktop-menu")
    if xdg:
        subprocess.run([xdg, "forceupdate"], capture_output=True, text=True)


def install_linux_shortcuts(
    install_dir: Path,
    create_menu: bool,
    create_desktop: bool,
    create_path_wrapper: bool,
) -> dict[str, str]:
    created: dict[str, str] = {}
    icon_value = install_linux_icons(install_dir)
    content = desktop_entry_content(install_dir, icon_value)

    local_desktop = install_dir / DESKTOP_FILENAME
    local_desktop.write_text(content, encoding="utf-8")
    set_executable(local_desktop)
    mark_desktop_trusted(local_desktop)

    if create_menu:
        if is_root_user():
            apps_dir = Path("/usr/local/share/applications")
        else:
            apps_dir = Path.home() / ".local" / "share" / "applications"
        apps_dir.mkdir(parents=True, exist_ok=True)
        menu_file = apps_dir / f"{APP_SLUG}.desktop"
        menu_file.write_text(content, encoding="utf-8")
        set_executable(menu_file)
        mark_desktop_trusted(menu_file)
        refresh_linux_desktop_database(apps_dir)
        created["menu_shortcut"] = str(menu_file)
        print(f"Created Application Menu entry: {menu_file}")

    if create_desktop:
        desktop_dir = Path.home() / "Desktop"
        if desktop_dir.is_dir():
            desktop_shortcut = desktop_dir / DESKTOP_FILENAME
            desktop_shortcut.write_text(content, encoding="utf-8")
            set_executable(desktop_shortcut)
            mark_desktop_trusted(desktop_shortcut)
            created["desktop_shortcut"] = str(desktop_shortcut)
            print(f"Created Desktop shortcut: {desktop_shortcut}")

    if create_path_wrapper:
        bin_dir = Path("/usr/local/bin") if is_root_user() else (Path.home() / ".local" / "bin")
        bin_dir.mkdir(parents=True, exist_ok=True)
        wrapper = bin_dir / APP_SLUG
        launcher_sh = install_dir / "launcher.sh"
        wrapper_text = (
            "#!/bin/sh\n"
            "# ClassicCars Studio CLI wrapper generated by installers/install.py\n"
            f'exec "{launcher_sh}" "$@"\n'
        )
        wrapper.write_text(wrapper_text, encoding="utf-8")
        set_executable(wrapper)
        created["cli_wrapper"] = str(wrapper)
        print(f"Created CLI command wrapper: {wrapper}")

    return created


def create_windows_lnk(lnk_path: Path, target_path: Path, working_dir: Path, icon_path: Path) -> bool:
    ps = shutil.which("powershell") or shutil.which("pwsh")
    if not ps:
        return False
    lnk_path.parent.mkdir(parents=True, exist_ok=True)
    script = (
        "$ws = New-Object -ComObject WScript.Shell; "
        "$s = $ws.CreateShortcut($env:CCS_LNK); "
        "$s.TargetPath = $env:CCS_TARGET; "
        "$s.WorkingDirectory = $env:CCS_WORKDIR; "
        "$s.Description = 'Open ClassicCars Studio in its own window'; "
        "if (Test-Path $env:CCS_ICON) { $s.IconLocation = \"$($env:CCS_ICON),0\" }; "
        "$s.Save()"
    )
    env = os.environ.copy()
    env["CCS_LNK"] = str(lnk_path)
    env["CCS_TARGET"] = str(target_path)
    env["CCS_WORKDIR"] = str(working_dir)
    env["CCS_ICON"] = str(icon_path)
    completed = subprocess.run(
        [ps, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script],
        env=env,
        capture_output=True,
        text=True,
    )
    return completed.returncode == 0 and lnk_path.is_file()


def install_windows_shortcuts(
    install_dir: Path,
    create_menu: bool,
    create_desktop: bool,
    create_path_wrapper: bool,
) -> dict[str, str]:
    created: dict[str, str] = {}
    exe = install_dir / "classiccars-studio.exe"
    bat = install_dir / "launcher.bat"
    target = exe if exe.is_file() else bat

    if create_menu:
        appdata = os.environ.get("APPDATA")
        start_menu = (
            Path(appdata) / "Microsoft" / "Windows" / "Start Menu" / "Programs"
            if appdata
            else Path.home() / "AppData" / "Roaming" / "Microsoft" / "Windows" / "Start Menu" / "Programs"
        )
        menu_lnk = start_menu / f"{APP_NAME}.lnk"
        if create_windows_lnk(menu_lnk, target, install_dir, exe):
            created["menu_shortcut"] = str(menu_lnk)
            print(f"Created Start Menu shortcut: {menu_lnk}")

    if create_desktop:
        userprofile = os.environ.get("USERPROFILE")
        desktop_dir = (Path(userprofile) if userprofile else Path.home()) / "Desktop"
        desktop_lnk = desktop_dir / f"{APP_NAME}.lnk"
        if create_windows_lnk(desktop_lnk, target, install_dir, exe):
            created["desktop_shortcut"] = str(desktop_lnk)
            print(f"Created Desktop shortcut: {desktop_lnk}")

    if create_path_wrapper:
        bin_cmd = install_dir / "classiccars-studio.cmd"
        bin_cmd.write_text(
            f'@echo off\r\n"{target}" %*\r\n',
            encoding="utf-8",
        )
        created["cli_wrapper"] = str(bin_cmd)

    return created


def write_uninstall_scripts(install_dir: Path) -> None:
    uninstall_sh = install_dir / "uninstall.sh"
    sh_content = """#!/bin/sh
# Uninstall ClassicCars Studio from this directory.
# Pass --purge-state to also delete saved user settings in state/.

case "$0" in
  */*) SCRIPT_DIR=${0%/*} ;;
  *) SCRIPT_DIR=. ;;
esac
SCRIPT_DIR=$(cd "$SCRIPT_DIR" && pwd)

for py in python3 python; do
  if command -v "$py" >/dev/null 2>&1; then
    exec "$py" "$SCRIPT_DIR/installers/install.py" --uninstall --dir "$SCRIPT_DIR" "$@"
  fi
done

echo "Python 3 not found; removing application files directly from $SCRIPT_DIR"
rm -f "$SCRIPT_DIR/classiccars-studio" "$SCRIPT_DIR/classiccars-studio.exe" \\
      "$SCRIPT_DIR/launcher.py" "$SCRIPT_DIR/launcher.sh" "$SCRIPT_DIR/launcher.bat" \\
      "$SCRIPT_DIR/launch-studio.sh" "$SCRIPT_DIR/VERSION" "$SCRIPT_DIR/README.md" \\
      "$SCRIPT_DIR/ClassicCars-Studio.desktop" "$SCRIPT_DIR/.install-manifest.json" \\
      "$SCRIPT_DIR/uninstall.bat"
rm -rf "$SCRIPT_DIR/web" "$SCRIPT_DIR/preview" "$SCRIPT_DIR/assets" "$SCRIPT_DIR/installers"
rm -f "$HOME/.local/share/applications/classiccars-studio.desktop" \\
      "$HOME/Desktop/ClassicCars-Studio.desktop" \\
      "$HOME/.local/bin/classiccars-studio"
rm -f "$SCRIPT_DIR/uninstall.sh"
echo "Uninstalled ClassicCars Studio from $SCRIPT_DIR"
"""
    uninstall_sh.write_text(sh_content, encoding="utf-8")
    set_executable(uninstall_sh)

    uninstall_bat = install_dir / "uninstall.bat"
    bat_content = (
        "@echo off\r\n"
        "setlocal EnableExtensions\r\n"
        "set \"INSTALL_DIR=%~dp0\"\r\n"
        "if \"%INSTALL_DIR:~-1%\"==\"\\\" set \"INSTALL_DIR=%INSTALL_DIR:~0,-1%\"\r\n"
        "if exist \"%INSTALL_DIR%\\installers\\install-windows.ps1\" (\r\n"
        "  powershell -NoProfile -ExecutionPolicy Bypass -File \"%INSTALL_DIR%\\installers\\install-windows.ps1\" -Uninstall -InstallDir \"%INSTALL_DIR%\" %*\r\n"
        "  exit /b %ERRORLEVEL%\r\n"
        ")\r\n"
        "python \"%INSTALL_DIR%\\installers\\install.py\" --uninstall --dir \"%INSTALL_DIR%\" %*\r\n"
    )
    uninstall_bat.write_text(bat_content, encoding="utf-8")


def ensure_state_directory(install_dir: Path) -> list[str]:
    state_dir = install_dir / "state"
    preserved: list[str] = []
    if state_dir.is_dir():
        for item in sorted(state_dir.iterdir()):
            if item.is_file():
                preserved.append(item.name)
    else:
        state_dir.mkdir(parents=True, exist_ok=True)

    tmp_traffic = state_dir / "tmp-traffic"
    tmp_traffic.mkdir(parents=True, exist_ok=True)

    if os.name != "nt":
        try:
            state_dir.chmod(0o700)
            tmp_traffic.chmod(0o700)
        except OSError:
            pass
    return preserved


def copy_payload(source_root: Path, install_dir: Path) -> list[str]:
    copied: list[str] = []
    same_root = source_root.resolve() == install_dir.resolve()
    install_dir.mkdir(parents=True, exist_ok=True)

    if not same_root:
        for filename in PAYLOAD_FILES:
            src_file = source_root / filename
            if not src_file.is_file():
                continue
            dest_file = install_dir / filename
            if dest_file.exists() or dest_file.is_symlink():
                dest_file.unlink()
            shutil.copy2(src_file, dest_file)
            if filename in EXECUTABLE_FILES:
                set_executable(dest_file)
            copied.append(filename)

        for dirname in PAYLOAD_DIRS:
            src_dir = source_root / dirname
            if not src_dir.is_dir():
                continue
            dest_dir = install_dir / dirname
            if dest_dir.exists():
                shutil.rmtree(dest_dir)
            shutil.copytree(
                src_dir,
                dest_dir,
                ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo", "*.tmp"),
            )
            if dirname == "installers":
                for script in dest_dir.iterdir():
                    if script.suffix in (".py", ".sh"):
                        set_executable(script)
            copied.append(f"{dirname}/")
    else:
        for filename in PAYLOAD_FILES:
            if (install_dir / filename).is_file():
                copied.append(filename)
        for dirname in PAYLOAD_DIRS:
            if (install_dir / dirname).is_dir():
                copied.append(f"{dirname}/")

    return copied


def perform_install(
    source_root: Path,
    install_dir: Path,
    create_menu: bool = True,
    create_desktop: bool = True,
    create_path_wrapper: bool = True,
) -> int:
    install_dir = install_dir.expanduser().resolve()
    version = read_version(source_root)
    print(f"Installing {APP_NAME} v{version} -> {install_dir}")

    preserved_state = ensure_state_directory(install_dir)
    copied = copy_payload(source_root, install_dir)
    # Re-verify state permissions after copy
    ensure_state_directory(install_dir)
    write_uninstall_scripts(install_dir)

    if os.name == "nt":
        shortcuts = install_windows_shortcuts(
            install_dir,
            create_menu=create_menu,
            create_desktop=create_desktop,
            create_path_wrapper=create_path_wrapper,
        )
    else:
        shortcuts = install_linux_shortcuts(
            install_dir,
            create_menu=create_menu,
            create_desktop=create_desktop,
            create_path_wrapper=create_path_wrapper,
        )

    manifest = {
        "app": APP_NAME,
        "version": version,
        "install_dir": str(install_dir),
        "installed_at": datetime.now(timezone.utc).isoformat(),
        "copied": copied,
        "shortcuts": shortcuts,
    }
    manifest_path = install_dir / MANIFEST_FILENAME
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    if preserved_state:
        print(f"Preserved existing user state files in {install_dir / 'state'}: {', '.join(preserved_state)}")
    else:
        print(f"Initialized private state directory: {install_dir / 'state'} (mode 0700)")

    launcher_hint = (
        str(install_dir / "classiccars-studio.exe")
        if os.name == "nt"
        else str(install_dir / "launcher.sh")
    )
    print(f"\nInstallation complete!")
    print(f"  Location:    {install_dir}")
    print(f"  Launch with: {launcher_hint}")
    print(f"  Uninstall:   {install_dir / ('uninstall.bat' if os.name == 'nt' else 'uninstall.sh')}")
    return 0


def shortcut_references_dir(shortcut_path: Path, install_dir: Path) -> bool:
    if not shortcut_path.is_file():
        return False
    if shortcut_path.suffix in (".desktop", ".sh", ".cmd", ""):
        try:
            text = shortcut_path.read_text(encoding="utf-8", errors="ignore")
            return str(install_dir) in text
        except OSError:
            return False
    return True


def perform_uninstall(install_dir: Path, purge_state: bool = False) -> int:
    install_dir = install_dir.expanduser().resolve()
    if not install_dir.exists():
        print(f"Nothing to uninstall: {install_dir} does not exist.")
        return 0

    print(f"Uninstalling {APP_NAME} from {install_dir}...")
    manifest_path = install_dir / MANIFEST_FILENAME
    recorded_shortcuts: dict[str, str] = {}
    if manifest_path.is_file():
        try:
            data = json.loads(manifest_path.read_text(encoding="utf-8"))
            if isinstance(data.get("shortcuts"), dict):
                recorded_shortcuts = {str(k): str(v) for k, v in data["shortcuts"].items()}
        except (OSError, ValueError):
            pass

    candidate_shortcuts: list[Path] = [Path(p) for p in recorded_shortcuts.values() if p]
    if os.name == "nt":
        appdata = os.environ.get("APPDATA")
        if appdata:
            candidate_shortcuts.append(
                Path(appdata) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / f"{APP_NAME}.lnk"
            )
        userprofile = os.environ.get("USERPROFILE")
        if userprofile:
            candidate_shortcuts.append(Path(userprofile) / "Desktop" / f"{APP_NAME}.lnk")
    else:
        candidate_shortcuts.extend([
            Path.home() / ".local" / "share" / "applications" / f"{APP_SLUG}.desktop",
            Path.home() / "Desktop" / DESKTOP_FILENAME,
            Path.home() / ".local" / "bin" / APP_SLUG,
        ])

    seen: set[Path] = set()
    for shortcut in candidate_shortcuts:
        if shortcut in seen or not shortcut.is_file():
            continue
        seen.add(shortcut)
        if str(shortcut) in recorded_shortcuts.values() or shortcut_references_dir(shortcut, install_dir):
            try:
                shortcut.unlink()
                print(f"Removed shortcut: {shortcut}")
            except OSError as exc:
                print(f"Warning: could not remove {shortcut}: {exc}", file=sys.stderr)

    if os.name != "nt":
        apps_dir = Path.home() / ".local" / "share" / "applications"
        refresh_linux_desktop_database(apps_dir)

    files_to_remove = list(PAYLOAD_FILES) + [
        DESKTOP_FILENAME,
        MANIFEST_FILENAME,
        "uninstall.sh",
        "uninstall.bat",
        "classiccars-studio.cmd",
    ]
    for filename in files_to_remove:
        target = install_dir / filename
        if target.is_file() or target.is_symlink():
            try:
                target.unlink()
            except OSError as exc:
                print(f"Warning: could not remove {target}: {exc}", file=sys.stderr)

    for dirname in PAYLOAD_DIRS:
        target_dir = install_dir / dirname
        if target_dir.is_dir():
            try:
                shutil.rmtree(target_dir)
            except OSError as exc:
                print(f"Warning: could not remove {target_dir}: {exc}", file=sys.stderr)

    state_dir = install_dir / "state"
    if purge_state and state_dir.exists():
        shutil.rmtree(state_dir, ignore_errors=True)
        print(f"Removed user state directory: {state_dir}")
    elif state_dir.is_dir():
        user_files = [
            f.name for f in state_dir.iterdir()
            if f.is_file() and (f.name in STATE_USER_FILES or f.suffix == ".json")
        ]
        if user_files:
            print(
                f"Preserved user settings in {state_dir} ({', '.join(sorted(user_files))}). "
                "Pass --purge-state to remove them."
            )
        else:
            shutil.rmtree(state_dir, ignore_errors=True)

    try:
        if install_dir.is_dir() and not any(install_dir.iterdir()):
            install_dir.rmdir()
            print(f"Removed empty installation directory: {install_dir}")
    except OSError:
        pass

    print(f"{APP_NAME} has been uninstalled.")
    return 0


def prompt_gui(
    version: str,
    initial_dir: Path,
    default_menu: bool,
    default_desktop: bool,
    default_path: bool,
) -> tuple[Path, bool, bool, bool] | None:
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox
    except ImportError:
        return None

    try:
        root = tk.Tk()
    except Exception:
        return None

    bg = "#181a1f"
    panel_bg = "#21252b"
    fg = "#e6edf3"
    muted = "#9da5b4"
    accent = "#3b82f6"

    root.title(f"Install {APP_NAME} v{version}")
    root.configure(bg=bg)
    root.geometry("560x340")
    root.resizable(False, False)

    result: dict[str, object] = {"confirmed": False}

    header = tk.Frame(root, bg=bg, padx=24, pady=18)
    header.pack(fill="x")
    tk.Label(
        header,
        text=f"{APP_NAME} v{version}",
        font=("Sans", 15, "bold"),
        bg=bg,
        fg=fg,
        anchor="w",
    ).pack(fill="x")
    tk.Label(
        header,
        text="Choose where to install ClassicCars Studio on your machine.",
        font=("Sans", 10),
        bg=bg,
        fg=muted,
        anchor="w",
        pady=4,
    ).pack(fill="x")

    body = tk.Frame(root, bg=bg, padx=24, pady=4)
    body.pack(fill="both", expand=True)

    tk.Label(
        body,
        text="Installation Directory:",
        font=("Sans", 10, "bold"),
        bg=bg,
        fg=fg,
        anchor="w",
    ).pack(fill="x", pady=(0, 6))

    dir_row = tk.Frame(body, bg=bg)
    dir_row.pack(fill="x", pady=(0, 14))

    dir_var = tk.StringVar(value=str(initial_dir))
    dir_entry = tk.Entry(
        dir_row,
        textvariable=dir_var,
        font=("Sans", 10),
        bg=panel_bg,
        fg=fg,
        insertbackground=fg,
        relief="flat",
        bd=6,
    )
    dir_entry.pack(side="left", fill="x", expand=True, padx=(0, 8))

    def on_browse() -> None:
        current = dir_var.get().strip() or str(initial_dir)
        start_parent = Path(current).expanduser()
        while not start_parent.is_dir() and start_parent.parent != start_parent:
            start_parent = start_parent.parent
        chosen = filedialog.askdirectory(
            parent=root,
            title=f"Select Installation Folder for {APP_NAME}",
            initialdir=str(start_parent),
        )
        if chosen:
            chosen_path = Path(chosen)
            if chosen_path.name.lower() not in ("classiccars-studio", "classiccars studio"):
                chosen_path = chosen_path / ("ClassicCars Studio" if os.name == "nt" else "ClassicCars-Studio")
            dir_var.set(str(chosen_path))

    tk.Button(
        dir_row,
        text="Browse…",
        command=on_browse,
        bg=panel_bg,
        fg=fg,
        activebackground="#2c313a",
        activeforeground=fg,
        relief="flat",
        padx=12,
        pady=4,
    ).pack(side="right")

    menu_var = tk.BooleanVar(value=default_menu)
    desktop_var = tk.BooleanVar(value=default_desktop)
    path_var = tk.BooleanVar(value=default_path)

    menu_label = (
        "Create Start Menu shortcut"
        if os.name == "nt"
        else "Create Application Menu shortcut (~/.local/share/applications)"
    )
    path_label = (
        "Create CLI launcher script (classiccars-studio.cmd)"
        if os.name == "nt"
        else "Add 'classiccars-studio' command to ~/.local/bin"
    )

    for var, text in (
        (menu_var, menu_label),
        (desktop_var, "Create Desktop shortcut"),
        (path_var, path_label),
    ):
        tk.Checkbutton(
            body,
            text=text,
            variable=var,
            bg=bg,
            fg=fg,
            selectcolor=panel_bg,
            activebackground=bg,
            activeforeground=fg,
            highlightthickness=0,
            anchor="w",
            font=("Sans", 10),
        ).pack(fill="x", pady=2)

    footer = tk.Frame(root, bg=bg, padx=24, pady=16)
    footer.pack(fill="x", side="bottom")

    def on_install() -> None:
        raw = dir_var.get().strip()
        if not raw:
            messagebox.showerror("Invalid Directory", "Please choose an installation directory.", parent=root)
            return
        result["confirmed"] = True
        result["dir"] = Path(raw).expanduser()
        result["menu"] = bool(menu_var.get())
        result["desktop"] = bool(desktop_var.get())
        result["path"] = bool(path_var.get())
        root.destroy()

    def on_cancel() -> None:
        root.destroy()

    tk.Button(
        footer,
        text="Install",
        command=on_install,
        bg=accent,
        fg="#ffffff",
        activebackground="#2563eb",
        activeforeground="#ffffff",
        relief="flat",
        font=("Sans", 10, "bold"),
        padx=18,
        pady=6,
    ).pack(side="right", padx=(8, 0))

    tk.Button(
        footer,
        text="Cancel",
        command=on_cancel,
        bg=panel_bg,
        fg=fg,
        activebackground="#2c313a",
        activeforeground=fg,
        relief="flat",
        font=("Sans", 10),
        padx=14,
        pady=6,
    ).pack(side="right")

    root.protocol("WM_DELETE_WINDOW", on_cancel)
    root.mainloop()

    if not result.get("confirmed"):
        raise SystemExit("Installation cancelled by user.")

    return (
        result["dir"],  # type: ignore[return-value]
        bool(result["menu"]),
        bool(result["desktop"]),
        bool(result["path"]),
    )


def ask_yes_no(prompt: str, default: bool = True) -> bool:
    suffix = "[Y/n]" if default else "[y/N]"
    try:
        answer = input(f"{prompt} {suffix}: ").strip().lower()
    except EOFError:
        return default
    if not answer:
        return default
    return answer in ("y", "yes", "1", "true")


def prompt_cli(
    version: str,
    initial_dir: Path,
    default_menu: bool,
    default_desktop: bool,
    default_path: bool,
) -> tuple[Path, bool, bool, bool]:
    print(f"=== {APP_NAME} v{version} Installer ===")
    try:
        raw_dir = input(f"Installation directory [{initial_dir}]: ").strip()
    except EOFError:
        raw_dir = ""
    chosen_dir = Path(raw_dir).expanduser() if raw_dir else initial_dir
    create_menu = ask_yes_no("Create Start Menu / Application Menu entry?", default=default_menu)
    create_desktop = ask_yes_no("Create Desktop shortcut?", default=default_desktop)
    create_path = ask_yes_no("Create CLI command wrapper in ~/.local/bin?", default=default_path)
    return chosen_dir, create_menu, create_desktop, create_path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=f"Install or uninstall {APP_NAME} to a user-chosen location.",
    )
    parser.add_argument(
        "--dir",
        "--prefix",
        "-d",
        dest="install_dir",
        help="Target installation directory (default: ~/.local/opt/ClassicCars-Studio on Linux, "
        "%%LOCALAPPDATA%%\\Programs\\ClassicCars Studio on Windows).",
    )
    parser.add_argument(
        "--source",
        help="Path to the ClassicCars Studio source/bundle directory.",
    )
    parser.add_argument(
        "--yes",
        "-y",
        "--quiet",
        "-q",
        "--non-interactive",
        dest="yes",
        action="store_true",
        help="Run non-interactively without GUI or CLI prompts.",
    )
    parser.add_argument(
        "--gui",
        action="store_true",
        help="Force the interactive GUI directory picker if a display is available.",
    )
    parser.add_argument(
        "--cli",
        action="store_true",
        help="Use interactive terminal prompts instead of the GUI dialog.",
    )
    parser.add_argument(
        "--menu",
        dest="menu",
        action="store_true",
        default=True,
        help="Create Start Menu / Application Menu entry (default).",
    )
    parser.add_argument(
        "--no-menu",
        dest="menu",
        action="store_false",
        help="Do not create Start Menu / Application Menu entry.",
    )
    parser.add_argument(
        "--desktop",
        dest="desktop",
        action="store_true",
        default=True,
        help="Create Desktop shortcut (default).",
    )
    parser.add_argument(
        "--no-desktop",
        dest="desktop",
        action="store_false",
        help="Do not create Desktop shortcut.",
    )
    parser.add_argument(
        "--path",
        dest="path_wrapper",
        action="store_true",
        default=True,
        help="Add classiccars-studio launcher to ~/.local/bin (default).",
    )
    parser.add_argument(
        "--no-path",
        dest="path_wrapper",
        action="store_false",
        help="Do not create ~/.local/bin/classiccars-studio wrapper.",
    )
    parser.add_argument(
        "--uninstall",
        action="store_true",
        help="Uninstall ClassicCars Studio from the target directory.",
    )
    parser.add_argument(
        "--purge-state",
        action="store_true",
        help="When uninstalling, also delete saved user state in <dir>/state/.",
    )
    parser.add_argument(
        "--keep-state",
        dest="purge_state",
        action="store_false",
        help="When uninstalling, preserve user settings in <dir>/state/ (default).",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    initial_dir = Path(args.install_dir).expanduser() if args.install_dir else default_install_dir()

    if args.uninstall:
        return perform_uninstall(initial_dir, purge_state=args.purge_state)

    source_root = detect_source_root(args.source)
    version = read_version(source_root)

    create_menu = bool(args.menu)
    create_desktop = bool(args.desktop)
    create_path = bool(args.path_wrapper)
    chosen_dir = initial_dir

    interactive_needed = args.gui or (not args.install_dir and not args.yes)
    if interactive_needed:
        gui_selection = None
        if not args.cli and has_gui_display():
            gui_selection = prompt_gui(
                version=version,
                initial_dir=initial_dir,
                default_menu=create_menu,
                default_desktop=create_desktop,
                default_path=create_path,
            )
        if gui_selection is not None:
            chosen_dir, create_menu, create_desktop, create_path = gui_selection
        elif sys.stdin.isatty():
            chosen_dir, create_menu, create_desktop, create_path = prompt_cli(
                version=version,
                initial_dir=initial_dir,
                default_menu=create_menu,
                default_desktop=create_desktop,
                default_path=create_path,
            )

    return perform_install(
        source_root=source_root,
        install_dir=chosen_dir,
        create_menu=create_menu,
        create_desktop=create_desktop,
        create_path_wrapper=create_path,
    )


if __name__ == "__main__":
    sys.exit(main())
