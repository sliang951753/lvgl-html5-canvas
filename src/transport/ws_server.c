/**
 * @file ws_server.c
 * Minimal libwebsockets server.
 *
 * M0 scope:
 *  - Accept any path, sub-protocol "lhc-v0".
 *  - Track connected viewers, log connect/disconnect.
 *  - Send a 4-byte heartbeat every ~1s so the browser can verify the link.
 *
 * M1 scope:
 *  - lhc_ws_server_broadcast() queues a frame on every viewer; lws callback
 *    pulls from a per-viewer ring on LWS_CALLBACK_SERVER_WRITEABLE.
 *  - For now broadcast() is a no-op placeholder.
 */
#include "ws_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libwebsockets.h>

#define LHC_MAX_VIEWERS 8

struct lhc_ws_server {
    struct lws_context *ctx;
    int viewer_count;
    uint64_t last_heartbeat_ms;
};

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

struct per_session_data {
    int dummy;
};

static lhc_ws_server_t *g_srv = NULL; /* lws callbacks need to find the server */

static int callback_lhc(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
{
    (void)user; (void)in; (void)len;
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        if (g_srv) g_srv->viewer_count++;
        lwsl_user("lhc: viewer connected (total=%d)\n",
                  g_srv ? g_srv->viewer_count : -1);
        break;
    case LWS_CALLBACK_CLOSED:
        if (g_srv && g_srv->viewer_count > 0) g_srv->viewer_count--;
        lwsl_user("lhc: viewer disconnected (total=%d)\n",
                  g_srv ? g_srv->viewer_count : -1);
        break;
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        /* M0: send a tiny heartbeat (4 bytes: 'L','H','C',0). */
        unsigned char buf[LWS_PRE + 4];
        unsigned char *p = &buf[LWS_PRE];
        p[0] = 'L'; p[1] = 'H'; p[2] = 'C'; p[3] = 0;
        int n = lws_write(wsi, p, 4, LWS_WRITE_BINARY);
        if (n < 4) {
            lwsl_warn("lhc: short write %d\n", n);
            return -1;
        }
        break;
    }
    case LWS_CALLBACK_RECEIVE:
        /* M4: forward upstream bytes to input ring. M0: just log. */
        lwsl_user("lhc: rx %zu bytes (ignored in M0)\n", len);
        break;
    default:
        break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    { "lhc-v0", callback_lhc, sizeof(struct per_session_data), 4096, 0, NULL, 0 },
    LWS_PROTOCOL_LIST_TERM
};

lhc_ws_server_t *lhc_ws_server_start(int port)
{
    lhc_ws_server_t *srv = calloc(1, sizeof(*srv));
    if (!srv) return NULL;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;

    lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN, NULL);

    srv->ctx = lws_create_context(&info);
    if (!srv->ctx) {
        fprintf(stderr, "lhc: lws_create_context failed\n");
        free(srv);
        return NULL;
    }
    g_srv = srv;
    srv->last_heartbeat_ms = now_ms();
    fprintf(stderr, "lhc: WS server listening on :%d (subprotocol=lhc-v0)\n", port);
    return srv;
}

void lhc_ws_server_service(lhc_ws_server_t *srv, int timeout_ms)
{
    if (!srv) return;
    lws_service(srv->ctx, timeout_ms);

    /* M0 heartbeat: every 1s, request callback_on_writable for everyone. */
    uint64_t now = now_ms();
    if (now - srv->last_heartbeat_ms >= 1000 && srv->viewer_count > 0) {
        srv->last_heartbeat_ms = now;
        lws_callback_on_writable_all_protocol(srv->ctx, &protocols[0]);
    }
}

void lhc_ws_server_broadcast(lhc_ws_server_t *srv, const uint8_t *data, size_t len)
{
    /* M1: queue per-viewer ring and request writable. M0 placeholder. */
    (void)srv; (void)data; (void)len;
}

void lhc_ws_server_stop(lhc_ws_server_t *srv)
{
    if (!srv) return;
    lws_context_destroy(srv->ctx);
    if (g_srv == srv) g_srv = NULL;
    free(srv);
}
