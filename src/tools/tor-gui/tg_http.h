#ifndef TG_HTTP_H
#define TG_HTTP_H

#include "vendor/mongoose.h"
#include "tg_control.h"

/* Forward declaration */
struct tg_events;

/* HTTP server configuration */
typedef struct tg_http_config {
  char listen_addr[256];     /* e.g. "127.0.0.1:8073" */
  char webroot[1024];        /* Path to static/ directory */
  char bearer_token[65];     /* Random auth token */
  int no_browser;            /* Don't auto-open browser */
} tg_http_config_t;

/* HTTP server state */
typedef struct tg_http {
  struct mg_mgr mgr;
  struct mg_connection *listener;
  tg_http_config_t config;
  tg_ctrl_t *ctrl;           /* Reference to control port client */

  /* Event state for bandwidth history, etc. */
  struct tg_events *events;

  /* WebSocket clients list */
  struct mg_connection *ws_clients[64];
  int ws_client_count;
} tg_http_t;

/* Initialize and start HTTP server. Returns 0 on success */
int tg_http_init(tg_http_t *http, tg_ctrl_t *ctrl,
                 const tg_http_config_t *config);

/* Run one poll cycle (call in main loop) */
void tg_http_poll(tg_http_t *http, int timeout_ms);

/* Broadcast message to all WebSocket clients */
void tg_http_ws_broadcast(tg_http_t *http, const char *msg, size_t len);

/* Add/remove WebSocket client */
void tg_http_ws_add(tg_http_t *http, struct mg_connection *c);
void tg_http_ws_remove(tg_http_t *http, struct mg_connection *c);

/* Clean up */
void tg_http_free(tg_http_t *http);

/* Open browser to the GUI URL */
void tg_http_open_browser(const char *url);

#endif /* TG_HTTP_H */
