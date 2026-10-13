#include "jstream.h"
#include <stdio.h>
#include <string.h>

void jstream_init(jstream_t *j, js_cb_t cb, void *ctx)
{
    memset(j, 0, sizeof(*j));
    j->cb = cb;
    j->ctx = ctx;
}

// Builds "a.b.c" for the current position (only valid while depth <= JS_MAX_DEPTH).
static void emit(jstream_t *j, const char *val, bool is_string)
{
    if (j->depth == 0 || j->depth > JS_MAX_DEPTH) return;
    char path[JS_MAX_DEPTH * (JS_KEY_LEN + 1) + 1];
    size_t n = 0;
    for (int i = 0; i < j->depth; i++) {
        if (i) path[n++] = '.';
        if (j->f[i].is_obj) {
            n += snprintf(path + n, sizeof(path) - n, "%s", j->f[i].key);
        } else {
            n += snprintf(path + n, sizeof(path) - n, "%d", j->f[i].idx);
        }
    }
    j->cb(j->ctx, path, val, is_string);
}

static void end_token(jstream_t *j)
{
    if (!j->tok_len) return;
    j->tok[j->tok_len] = 0;
    j->tok_len = 0;
    emit(j, j->tok, false);
}

static void push(jstream_t *j, bool obj)
{
    if (j->depth < JS_MAX_DEPTH) {
        j->f[j->depth].is_obj = obj;
        j->f[j->depth].expect_key = obj;
        j->f[j->depth].idx = 0;
        j->f[j->depth].key[0] = 0;
    }
    j->depth++;
}

static void feed_char(jstream_t *j, char c)
{
    if (j->in_str) {
        if (j->esc) {
            j->esc = false;
            if (j->str_len < JS_VAL_LEN - 1) j->str[j->str_len++] = c;
        } else if (c == '\\') {
            j->esc = true;
        } else if (c == '"') {
            j->in_str = false;
            j->str[j->str_len] = 0;
            int d = j->depth - 1;
            if (d >= 0 && d < JS_MAX_DEPTH && j->f[d].is_obj && j->f[d].expect_key) {
                strlcpy(j->f[d].key, j->str, JS_KEY_LEN);
            } else {
                emit(j, j->str, true);
            }
        } else if (j->str_len < JS_VAL_LEN - 1) {
            j->str[j->str_len++] = c;
        }
        return;
    }
    switch (c) {
    case '"': j->in_str = true; j->str_len = 0; break;
    case '{': end_token(j); push(j, true); break;
    case '[': end_token(j); push(j, false); break;
    case '}': case ']':
        end_token(j);
        if (j->depth > 0) j->depth--;
        break;
    case ':': {
        int d = j->depth - 1;
        if (d >= 0 && d < JS_MAX_DEPTH) j->f[d].expect_key = false;
        break;
    }
    case ',': {
        end_token(j);
        int d = j->depth - 1;
        if (d >= 0 && d < JS_MAX_DEPTH) {
            if (j->f[d].is_obj) j->f[d].expect_key = true; else j->f[d].idx++;
        }
        break;
    }
    case ' ': case '\t': case '\r': case '\n': end_token(j); break;
    default:
        if (j->tok_len < JS_VAL_LEN - 1) j->tok[j->tok_len++] = c;
    }
}

void jstream_feed(jstream_t *j, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) feed_char(j, data[i]);
}
