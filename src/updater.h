#ifndef STUDIO_UPDATER_H
#define STUDIO_UPDATER_H

#include "json.h"

#include <stddef.h>

/* Compare two semantic version strings ("v0.0.34" vs "0.0.35").
 * Returns <0 if v1 < v2, 0 if equal, >0 if v1 > v2. */
int updater_compare_versions(const char *v1, const char *v2);

/* Compute lowercase 64-char hex SHA-256 of a local file.
 * Returns 0 on success, -1 on I/O error. */
int updater_sha256_file(const char *path, char hex_out[65]);

/* Build JSON document describing installed version, dismissed version,
 * pending staged update in <studio_home>/state/pending-update/ready.json,
 * and cached latest release metadata.
 * Returns 0 on success. */
int updater_status(const char *studio_home, JsonDoc **doc_out, char *err, size_t err_sz);

/* Query GitHub Releases (or STUDIO_UPDATE_MANIFEST_URL) for the latest release,
 * update cached release metadata, and return the status JSON document.
 * Returns 0 on success, 1 on bad request, -1 on network/parse error. */
int updater_check(const char *studio_home, const JsonValue *body,
                  JsonDoc **doc_out, char *err, size_t err_sz);

/* Record or clear dismissedReleaseVersion in <studio_home>/state/app-config.json
 * and return the updated status JSON document.
 * Returns 0 on success, 1 on validation error, -1 on I/O error. */
int updater_dismiss(const char *studio_home, const JsonValue *body,
                    JsonDoc **doc_out, char *err, size_t err_sz);

/* Download the release asset (and SHA256SUMS when attached) into
 * <studio_home>/state/pending-update/, verify SHA-256 checksum, extract/stage
 * whitelisted Studio files into <studio_home>/state/pending-update/staged/,
 * write <studio_home>/state/pending-update/ready.json, and return the updated
 * status JSON document.
 * Returns 0 on success, 1 on validation/state error, -1 on download/staging error. */
int updater_download(const char *studio_home, const JsonValue *body,
                     JsonDoc **doc_out, char *err, size_t err_sz);

/* Apply a previously staged update from <studio_home>/state/pending-update/staged/
 * into <studio_home> on startup without touching <studio_home>/state/.
 * Returns 1 when a staged update was applied, 0 when no staged update is waiting,
 * or -1 on error. */
int updater_apply_pending_on_boot(const char *studio_home, char *err, size_t err_sz);

/* Reset in-memory cached release state (used by self-check tests). */
void updater_reset_cache(void);

#endif
