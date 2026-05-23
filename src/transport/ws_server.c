/**
 * @file ws_server.c
 * libwebsockets server, runs on its own thread.
 *
 * M1 scope:
 *  - subprotocol "lhc-v0", binary only.
 *  - lhc_ws_server_broadcast() pushes a frame to every viewer's ring;
 *    LWS_CALLBACK_SERVER_WRITEABLE drains the ring with lws_write().
 *  - One drop-oldest ring per viewer (16 slots).
 *  - Service loop runs on a dedicated pthread so lvgl rendering on the
 *    main thread doesn't block on libwebsockets poll() (which can sit on
 *    epoll_wait for ~1s waiting for its own scheduled timers, even with
 *    timeout_ms=0). Broadcast wakes the service thread with
 *    lws_cancel_service().
 */
#include "ws_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include <libwebsockets.h>

#define LHC_RING_CAPACITY 16

typedef struct {
    uint8_t *data;
    size_t   len;
} lhc_msg_t;

struct per_session_data {
    lhc_msg_t ring[LHC_RING_CAPACITY];
    int       head;
    int       tail;
    int       count;
    int       dropped;
};

struct lhc_ws_server {
    struct lws_context *ctx;
    int viewer_count;
    pthread_t thread;
    volatile int running;
};

static lhc_ws_server_t *g_srv = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

#define LHC_MAX_VIEWERS 8
static struct per_session_data *g_sessions[LHC_MAX_VIEWERS];
static struct lws              *g_wsi[LHC_MAX_VIEWERS];
static int                      g_session_count = 0;

static void session_add(struct lws *wsi, struct per_session_data *pss)
{
    if (g_session_count >= LHC_MAX_VIEWERS) return;
    g_sessions[g_session_count] = pss;
    g_wsi[g_session_count] = wsi;
    g_session_count++;
}
static void session_remove(struct lws *wsi)
{
    for (int i = 0; i < g_session_count; i++) {
        if (g_wsi[i] == wsi) {
            struct per_session_data *pss = g_sessions[i];
            while (pss->count > 0) {
                free(pss->ring[pss->tail].data);
                pss->ring[pss->tail].data = NULL;
                pss->tail = (pss->tail + 1) % LHC_RING_CAPACITY;
                pss->count--;
            }
            g_sessions[i] = g_sessions[g_session_count - 1];
            g_wsi[i]      = g_wsi[g_session_count - 1];
            g_session_count--;
            return;
        }
    }
}

static void ring_push(struct per_session_data *pss, const uint8_t *data, size_t len)
{
    if (pss->count == LHC_RING_CAPACITY) {
        free(pss->ring[pss->tail].data);
        pss->ring[pss->tail].data = NULL;
        pss->tail = (pss->tail + 1) % LHC_RING_CAPACITY;
        pss->count--;
        pss->dropped++;
    }
    uint8_t *copy = malloc(LWS_PRE + len);
    if (!copy) return;
    memcpy(copy + LWS_PRE, data, len);
    pss->ring[pss->head].data = copy;
    pss->ring[pss->head].len  = len;
    pss->head = (pss->head + 1) % LHC_RING_CAPACITY;
    pss->count++;
}

static int callback_lhc(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
{
    struct per_session_data *pss = (struct per_session_data *)user;
    (void)in;
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        pthread_mutex_lock(&g_lock);
        memset(pss, 0, sizeof(*pss));
        session_add(wsi, pss);
        if (g_srv) g_srv->viewer_count++;
        pthread_mutex_unlock(&g_lock);
        lwsl_user("lhc: viewer connected (total=%d)\n",
                  g_srv ? g_srv->viewer_count : -1);
        break;
    case LWS_CALLBACK_CLOSED:
        pthread_mutex_lock(&g_lock);
        session_remove(wsi);
        if (g_srv && g_srv->viewer_count > 0) g_srv->viewer_count--;
        pthread_mutex_unlock(&g_lock);
        lwsl_user("lhc: viewer disconnected (total=%d)\n",
                  g_srv ? g_srv->viewer_count : -1);
        break;
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        pthread_mutex_lock(&g_lock);
        if (pss->count == 0) { pthread_mutex_unlock(&g_lock); break; }
        lhc_msg_t m = pss->ring[pss->tail];
        pss->ring[pss->tail].data = NULL;
        pss->tail = (pss->tail + 1) % LHC_RING_CAPACITY;
        pss->count--;
        int still_more = pss->count > 0;
        pthread_mutex_unlock(&g_lock);

        int n = lws_write(wsi, m.data + LWS_PRE, m.len, LWS_WRITE_BINARY);
        free(m.data);
        if (n < (int)m.len) {
            lwsl_warn("lhc: short write %d/%zu\n", n, m.len);
            return -1;
        }
        if (still_more) lws_callback_on_writable(wsi);
        break;
    }
    case LWS_CALLBACK_RECEIVE:
        lwsl_user("lhc: rx %zu bytes (ignored in M1)\n", len);
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

static void *ws_thread_main(void *arg)
{
    lhc_ws_server_t *srv = (lhc_ws_server_t *)arg;
    while (srv->running) {
        lws_service(srv->ctx, 50);
    }
    return NULL;
}

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
    srv->running = 1;
    if (pthread_create(&srv->thread, NULL, ws_thread_main, srv) != 0) {
        fprintf(stderr, "lhc: pthread_create failed\n");
        lws_context_destroy(srv->ctx);
        free(srv);
        g_srv = NULL;
        return NULL;
    }
    fprintf(stderr, "lhc: WS server listening on :%d (subprotocol=lhc-v0, dedicated thread)\n", port);
    return srv;
}

void lhc_ws_server_service(lhc_ws_server_t *srv, int timeout_ms)
{
    (void)srv; (void)timeout_ms;
    /* No-op: service runs on a dedicated thread. */
}

void lhc_ws_server_broadcast(lhc_ws_server_t *srv, const uint8_t *data, size_t len)
{
    if (!srv || !data || len == 0) return;
    pthread_mutex_lock(&g_lock);
    if (g_session_count == 0) { pthread_mutex_unlock(&g_lock); return; }
    for (int i = 0; i < g_session_count; i++) {
        ring_push(g_sessions[i], data, len);
        lws_callback_on_writable(g_wsi[i]);
    }
    pthread_mutex_unlock(&g_lock);
    lws_cancel_service(srv->ctx);
}

void lhc_ws_server_stop(lhc_ws_server_t *srv)
{
    if (!srv) return;
    srv->running = 0;
    lws_cancel_service(srv->ctx);
    pthread_join(srv->thread, NULL);
    lws_context_destroy(srv->ctx);
    if (g_srv == srv) g_srv = NULL;
    g_session_count = 0;
    free(srv);
}
