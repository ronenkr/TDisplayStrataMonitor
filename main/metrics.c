#include "metrics.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jstream.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "wifi.h"

static const char *TAG = "metrics";

// Resolves the server's name through the router's DNS. lwIP does not apply search domains, so a bare
// name such as "my-pc" is tried as given and then as "<name>.lan", which is how the router
// registers its DHCP clients. An IP address passes straight through.
static bool resolve(const char *name, char *ip, size_t n)
{
    char with_lan[64];
    const char *tries[2] = {name, NULL};
    if (!strchr(name, '.')) {
        snprintf(with_lan, sizeof(with_lan), "%s.lan", name);
        tries[1] = with_lan;
    }
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    for (int i = 0; i < 2 && tries[i]; i++) {
        struct addrinfo *res = NULL;
        if (getaddrinfo(tries[i], NULL, &hints, &res) == 0 && res) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ip, n);
            freeaddrinfo(res);
            return true;
        }
    }
    return false;
}

static config_t cfg;
static metrics_t shared;
static SemaphoreHandle_t lock;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static float num(const char *v)
{
    if (!strcmp(v, "null")) return NAN;
    char *end;
    double d = strtod(v, &end);
    return end == v ? NAN : (float)d;
}

typedef struct { metrics_t m; bool saw_state; } parse_t;

// Picks the handful of fields we show out of Strata's /metrics document.
static void on_value(void *ctx, const char *path, const char *v, bool is_str)
{
    parse_t *p = ctx;
    metrics_t *m = &p->m;
    if (is_str) {
        if (!strcmp(path, "live.state")) { strlcpy(m->state, v, sizeof(m->state)); p->saw_state = true; }
        else if (!strcmp(path, "engine.model")) strlcpy(m->model, v, sizeof(m->model));
        return;
    }
    if (!strcmp(path, "live.tok_s")) m->tok_s = num(v);
    else if (!strcmp(path, "requests.0.decode_tok_s")) m->last_tok_s = num(v);
    else if (!strcmp(path, "live.prefill_tok_s_mean")) m->prefill_tok_s = num(v);
    else if (!strcmp(path, "live.queued")) m->queued = (int)num(v);
    else if (!strcmp(path, "live.prompt_read")) m->prompt_read = (int)num(v);
    else if (!strcmp(path, "live.prompt_total")) m->prompt_total = (int)num(v);
    else if (!strcmp(path, "hardware.ram_used")) m->ram_used = num(v);
    else if (!strcmp(path, "hardware.ram_total")) m->ram_total = num(v);
    else if (!strcmp(path, "hardware.disk_read_mb")) m->disk_read_mb = num(v);
    else if (!strcmp(path, "hardware.disk_write_mb")) m->disk_write_mb = num(v);
    else if (!strcmp(path, "hardware.cpu")) m->cpu = num(v);
    else if (!strcmp(path, "hardware.gpu_util")) m->gpu_util = num(v);
    else if (!strcmp(path, "hardware.gpu_temp")) m->gpu_temp = num(v);
}

static void publish_link(link_state_t link, int status, bool failed)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    shared.link = link;
    shared.http_status = status;
    shared.fail_count = failed ? shared.fail_count + 1 : 0;
    xSemaphoreGive(lock);
}

