#pragma once
#include <stdbool.h>
#include <stddef.h>

// A tiny streaming JSON scanner. It never buffers the document: it reports every scalar whose
// path is at most JS_MAX_DEPTH levels deep as e.g. "hardware.ram_used" or "requests.0.decode_tok_s"
// (array elements are named by index) and skips the rest (such as the long "history" arrays).
#define JS_MAX_DEPTH 3
#define JS_KEY_LEN   24
#define JS_VAL_LEN   64

// `val` is the raw token (number/true/false/null) or the (possibly truncated) string content.
typedef void (*js_cb_t)(void *ctx, const char *path, const char *val, bool is_string);

typedef struct {
    struct { char is_obj; bool expect_key; int idx; char key[JS_KEY_LEN]; } f[JS_MAX_DEPTH];
    int depth;                 // real nesting depth, may exceed JS_MAX_DEPTH
    bool in_str, esc, str_is_key;
    char str[JS_VAL_LEN]; int str_len;
    char tok[JS_VAL_LEN]; int tok_len;
    js_cb_t cb; void *ctx;
} jstream_t;

void jstream_init(jstream_t *j, js_cb_t cb, void *ctx);
void jstream_feed(jstream_t *j, const char *data, size_t len);
