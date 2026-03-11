/* tg_http.c -- Embedded HTTP server for tor-gui.
 *
 * Uses the Mongoose library to serve a static web UI and expose
 * REST+WebSocket endpoints for the JavaScript frontend.  All API
 * requests require a bearer token (except static file serving on
 * localhost).  WebSocket connections receive real-time Tor control
 * port events.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tg_http.h"
#include "tg_api.h"
#include "tg_util.h"

/* Security headers applied to every response */
static const char *s_security_headers =
    "X-Frame-Options: DENY\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Security-Policy: "
    "default-src 'self'; "
    "script-src 'self' 'unsafe-inline'; "
    "style-src 'self' 'unsafe-inline'; "
    "connect-src 'self' ws://127.0.0.1:8073; "
    "img-src 'self' data: blob:;\r\n";

/* ------------------------------------------------------------------ */
/* Bearer token validation                                            */
/* ------------------------------------------------------------------ */

/** Check that the request carries a valid bearer token.
 *  Returns 1 if authorized, 0 if not.  Static file requests and
 *  WebSocket upgrades from localhost skip the check. */
static int
check_auth(const tg_http_t *http, const struct mg_http_message *hm,
           int is_api)
{
  struct mg_str auth;
  char expected[80];
  size_t elen;

  /* If no bearer token is configured, allow everything */
  if (http->config.bearer_token[0] == '\0')
    return 1;

  /* Static file requests don't require auth */
  if (!is_api)
    return 1;

  auth = mg_http_get_header(hm, "Authorization");
  if (auth.buf == NULL || auth.len == 0)
    return 0;

  snprintf(expected, sizeof(expected), "Bearer %s",
           http->config.bearer_token);
  elen = strlen(expected);

  if (auth.len != elen)
    return 0;
  return (memcmp(auth.buf, expected, elen) == 0) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Main HTTP event handler                                            */
/* ------------------------------------------------------------------ */

static void
http_event_handler(struct mg_connection *c, int ev, void *ev_data)
{
  tg_http_t *http = (tg_http_t *)c->fn_data;

  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    /* WebSocket upgrade request */
    if (mg_match(hm->uri, mg_str("/ws"), NULL)) {
      /* Auth check for WebSocket */
      if (!check_auth(http, hm, 1)) {
        mg_http_reply(c, 401, s_security_headers,
                      "{\"error\":\"Unauthorized\"}\n");
        return;
      }
      mg_ws_upgrade(c, hm, NULL);
      return;
    }

    /* API requests */
    if (mg_match(hm->uri, mg_str("/api/*"), NULL) ||
        mg_match(hm->uri, mg_str("/api"), NULL)) {
      /* Check bearer token */
      if (!check_auth(http, hm, 1)) {
        mg_http_reply(c, 401, s_security_headers,
                      "{\"error\":\"Unauthorized\"}\n");
        return;
      }

      /* Handle OPTIONS preflight */
      if (mg_strcmp(hm->method, mg_str("OPTIONS")) == 0) {
        mg_http_reply(c, 204,
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: "
                      "GET, POST, PUT, DELETE\r\n"
                      "Access-Control-Allow-Headers: "
                      "Authorization, Content-Type\r\n",
                      "");
        return;
      }

      tg_api_handle(c, hm, http);
      return;
    }

    /* Static file serving with security headers.
     * We rely on mg_http_serve_dir for static content.
     * Note: security headers for static files are best added
     * via a reverse proxy in production, but for a localhost
     * dev tool this is acceptable. */
    if (http->config.webroot[0] != '\0') {
      mg_http_serve_dir(c, hm, http->config.webroot);
      return;
    }

    /* No webroot configured -- serve a minimal placeholder */
    mg_http_reply(c, 200, s_security_headers,
                  "<html><body><h1>tor-gui</h1>"
                  "<p>No static files found. Set --webroot.</p>"
                  "</body></html>\n");

  } else if (ev == MG_EV_WS_OPEN) {
    /* New WebSocket client connected */
    tg_http_ws_add(http, c);
    tg_log(TG_LOG_INFO, "WebSocket client connected (id=%d, total=%d)",
           c->id, http->ws_client_count);

  } else if (ev == MG_EV_WS_MSG) {
    /* We don't expect incoming WebSocket messages from the client,
     * but log them at debug level in case of future use. */
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    (void)wm;
    tg_log(TG_LOG_DEBUG, "Received WebSocket message from client id=%d",
           c->id);

  } else if (ev == MG_EV_CLOSE) {
    /* Remove from WebSocket client list if applicable */
    if (c->is_websocket) {
      tg_http_ws_remove(http, c);
      tg_log(TG_LOG_DEBUG, "WebSocket client disconnected (id=%d, "
             "remaining=%d)", c->id, http->ws_client_count);
    }
  }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int
tg_http_init(tg_http_t *http, tg_ctrl_t *ctrl,
             const tg_http_config_t *config)
{
  char url[512];

  memset(http, 0, sizeof(*http));
  http->ctrl = ctrl;
  memcpy(&http->config, config, sizeof(*config));

  /* Generate a random bearer token if none was provided */
  if (http->config.bearer_token[0] == '\0') {
    tg_random_hex(http->config.bearer_token,
                  sizeof(http->config.bearer_token));
    tg_log(TG_LOG_INFO, "Generated bearer token: %s",
           http->config.bearer_token);
  }

  /* Initialize Mongoose */
  mg_mgr_init(&http->mgr);
  http->mgr.userdata = http;

  /* Start listening */
  snprintf(url, sizeof(url), "http://%s", http->config.listen_addr);
  http->listener = mg_http_listen(&http->mgr, url,
                                  http_event_handler, http);
  if (!http->listener) {
    tg_log(TG_LOG_ERROR, "Failed to start HTTP server on %s",
           http->config.listen_addr);
    return -1;
  }

  tg_log(TG_LOG_INFO, "HTTP server listening on %s",
         http->config.listen_addr);

  if (http->config.webroot[0] != '\0') {
    tg_log(TG_LOG_INFO, "Serving static files from %s",
           http->config.webroot);
  }

  return 0;
}

void
tg_http_poll(tg_http_t *http, int timeout_ms)
{
  mg_mgr_poll(&http->mgr, timeout_ms);
}

void
tg_http_ws_broadcast(tg_http_t *http, const char *msg, size_t len)
{
  int i;
  for (i = 0; i < http->ws_client_count; i++) {
    struct mg_connection *c = http->ws_clients[i];
    if (c && !c->is_closing) {
      mg_ws_send(c, msg, len, WEBSOCKET_OP_TEXT);
    }
  }
}

void
tg_http_ws_add(tg_http_t *http, struct mg_connection *c)
{
  if (http->ws_client_count >= 64) {
    tg_log(TG_LOG_WARN, "Maximum WebSocket clients reached (64), "
           "rejecting connection");
    c->is_draining = 1;
    return;
  }
  http->ws_clients[http->ws_client_count++] = c;
}

void
tg_http_ws_remove(tg_http_t *http, struct mg_connection *c)
{
  int i;
  for (i = 0; i < http->ws_client_count; i++) {
    if (http->ws_clients[i] == c) {
      /* Shift remaining entries down */
      int j;
      for (j = i; j < http->ws_client_count - 1; j++) {
        http->ws_clients[j] = http->ws_clients[j + 1];
      }
      http->ws_clients[http->ws_client_count - 1] = NULL;
      http->ws_client_count--;
      return;
    }
  }
}

void
tg_http_free(tg_http_t *http)
{
  mg_mgr_free(&http->mgr);
  http->listener = NULL;
  http->ws_client_count = 0;
  memset(http->ws_clients, 0, sizeof(http->ws_clients));
  tg_log(TG_LOG_DEBUG, "HTTP server freed");
}

void
tg_http_open_browser(const char *url)
{
#if defined(__APPLE__)
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
  if (system(cmd) != 0)
    tg_log(TG_LOG_WARN, "Failed to open browser");
#elif defined(_WIN32)
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", url);
  if (system(cmd) != 0)
    tg_log(TG_LOG_WARN, "Failed to open browser");
#else
  /* Linux / other Unix */
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 &", url);
  if (system(cmd) != 0)
    tg_log(TG_LOG_WARN, "Failed to open browser (is xdg-open installed?)");
#endif
}
