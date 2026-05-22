/**
 * @file ws_server.h
 * libwebsockets server for lvgl-html5-canvas.
 *
 * M0: opens port, accepts connections, prints client connect/disconnect,
 * sends a heartbeat ping every second. No frame broadcasting yet.
 */
#ifndef LHC_WS_SERVER_H
#define LHC_WS_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lhc_ws_server lhc_ws_server_t;

/** Create + start the server on the given port. NULL on error. */
lhc_ws_server_t *lhc_ws_server_start(int port);

/** Service pending events (call from main loop, non-blocking). */
void lhc_ws_server_service(lhc_ws_server_t *srv, int timeout_ms);

/** Broadcast a binary frame to all connected viewers (M1+). */
void lhc_ws_server_broadcast(lhc_ws_server_t *srv, const uint8_t *data, size_t len);

/** Stop + free. */
void lhc_ws_server_stop(lhc_ws_server_t *srv);

#ifdef __cplusplus
}
#endif

#endif /* LHC_WS_SERVER_H */
