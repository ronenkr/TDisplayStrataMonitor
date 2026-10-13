#include "config.h"
#include <string.h>
#include "nvs.h"

#define NS "strata"

static void get(nvs_handle_t h, const char *k, char *out, size_t n, const char *def)
{
    size_t len = n;
    if (nvs_get_str(h, k, out, &len) != ESP_OK) {
        strlcpy(out, def, n);
    }
}

void config_load(config_t *c)
{
    nvs_handle_t h;
    memset(c, 0, sizeof(*c));
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        strlcpy(c->host, DEFAULT_HOST, sizeof(c->host));
        return;
    }
    get(h, "ssid", c->ssid, sizeof(c->ssid), "");
    get(h, "pass", c->pass, sizeof(c->pass), "");
    get(h, "host", c->host, sizeof(c->host), DEFAULT_HOST);
    get(h, "key", c->key, sizeof(c->key), "");
    nvs_close(h);
}

bool config_set(const char *name, const char *value)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, name, value) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

void config_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}
