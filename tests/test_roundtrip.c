#define _POSIX_C_SOURCE 200809L

#include "../src/gallery.h"
#include "../src/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifndef _MSC_VER
#include <dirent.h>
#include <unistd.h>
#endif

#include "../src/compat.h"

static void show_diff(const char *label, const char *path, const char *a, size_t na, const char *b, size_t nb) {
    size_t i = 0;
    size_t n = na < nb ? na : nb;
    size_t start;
    size_t k;

    while (i < n && (unsigned char)a[i] == (unsigned char)b[i]) {
        i++;
    }
    fprintf(stderr, "%s %s at byte %zu (file %zu, generated %zu)\n", label, path, i, na, nb);
    start = i > 40 ? i - 40 : 0;
    fprintf(stderr, "  file: ");
    for (k = start; k < na && k < i + 40; k++) {
        unsigned char ch = (unsigned char)a[k];
        if (ch >= 32 && ch < 127) {
            fputc(ch, stderr);
        } else {
            fprintf(stderr, "\\x%02x", ch);
        }
    }
    fprintf(stderr, "\n  gen:  ");
    for (k = start; k < nb && k < i + 40; k++) {
        unsigned char ch = (unsigned char)b[k];
        if (ch >= 32 && ch < 127) {
            fputc(ch, stderr);
        } else {
            fprintf(stderr, "\\x%02x", ch);
        }
    }
    fputc('\n', stderr);
}

static char *path_join(const char *left, const char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *out = (char *)malloc(left_len + right_len + 2);
    if (!out) {
        return NULL;
    }
    memcpy(out, left, left_len);
    out[left_len] = '/';
    memcpy(out + left_len + 1, right, right_len + 1);
    return out;
}

static int looks_like_root(const char *path) {
    char *sales = path_join(path, "sales");
    char *folder = sales ? path_join(sales, "1905fntypeafourcylinder") : NULL;
    char *probe = folder ? path_join(folder, "deep-data.json") : NULL;
    int ok = probe && access(probe, R_OK) == 0;
    free(sales);
    free(folder);
    free(probe);
    return ok;
}

static char *accept_root(const char *path) {
    char resolved[4096];
    if (!path || !path[0]) {
        return NULL;
    }
    if (realpath(path, resolved) && looks_like_root(resolved)) {
        return strdup(resolved);
    }
    if (looks_like_root(path)) {
        return strdup(path);
    }
    return NULL;
}

static char *sibling_website(const char *base, const char *suffix) {
    char rel[8192];
    size_t base_len;
    size_t suffix_len;
    if (!base || !base[0] || !suffix) {
        return NULL;
    }
    base_len = strlen(base);
    suffix_len = strlen(suffix);
    if (base_len + 1 + suffix_len + 1 > sizeof(rel)) {
        return NULL;
    }
    memcpy(rel, base, base_len);
    rel[base_len] = '/';
    memcpy(rel + base_len + 1, suffix, suffix_len + 1);
    return accept_root(rel);
}

static char *find_root(int argc, char **argv) {
    char cwd[4096];
    char cur[4096];
    char *found;
    int i;

    if (argc > 1) {
        found = accept_root(argv[1]);
        if (found) {
            return found;
        }
    }
    if (!getcwd(cwd, sizeof(cwd))) {
        cwd[0] = '\0';
    }
    snprintf(cur, sizeof(cur), "%s", cwd);
    for (i = 0; cwd[0] && i < 8; i++) {
        char *slash;
        if (looks_like_root(cur)) {
            return strdup(cur);
        }
        slash = strrchr(cur, '/');
        if (!slash || slash == cur) {
            break;
        }
        *slash = '\0';
    }
    found = accept_root("REDACTED_MEDIA/Projects/GIT/Websites/ClassicCars");
    if (found) {
        return found;
    }
    found = sibling_website(cwd, "../../Websites/ClassicCars");
    if (found) {
        return found;
    }
    {
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) {
            char *slash;
            exe[n] = '\0';
            slash = strrchr(exe, '/');
            if (slash) {
                *slash = '\0';
                found = sibling_website(exe, "../../Websites/ClassicCars");
                if (found) {
                    return found;
                }
                found = sibling_website(exe, "../../../Websites/ClassicCars");
                if (found) {
                    return found;
                }
            }
        }
    }
    return NULL;
}

