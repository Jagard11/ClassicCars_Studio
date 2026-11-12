#ifndef STUDIO_CODE_ENGINE_H
#define STUDIO_CODE_ENGINE_H

#include "json.h"
#include <stddef.h>

#define CODE_DIFF_NONE     0
#define CODE_DIFF_ADDED    1
#define CODE_DIFF_MODIFIED 2

/* Return canonical syntax language ("php", "html", "css", "js", "json", "text") from path. */
const char *code_engine_language_from_path(const char *path);

/* Highlight source text into <span class="tok-..."> HTML markup (1:1 with syntax-highlight.js).
 * Caller frees the returned malloc'd UTF-8 string. */
char *code_engine_highlight_syntax(const char *source, const char *language);

/* Highlight full document (appending trailing space after final '\n' for textarea parity).
 * Caller frees the returned malloc'd UTF-8 string. */
char *code_engine_highlight_document(const char *source, const char *language);

/* Compute line-by-line diff against base_text (1:1 with line-diff.js computeLineDiff).
 * When base_text is NULL, returns an unchanged diff result.
 * Returns a JSON_OBJECT with hasChanges, addedCount, modifiedCount, deletedCount,
 * lineKinds, deletedAfter, deletions, and hunks. */
JsonValue *code_engine_compute_line_diff(const char *base_text, const char *current_text);

/* Scan source lines into minimap structural blocks and token micro-bars (1:1 with code-minimap.js).
 * Returns a JSON_ARRAY of line objects. */
JsonValue *code_engine_scan_minimap_lines(const char *source, const char *language);

/* Detect comment context ("html", "php", "css", "js", "json") at char_offset in source. */
const char *code_engine_detect_comment_context(const char *source, size_t char_offset,
                                               const char *file_path);

/* Compute context-aware Ctrl+/ comment toggle edit (1:1 with code-comments.js computeCommentToggle).
 * Returns a JSON_OBJECT with from, to, insert, selectionStart, selectionEnd, context. */
JsonValue *code_engine_compute_comment_toggle(const char *source, size_t selection_start,
                                              size_t selection_end, const char *file_path);

/* Evaluate project macros against a page document and source (1:1 with macro-engine.js evaluateAllMacros).
 * Returns a JSON_OBJECT with evaluations (array) and issues (array). */
JsonValue *code_engine_evaluate_macros(const JsonValue *macros_array, const char *doc_path,
                                       const char *doc_section, int is_media,
                                       const char *source);

/* Apply, insert, repair, or remove a macro in source text (1:1 with macro-engine.js applyMacroToSource).
 * Returns a JSON_OBJECT with content, range ({startOffset, endOffset} or null), and label. */
JsonValue *code_engine_apply_macro(const JsonValue *macro_def, const char *source,
                                   const JsonValue *input_values, const char *mode,
                                   int cursor_offset);

#endif
