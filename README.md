# ClassicCars Studio

Local preview server for this repository. It serves the static site, runs Apache-style server-side includes, and proxies `.php` files to a supervised `php -S` child so sales pages and embedded scripts behave like production.

Open the editor at [http://127.0.0.1:8770/__studio/](http://127.0.0.1:8770/__studio/).

`GET /` redirects there. `GET /?preview=1` redirects to `/sales/index.php`, matching the public landing page.

## Build

From `studio/`:

```sh
make
make test
./build/classiccars-studio
```

CMake (from `studio/`):

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/classiccars-studio --check
```

Use either Make or CMake in `studio/build/`, not both at once. The full link also needs `src/json.c`, `src/gallery.c`, and `src/api.c`.

`make test` / `classiccars-studio --check` runs the built-in self-check and exits 0 on success.

## Launch

Linux: double-click `studio/ClassicCars-Studio.desktop`, or run `./studio/launcher.sh`. If `studio/classiccars-studio` is missing, the launcher runs `./studio/build.sh` first. Studio opens in its own window at [http://127.0.0.1:8770/__studio/](http://127.0.0.1:8770/__studio/). Closing the window stops the server. `Terminal=false`, so the desktop entry does not leave a terminal behind the window. `studio/launch-studio.sh` starts `studio/launcher.sh`, so an older shortcut still uses that entry.

`./studio/build.sh` and `studio\build.bat` increment the patch number in `studio/VERSION` (`0.0.1` becomes `0.0.2`) and write `studio/src/version.h` before compiling. `--rebuild` deletes `studio/build/` and the binaries first, then increments and rebuilds. `--no-bump` compiles the version already in `studio/VERSION`. Edit `studio/VERSION` by hand to change `MAJOR` or `MINOR`.

`make desktop` rewrites `ClassicCars-Studio.desktop` so `Exec` is this checkout's `studio/launcher.sh` (a copy on the Desktop or in `~/.local/share/applications/` still starts the right tree). `make desktop-install` copies that file into `~/.local/share/applications/`.

Windows: double-click `studio/classiccars-studio.exe`, or run `studio\launcher.bat`. The exe is a GUI program: it opens its own window and does not open a browser tab. `studio\build.bat` increments the patch version and builds `classiccars-studio.exe`. From Linux, `./studio/build.sh` also updates that exe when `x86_64-w64-mingw32-gcc` or `zig` is installed (`make windows`). The exe is linked `-static` so it does not need MinGW runtime DLLs. The page is drawn with the Edge WebView2 runtime that ships with Windows 10 and 11; if that runtime is missing, the window stays open and explains how to install it.

If something is already listening on the studio port, Studio opens another window onto that server and exits when the window closes, instead of failing with "Address already in use". `--headless` prints the address and exits 0.

## Run

```sh
./build/classiccars-studio --root /path/to/ClassicCars
```

| Flag | Default | Meaning |
| --- | --- | --- |
| `--port` | `8770` | Studio listen port (bound to `127.0.0.1`) |
| `--php-port` | `8771` | Port for the `php -S` child |
| `--root` | auto | Repository root. Detected by walking up from the executable, then from the current directory. |
| `--no-php` | off | Serve static files and the API without spawning PHP |
| `--headless` | off | Serve in this terminal and do not open a window. `--no-window`, `--server-only`, and `--no-open` are the same flag |
| `--open` | ignored | Accepted so older launchers still start. The window opens unless a headless flag is set |
| `--check` | off | Run self-checks and exit |
| `--version` | off | Print the version and exit |

The process runs:

```text
php -d auto_prepend_file=<root>/studio/preview/prepend.php -S 127.0.0.1:<php-port> -t <root>
```

`studio/preview/prepend.php` points traffic logging at `studio/state/tmp-traffic` and, when a page include sends `X-Preview-Document-Uri`, copies that value into `DOCUMENT_URI`, `REDIRECT_URL`, and `REQUEST_URI`. That lets `php/similar_vehicles.php?embed=1` read the parent HTML page's `<!-- related-sales -->` comment.

Startup waits up to 3 seconds for PHP to accept connections. `SIGINT`, `SIGTERM`, and `SIGHUP` stop the child. If PHP exits while the studio is running, it is restarted. With a window open, the HTTP accept loop runs on a background thread and the native window runs on the main thread. Closing the window stops both. `--headless` keeps the accept loop on the main thread so `curl` and `--check` do not need a display.

## Routing

- `GET /` → `302 /__studio/` unless `?preview=1`, which → `302 /sales/index.php`.
- `/__studio` → `302 /__studio/`. `/__studio/*` (except `/__studio/api/*`) is served from `studio/web/`.
- `/__studio/api/*` and `/api/*` go to `api_handle_request()`.
- `.htaccess`, `/data/`, `/admin/`, `/studio/state/`, `/studio/src/`, `/studio/build/`, and any path containing `..` or `\` return `403`.
- `/sales/<slug>` redirects `301` to `/sales/<slug>/index.php` when that file exists. `/sales/<slug>/` is executed as that `index.php`.
- `/articles/<slug>/`, `/awards/<slug>/`, and `/projects/<slug>/` resolve `index.php`, `index.html`, `index.shtml`, then `index.htm`.
- Images, CSS, JS, fonts, and the other static types are served from the repository with `Cache-Control: no-cache`.
- `.html`, `.htm`, and `.shtml` are passed through the SSI splicer.
- `.php` is proxied to `php -S`. HTML responses are SSI-spliced afterward, so `includes/header.html`'s `<!--#include virtual="/php/logview.php" -->` still runs.

SSI resolves `<!--#include virtual="..." -->` up to depth 8. A `.php` target is fetched from the PHP child with `X-Preview-Document-Uri` set to the top-level page. A static HTML target is read from disk and spliced recursively.

`studio/state/` (theme and temporary traffic) and `studio/build/` stay untracked. `studio/*` is listed in `.git-ftp-ignore`, so the studio is not uploaded to the live site.