static int cmp_cstr(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int array_has_name(const JsonValue *array, const char *name) {
    size_t i;
    for (i = 0; i < json_array_len(array); i++) {
        const char *got = json_string_get(json_object_get(json_array_at(array, i), "name"), NULL);
        if (got && name && strcasecmp(got, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int names_overlap(const JsonValue *images, const JsonValue *unlisted) {
    size_t i;
    for (i = 0; i < json_array_len(images); i++) {
        const char *name = json_string_get(json_object_get(json_array_at(images, i), "name"), NULL);
        if (array_has_name(unlisted, name)) {
            return 1;
        }
    }
    return 0;
}

static void expect_slide_lists(const char *root, const char *section, const char *slug, int want_images, int want_unlisted,
                               const char *present, const char *absent, int *fails) {
    char err[256];
    JsonDoc *state;
    JsonValue *images;
    JsonValue *unlisted;
    JsonValue *warnings;
    size_t i;
    err[0] = '\0';
    state = gallery_state_document(root, section, slug, err, sizeof(err));
    images = state ? json_object_get(state->root, "images") : NULL;
    unlisted = state ? json_object_get(state->root, "unlistedSlides") : NULL;
    if (!state || (int)json_array_len(images) != want_images || (int)json_array_len(unlisted) != want_unlisted ||
        names_overlap(images, unlisted) || (present && !array_has_name(images, present)) ||
        (absent && (array_has_name(images, absent) || array_has_name(unlisted, absent)))) {
        fprintf(stderr, "gallery %s/%s images=%zu unlisted=%zu (%s)\n", section, slug,
                images ? json_array_len(images) : 0, unlisted ? json_array_len(unlisted) : 0, err[0] ? err : "ok");
        (*fails)++;
    }
    warnings = state ? json_object_get(state->root, "warnings") : NULL;
    if (want_unlisted == 0 && warnings) {
        for (i = 0; i < json_array_len(warnings); i++) {
            const char *text = json_string_get(json_array_at(warnings, i), NULL);
            if (text && (strstr(text, "not in the gallery") || strstr(text, "not listed in deep-data"))) {
                fprintf(stderr, "unexpected unlisted warning for %s/%s: %s\n", section, slug, text);
                (*fails)++;
            }
        }
    }
    json_doc_free(state);
}

static JsonValue *image_list_from_doc(const JsonDoc *doc) {
    JsonValue *objects = json_object_get(doc->root, "objects");
    JsonValue *list = json_array();
    size_t i;
    size_t count = json_array_len(objects);

    if (!list) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        JsonValue *object = json_array_at(objects, i);
        JsonValue *name = json_object_get(object, "name");
        JsonValue *item = json_object();
        JsonValue *caption_html = json_object_get(object, "imageCaption");
        size_t name_len = 0;
        const char *name_text = json_string_get(name, &name_len);
        char *caption = NULL;
        if (!item || !name_text) {
            json_free(item);
            json_free(list);
            free(caption);
            return NULL;
        }
        if (caption_html && json_type(caption_html) == JSON_STRING) {
            size_t html_len = 0;
            const char *html = json_string_get(caption_html, &html_len);
            caption = gallery_caption_from_html(html, html_len);
        }
        json_object_put(item, "name", json_string(name_text, name_len));
        json_object_put(item, "caption", json_string_cstr(caption ? caption : ""));
        free(caption);
        if (json_array_add(list, item) != 0) {
            json_free(list);
            return NULL;
        }
    }
    return list;
}

int main(int argc, char **argv) {
    char *root = find_root(argc, argv);
    char sales[4096];
    DIR *dir;
    struct dirent *ent;
    char **slugs = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t i;
    int json_fail = 0;
    int image_fail = 0;
    int apply_fail = 0;
    int header_fail = 0;
    int checks_fail = 0;

    if (!root) {
        fprintf(stderr, "Could not find the ClassicCars repo root\n");
        return 1;
    }
    snprintf(sales, sizeof(sales), "%s/sales", root);
    dir = opendir(sales);
    if (!dir) {
        fprintf(stderr, "Could not open %s\n", sales);
        free(root);
        return 1;
    }
    while ((ent = readdir(dir)) != NULL) {
        char *folder;
        struct stat st;
        if (ent->d_name[0] == '.') {
            continue;
        }
        folder = path_join(sales, ent->d_name);
        if (!folder || stat(folder, &st) != 0 || !S_ISDIR(st.st_mode)) {
            free(folder);
            continue;
        }
        free(folder);
        if (count == cap) {
            size_t next_cap = cap ? cap * 2 : 64;
            char **next = (char **)realloc(slugs, next_cap * sizeof(*next));
            if (!next) {
                fprintf(stderr, "out of memory\n");
                closedir(dir);
                free(root);
                return 1;
            }
            slugs = next;
            cap = next_cap;
        }
        slugs[count++] = strdup(ent->d_name);
    }
    closedir(dir);
    qsort(slugs, count, sizeof(char *), cmp_cstr);

    for (i = 0; i < count; i++) {
        char *deep_path;
        char *image_path;
        char *folder;
        size_t raw_len = 0;
        char *raw;
        JsonDoc *doc;
        char err[256];
        size_t out_len = 0;
        char *out;
        size_t image_len = 0;
        char *image_text;
        size_t existing_len = 0;
        char *existing;
        JsonValue *images;

        folder = path_join(sales, slugs[i]);
        deep_path = folder ? path_join(folder, "deep-data.json") : NULL;
        image_path = folder ? path_join(folder, "image_data.txt") : NULL;
        if (!deep_path || !image_path) {
            free(folder);
            free(deep_path);
            free(image_path);
            json_fail++;
            continue;
        }
        raw = NULL;
        {
            FILE *file = fopen(deep_path, "rb");
            long size;
            if (!file) {
                fprintf(stderr, "missing %s\n", deep_path);
                json_fail++;
                free(folder);
                free(deep_path);
                free(image_path);
                continue;
            }
            fseek(file, 0, SEEK_END);
            size = ftell(file);
            rewind(file);
            raw = (char *)malloc((size_t)size + 1);
            if (!raw || fread(raw, 1, (size_t)size, file) != (size_t)size) {
                fprintf(stderr, "read failed %s\n", deep_path);
                json_fail++;
                free(raw);
                fclose(file);
                free(folder);
                free(deep_path);
                free(image_path);
                continue;
            }
            raw[size] = '\0';
            raw_len = (size_t)size;
            fclose(file);
        }
        err[0] = '\0';
        doc = json_parse(raw, raw_len, err, sizeof(err));
        if (!doc) {
            fprintf(stderr, "parse failed %s: %s\n", deep_path, err);
            json_fail++;
            free(raw);
            free(folder);
            free(deep_path);
            free(image_path);
            continue;
        }
        out = json_stringify(doc, &out_len);
        if (!out || out_len != raw_len || memcmp(out, raw, raw_len) != 0) {
            show_diff("JSON", deep_path, raw, raw_len, out ? out : "", out ? out_len : 0);
            json_fail++;
        }
        image_text = gallery_image_data_from_doc(doc, &image_len);
        existing = NULL;
        existing_len = 0;
        {
            FILE *file = fopen(image_path, "rb");
            long size;
            if (!file) {
                fprintf(stderr, "missing %s\n", image_path);
                image_fail++;
            } else {
                fseek(file, 0, SEEK_END);
                size = ftell(file);
                rewind(file);
                existing = (char *)malloc((size_t)size + 1);
                if (!existing || fread(existing, 1, (size_t)size, file) != (size_t)size) {
                    image_fail++;
                    free(existing);
                    existing = NULL;
                } else {
                    existing[size] = '\0';
                    existing_len = (size_t)size;
                    if (!image_text || image_len != existing_len || memcmp(image_text, existing, existing_len) != 0) {
                        show_diff("IMAGE", image_path, existing, existing_len, image_text ? image_text : "",
                                  image_text ? image_len : 0);
                        image_fail++;
                    }
                }
                fclose(file);
            }
        }
        images = image_list_from_doc(doc);
        err[0] = '\0';
        if (!images || gallery_apply_image_list(doc, images, folder, err, sizeof(err)) != 0) {
            fprintf(stderr, "apply failed %s: %s\n", slugs[i], err[0] ? err : "unknown");
            apply_fail++;
        } else {
            size_t again_len = 0;
            char *again = json_stringify(doc, &again_len);
            if (!again || again_len != raw_len || memcmp(again, raw, raw_len) != 0) {
                show_diff("APPLY", deep_path, raw, raw_len, again ? again : "", again ? again_len : 0);
                apply_fail++;
            }
            free(again);
        }
        {
            char *index_path = path_join(folder, "index.php");
            GallerySalesHeader header;
            char *rewritten = NULL;
            size_t rewritten_len = 0;
            size_t php_len = 0;
            char *php_text;
            memset(&header, 0, sizeof(header));
            err[0] = '\0';
            if (gallery_read_sales_header(index_path, &header, err, sizeof(err)) != 0 || !header.vehicle_name ||
                !header.vehicle_name[0] || !header.price || !header.price[0]) {
                fprintf(stderr, "header failed %s: %s\n", slugs[i], err);
                header_fail++;
            } else {
                php_text = NULL;
                {
                    FILE *file = fopen(index_path, "rb");
                    long size;
                    if (!file) {
                        header_fail++;
                    } else {
                        fseek(file, 0, SEEK_END);
                        size = ftell(file);
                        rewind(file);
                        php_text = (char *)malloc((size_t)size + 1);
                        if (!php_text || fread(php_text, 1, (size_t)size, file) != (size_t)size) {
                            header_fail++;
                            free(php_text);
                            php_text = NULL;
                        } else {
                            php_text[size] = '\0';
                            php_len = (size_t)size;
                        }
                        fclose(file);
                    }
                }
                if (php_text) {
                    rewritten = gallery_apply_sales_header(php_text, php_len, &header, &rewritten_len, err, sizeof(err));
                    if (!rewritten || rewritten_len != php_len || memcmp(rewritten, php_text, php_len) != 0) {
                        fprintf(stderr, "header rewrite changed %s (%s)\n", slugs[i], err);
                        header_fail++;
                    }
                    if (strcmp(slugs[i], "1925duesenbergmodaboattailroadsterbodybybrunn") == 0) {
                        char *mutated = NULL;
                        size_t mutated_len = 0;
                        char *old_price;
                        if (strcmp(header.vehicle_name, "1925 Duesenberg Model A Boattail Roadster Body by Brunn") != 0 ||
                            strcmp(header.price, "650000") != 0 || strcmp(header.inventory_number, "8096") != 0 ||
                            !header.description || !strstr(header.description, "Brunn")) {
                            fprintf(stderr, "duesenberg header mismatch name=%s price=%s inv=%s\n", header.vehicle_name,
                                    header.price, header.inventory_number);
                            header_fail++;
                        }
                        old_price = header.price;
                        header.price = strdup("SOLD");
                        header.do_not_render = 1;
                        mutated = gallery_apply_sales_header(php_text, php_len, &header, &mutated_len, err, sizeof(err));
                        if (!mutated || !strstr(mutated, "$price = \"SOLD\";") || !strstr(mutated, "$doNotRender = true;") ||
                            strstr(mutated, "$price = 650000") || !strstr(mutated, "Boattail Roadster")) {
                            fprintf(stderr, "duesenberg header mutation failed\n");
                            header_fail++;
                        }
                        free(mutated);
                        free(header.price);
                        header.price = old_price;
                        header.do_not_render = 0;
                    }
                    if (strcmp(slugs[i], "1909walkerbakerswagon") == 0 &&
                        strcmp(header.vehicle_name, "1909 Walker Electric Baker's Wagon") != 0) {
                        fprintf(stderr, "apostrophe header mismatch: %s\n", header.vehicle_name);
                        header_fail++;
                    }
                    if (strcmp(slugs[i], "1915indianv_twin") == 0 && header.description && header.description[0]) {
                        fprintf(stderr, "expected empty description for 1915indianv_twin\n");
                        header_fail++;
                    }
                }
                free(rewritten);
                free(php_text);
            }
            gallery_sales_header_free(&header);
            free(index_path);
        }
        json_free(images);
        json_doc_free(doc);
        free(out);
        free(image_text);
        free(existing);
        free(raw);
        free(folder);
        free(deep_path);
        free(image_path);
    }

    {
        int extra_fail = 0;
        char err[512];
        char *folder;
        char *deep_path;
        FILE *file;
        long size;
        char *raw;
        JsonDoc *doc;
        JsonValue *list;
        JsonValue *bad;
        JsonValue *item;
        JsonValue *first;
        JsonValue *second;
        JsonValue *reordered;
        size_t nimages;
        size_t k;
        const char *equals_error = "Captions cannot contain '=' because image_data.txt uses it as the delimiter.";
        int width = 0;
        int height = 0;
        JsonDoc *state;
        JsonDoc *folders_doc;
        char *state_text;
        size_t state_len = 0;

        folder = path_join(root, "sales");
        {
            char *slug_dir = folder ? path_join(folder, "1925duesenbergmodaboattailroadsterbodybybrunn") : NULL;
            free(folder);
            folder = slug_dir;
        }
        deep_path = folder ? path_join(folder, "deep-data.json") : NULL;
        file = deep_path ? fopen(deep_path, "rb") : NULL;
        size = -1;
        raw = NULL;
        if (file) {
            fseek(file, 0, SEEK_END);
            size = ftell(file);
            rewind(file);
            if (size >= 0) {
                raw = (char *)malloc((size_t)size + 1);
            }
        }
        if (!file || !raw || fread(raw, 1, (size_t)size, file) != (size_t)size) {
            fprintf(stderr, "extra setup failed\n");
            extra_fail++;
        } else {
            raw[size] = '\0';
            doc = json_parse(raw, (size_t)size, err, sizeof(err));
            list = doc ? image_list_from_doc(doc) : NULL;
            nimages = json_array_len(list);
            bad = json_array();
            item = json_object();
            first = json_array_at(list, 0);
            json_object_put(item, "name", json_string_cstr(json_string_get(json_object_get(first, "name"), NULL)));
            json_object_put(item, "caption", json_string_cstr("no=equals"));
            json_array_add(bad, item);
            err[0] = '\0';
            if (!doc || gallery_apply_image_list(doc, bad, folder, err, sizeof(err)) != 1 || strcmp(err, equals_error) != 0) {
                fprintf(stderr, "equals caption rejection failed: %s\n", err);
                extra_fail++;
            }
            json_free(bad);
            bad = json_array();
            item = json_object();
            json_object_put(item, "name", json_string_cstr(json_string_get(json_object_get(first, "name"), NULL)));
            json_object_put(item, "caption", json_string_cstr("line\nbreak"));
            json_array_add(bad, item);
            err[0] = '\0';
            if (gallery_apply_image_list(doc, bad, folder, err, sizeof(err)) != 1) {
                fprintf(stderr, "newline caption rejection failed: %s\n", err);
                extra_fail++;
            }
            json_free(bad);
            reordered = json_array();
            second = json_array_detach(list, 1);
            first = json_array_detach(list, 0);
            json_array_add(reordered, second);
            json_array_add(reordered, first);
            while (json_array_len(list) > 0) {
                json_array_add(reordered, json_array_detach(list, 0));
            }
            err[0] = '\0';
            if (gallery_apply_image_list(doc, reordered, folder, err, sizeof(err)) != 0) {
                fprintf(stderr, "reorder apply failed: %s\n", err);
                extra_fail++;
            } else {
                JsonValue *objects = json_object_get(doc->root, "objects");
                JsonValue *moved = json_array_at(objects, 0);
                JsonValue *caption = json_object_get(moved, "thumbCaption");
                const char *caption_text = json_string_get(caption, NULL);
                char expect[64];
                snprintf(expect, sizeof(expect), "<div class=\"nr\">1 / %zu</div>", nimages);
                if (!caption_text || strcmp(caption_text, expect) != 0) {
                    fprintf(stderr, "thumb caption after reorder: %s\n", caption_text ? caption_text : "(null)");
                    extra_fail++;
                }
            }
            json_free(reordered);
            json_free(list);
            json_doc_free(doc);
        }
        if (file) {
            fclose(file);
        }
        free(raw);
        free(folder);
        free(deep_path);
        if (gallery_image_size("REDACTED_MEDIA/Projects/GIT/Websites/ClassicCars/sales/1925duesenbergmodaboattailroadsterbodybybrunn/slides/1925 Duesenberg Model A Boattail Roadster Body by Brunn 2026-07-13 125606.jpg",
                               &width, &height) != 0 ||
            width != 4000 || height != 3000) {
            fprintf(stderr, "jpeg size %d x %d\n", width, height);
            extra_fail++;
        }
        width = 0;
        height = 0;
        if (gallery_image_size("REDACTED_MEDIA/Projects/GIT/Websites/ClassicCars/articles/2023shappyshow/slides/2023shappyshow Chifferobe TropiGals.png",
                               &width, &height) != 0 ||
            width != 2546 || height != 1282) {
            fprintf(stderr, "png size %d x %d\n", width, height);
            extra_fail++;
        }
        err[0] = '\0';
        state = gallery_state_document(root, "sales", "1925duesenbergmodaboattailroadsterbodybybrunn", err, sizeof(err));
        state_text = state ? json_stringify(state, &state_len) : NULL;
        if (!state || !state_text || !strstr(state_text, "\"ok\":true") ||
            !strstr(state_text, "1925 Duesenberg Model A Boattail Roadster Body by Brunn") ||
            json_array_len(json_object_get(state->root, "images")) < 2) {
            fprintf(stderr, "gallery state failed: %s\n", err);
            extra_fail++;
        }
        json_doc_free(state);
        free(state_text);
        err[0] = '\0';
        folders_doc = gallery_folders_document(root, "sales", err, sizeof(err));
        if (!folders_doc || json_array_len(json_object_get(folders_doc->root, "folders")) != count) {
            fprintf(stderr, "folder list failed: %s count=%zu\n", err,
                    folders_doc ? json_array_len(json_object_get(folders_doc->root, "folders")) : 0);
            extra_fail++;
        } else {
            int found = 0;
            JsonValue *folders_json = json_object_get(folders_doc->root, "folders");
            for (k = 0; k < json_array_len(folders_json); k++) {
                const char *slug = json_string_get(json_object_get(json_array_at(folders_json, k), "slug"), NULL);
                if (slug && strcmp(slug, "1925duesenbergmodaboattailroadsterbodybybrunn") == 0) {
                    const char *hero = json_string_get(json_object_get(json_array_at(folders_json, k), "heroThumbUrl"), NULL);
                    found = 1;
                    if (!hero || !strstr(hero, "folderthumb.jpg")) {
                        fprintf(stderr, "duesenberg heroThumbUrl: %s\n", hero ? hero : "(null)");
                        extra_fail++;
                    }
                } else if (slug && !json_string_get(json_object_get(json_array_at(folders_json, k), "heroThumbUrl"), NULL)) {
                    fprintf(stderr, "missing heroThumbUrl for %s\n", slug);
                    extra_fail++;
                }
            }
            if (!found) {
                fprintf(stderr, "folder list missing duesenberg\n");
                extra_fail++;
            }
        }
        json_doc_free(folders_doc);
        expect_slide_lists(root, "articles", "greenwichconcours2026", 12, 0, "2026-06-03-02.jpg", "2026-06-09-01.jpg",
                           &extra_fail);
        expect_slide_lists(root, "sales", "1925duesenbergmodaboattailroadsterbodybybrunn", 53, 0, NULL, NULL, &extra_fail);
        expect_slide_lists(root, "sales", "1931cadv16lancefieldconvert", 19, 25, NULL, NULL, &extra_fail);
        expect_slide_lists(root, "awards", "2022greenwichconcoursdelegance", 12, 21,
                           "1930 Cadillac Wins Best in Class at Concours d'Elegance (Medium).png", NULL, &extra_fail);
        {
            char err[256];
            JsonDoc *page = NULL;
            err[0] = '\0';
            if (gallery_page_document(root, "/articles/greenwichconcours2026/greenwichconcours2026.html", &page, err,
                                      sizeof(err)) != 0) {
                fprintf(stderr, "greenwich page document failed: %s\n", err);
                extra_fail++;
            } else {
                gallery_attach_page_slides(root, page);
                if ((int)json_array_len(json_object_get(page->root, "images")) != 12 ||
                    (int)json_array_len(json_object_get(page->root, "unlistedSlides")) != 0) {
                    fprintf(stderr, "greenwich page slides images=%zu unlisted=%zu\n",
                            json_array_len(json_object_get(page->root, "images")),
                            json_array_len(json_object_get(page->root, "unlistedSlides")));
                    extra_fail++;
                }
                {
                    JsonValue *errors = json_object_get(page->root, "errors");
                    size_t e;
                    for (e = 0; e < json_array_len(errors); e++) {
                        const char *id = json_string_get(json_object_get(json_array_at(errors, e), "id"), NULL);
                        const char *message = json_string_get(json_object_get(json_array_at(errors, e), "message"), NULL);
                        if (id && strstr(id, "gallery-mismatch") && message &&
                            (strstr(message, "not in the gallery") || strstr(message, "not documented") ||
                             strstr(message, "not listed"))) {
                            fprintf(stderr, "greenwich gallery-mismatch: %s\n", message);
                            extra_fail++;
                        }
                    }
                }
            }
            json_doc_free(page);
        }
        checks_fail = extra_fail;
        printf("checks: caption guards, reorder, image sizes, gallery payload (%d failed)\n", checks_fail);
    }

    printf("round-trip: %zu/%zu deep-data.json byte-for-byte (%d failed)\n", count - (size_t)json_fail, count, json_fail);
    printf("image_data: %zu/%zu parity (%d failed)\n", count - (size_t)image_fail, count, image_fail);
    printf("noop-save: %zu/%zu unchanged (%d failed)\n", count - (size_t)apply_fail, count, apply_fail);
    printf("php-header: %d failed\n", header_fail);
    for (i = 0; i < count; i++) {
        free(slugs[i]);
    }
    free(slugs);
    free(root);
    return (json_fail || image_fail || apply_fail || header_fail || checks_fail) ? 1 : 0;
}