static void poll_once(void)
{
    char url[96];
    // cfg.host is "name-or-ip[:port]"
    char name[48], ip[16], *colon = strrchr(cfg.host, ':');
    snprintf(name, sizeof(name), "%.*s", colon ? (int)(colon - cfg.host) : (int)strlen(cfg.host), cfg.host);
    if (!resolve(name, ip, sizeof(ip))) {
        ESP_LOGW(TAG, "cannot resolve '%s'", name);
        publish_link(LINK_NO_NAME, 0, true);
        return;
    }
    static char last_ip[16];
    if (strcmp(ip, last_ip)) {
        ESP_LOGI(TAG, "'%s' is %s", name, ip);
        strlcpy(last_ip, ip, sizeof(last_ip));
    }
    snprintf(url, sizeof(url), "http://%s:%s/metrics", ip, colon ? colon + 1 : "80");
    esp_http_client_config_t hc = {.url = url, .timeout_ms = 3000, .buffer_size = 1024, .disable_auto_redirect = true};
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (!c) { publish_link(LINK_UNREACHABLE, 0, true); return; }
    if (cfg.key[0]) {
        char auth[160];
        snprintf(auth, sizeof(auth), "Bearer %s", cfg.key);
        esp_http_client_set_header(c, "Authorization", auth);
    }
    esp_http_client_set_header(c, "Connection", "close");

    if (esp_http_client_open(c, 0) != ESP_OK) {
        int e = esp_http_client_get_errno(c);
        ESP_LOGW(TAG, "connect failed (errno %d)", e);
        // Refused/reset: the PC is there but nothing listens on the port. Anything else: no answer.
        bool refused = e == ECONNREFUSED || e == ECONNRESET;
        publish_link(refused ? LINK_REFUSED : LINK_UNREACHABLE, 0, true);
        goto out;
    }
    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    if (status == 401 || status == 403) { publish_link(LINK_AUTH, status, true); goto out; }
    if (status != 200) { publish_link(LINK_HTTP_ERROR, status, true); goto out; }

    static parse_t parse;   // static: keep it off the task stack
    static jstream_t js;
    memset(&parse, 0, sizeof(parse));
    parse.m.tok_s = parse.m.last_tok_s = parse.m.prefill_tok_s = NAN;
    parse.m.ram_used = parse.m.ram_total = NAN;
    parse.m.disk_read_mb = parse.m.disk_write_mb = parse.m.cpu = parse.m.gpu_util = parse.m.gpu_temp = NAN;
    jstream_init(&js, on_value, &parse);

    char buf[1024];
    int n;
    while ((n = esp_http_client_read(c, buf, sizeof(buf))) > 0) jstream_feed(&js, buf, n);
    if (n < 0) {
        publish_link(LINK_UNREACHABLE, status, true);   // connection died mid-response
        goto out;
    }
    if (!parse.saw_state) { publish_link(LINK_BAD_DATA, status, true); goto out; }

    xSemaphoreTake(lock, portMAX_DELAY);
    shared = parse.m;
    shared.link = LINK_OK;
    shared.http_status = status;
    shared.fail_count = 0;
    shared.have_data = true;
    shared.last_ok_ms = now_ms();
    metrics_t s = shared;
    xSemaphoreGive(lock);

    static int polls;
    if (polls++ % 10 == 0) {
        ESP_LOGI(TAG, "%s tok/s=%.1f last=%.1f disk=%.1fMB/s ram=%.2f/%.2fGB", s.state, s.tok_s, s.last_tok_s,
                 s.disk_read_mb, s.ram_used / 1073741824.0, s.ram_total / 1073741824.0);
    }
out:
    esp_http_client_cleanup(c);
}

static void task(void *arg)
{
    for (;;) {
        if (!cfg.ssid[0]) {
            publish_link(LINK_NO_CONFIG, 0, false);
        } else if (!wifi_is_connected()) {
            publish_link(LINK_WIFI_DOWN, 0, false);
        } else {
            poll_once();
        }
        vTaskDelay(pdMS_TO_TICKS(shared.link == LINK_OK ? 1000 : 2000));
    }
}

void metrics_start(const config_t *c)
{
    cfg = *c;
    lock = xSemaphoreCreateMutex();
    shared.link = c->ssid[0] ? LINK_CONNECTING : LINK_NO_CONFIG;
    xTaskCreate(task, "metrics", 6144, NULL, 5, NULL);
}

void metrics_get(metrics_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = shared;
    xSemaphoreGive(lock);
}
