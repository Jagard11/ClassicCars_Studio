#ifndef STUDIO_GALLERY_H
#define STUDIO_GALLERY_H

#include "json.h"

#include <stddef.h>

/* Gallery engine for deep-data.json, image_data.txt, sales PHP headers,
 * and article page-values. Independent of the HTTP server. */

typedef struct GallerySalesHeader {
    char *vehicle_type;
    char *make_type;
    int do_not_render;
    char *vehicle_name;
    char *inventory_number;
    char *price;
    char *description;
} GallerySalesHeader;

int gallery_section_ok(const char *section);
int gallery_valid_slug(const char *slug);

char *gallery_caption_from_html(const char *html, size_t len);
char *gallery_image_data_from_doc(const JsonDoc *doc, size_t *out_len);
int gallery_image_size(const char *path, int *width, int *height);

/* 0 success, 1 validation, 2 not found, -1 IO/internal. */
int gallery_apply_image_list(JsonDoc *doc, const JsonValue *images, const char *folder_fs,
                             char *err, size_t err_sz);

int gallery_read_sales_header(const char *index_php_path, GallerySalesHeader *out,
                              char *err, size_t err_sz);
void gallery_sales_header_free(GallerySalesHeader *header);
char *gallery_apply_sales_header(const char *php_text, size_t len, const GallerySalesHeader *header,
                                 size_t *out_len, char *err, size_t err_sz);

JsonDoc *gallery_folders_document(const char *repo_root, const char *section,
                                  char *err, size_t err_sz);
JsonDoc *gallery_state_document(const char *repo_root, const char *section, const char *slug,
                                char *err, size_t err_sz);

int gallery_save_images(const char *repo_root, const char *section, const char *slug,
                        const JsonValue *images, char *err, size_t err_sz);
int gallery_save_details(const char *repo_root, const char *section, const char *slug,
                         const JsonValue *details, const JsonValue *page_values,
                         const JsonValue *related, char *err, size_t err_sz);
int gallery_revert_folder(const char *repo_root, const char *section, const char *slug,
                          char *err, size_t err_sz);

/* File tree, raw page editor, widget patches, and new-page templates.
 * Return codes: 0 ok, 1 validation, 2 not found, 3 conflict, -1 internal. */
JsonDoc *gallery_tree_document(const char *repo_root, char *err, size_t err_sz);
int gallery_page_document(const char *repo_root, const char *public_path, JsonDoc **out, char *err,
                          size_t err_sz);
void gallery_attach_page_slides(const char *repo_root, JsonDoc *doc);
int gallery_page_save(const char *repo_root, const char *public_path, const char *content,
                      int run_page_values, JsonDoc **out, char *err, size_t err_sz);
int gallery_widget_apply(const char *repo_root, const char *public_path, const char *content,
                         int has_content, const char *widget, const JsonValue *data, int dry_run,
                         JsonDoc **out, char *err, size_t err_sz);
JsonDoc *gallery_templates_document(char *err, size_t err_sz);
int gallery_page_create(const char *repo_root, const JsonValue *body, JsonDoc **out, char *err,
                        size_t err_sz);

#endif
