#ifndef STUDIO_WINDOW_H
#define STUDIO_WINDOW_H

/* Runs the native standalone desktop window pointed at `url`.
   Blocks until the user closes the window. `repo_root` selects
   studio/state/window.json for restoring and saving position, size, and
   maximized state; it may be NULL or empty to skip persistence.
   Returns 0 on normal window close, or -1 if no graphical display / windowing
   runtime is available. */
int studio_window_run(const char *url, const char *initial_title, const char *icon_path,
                      const char *repo_root);

/* Ask the native window to close. Safe to call from the HTTP thread.
   Headless runs have no window; the server is stopped either way. */
void studio_window_request_close(void);

#endif
