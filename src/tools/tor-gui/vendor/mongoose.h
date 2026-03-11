/* Minimal Mongoose-compatible API for tor-gui
 * For production use, replace with full Mongoose from
 * https://github.com/cesanta/mongoose
 * License: MIT */
#ifndef MONGOOSE_H
#define MONGOOSE_H

#include <stddef.h>
#include <stdint.h>

struct mg_str {
  const char *buf;
  size_t len;
};

struct mg_addr {
  uint32_t ip;
  uint16_t port;
  uint8_t is_ip6;
};

struct mg_connection {
  struct mg_connection *next;
  struct mg_mgr *mgr;
  struct mg_addr loc;
  struct mg_addr rem;
  void *fn_data;
  void *userdata;
  unsigned is_websocket : 1;
  unsigned is_closing : 1;
  unsigned is_accepted : 1;
  unsigned is_listening : 1;
  unsigned is_draining : 1;
  unsigned is_resp : 1;
  int id;
  struct {
    unsigned char *buf;
    size_t len;
    size_t size;
  } recv;
  struct {
    unsigned char *buf;
    size_t len;
    size_t size;
  } send;
  char data[32]; /* user data storage */
};

struct mg_mgr {
  struct mg_connection *conns;
  void *userdata;
  int dnstimeout;
};

struct mg_http_message {
  struct mg_str method, uri, query, proto, body;
  struct mg_str headers[30];
  int header_count;
};

struct mg_ws_message {
  struct mg_str data;
  uint8_t flags;
};

/* Events */
#define MG_EV_ERROR     0
#define MG_EV_OPEN      1
#define MG_EV_POLL      2
#define MG_EV_RESOLVE   3
#define MG_EV_CONNECT   4
#define MG_EV_ACCEPT    5
#define MG_EV_TLS_HS    6
#define MG_EV_READ      7
#define MG_EV_WRITE     8
#define MG_EV_CLOSE     9
#define MG_EV_HTTP_MSG  10
#define MG_EV_WS_OPEN   11
#define MG_EV_WS_MSG    12
#define MG_EV_WS_CTL    13

typedef void (*mg_event_handler_t)(struct mg_connection *, int ev,
                                   void *ev_data);

void mg_mgr_init(struct mg_mgr *mgr);
void mg_mgr_free(struct mg_mgr *mgr);
void mg_mgr_poll(struct mg_mgr *mgr, int timeout_ms);

struct mg_connection *mg_http_listen(struct mg_mgr *mgr, const char *url,
                                     mg_event_handler_t fn, void *fn_data);

void mg_http_reply(struct mg_connection *c, int status, const char *headers,
                   const char *fmt, ...);
void mg_http_serve_dir(struct mg_connection *c, struct mg_http_message *hm,
                       const char *root_dir);
int mg_http_match_uri(const struct mg_http_message *hm, const char *glob);
struct mg_str mg_http_get_header(const struct mg_http_message *hm,
                                 const char *name);
int mg_http_get_var(const struct mg_str *buf, const char *name, char *dst,
                    size_t dst_len);
void mg_ws_upgrade(struct mg_connection *c, struct mg_http_message *hm,
                   const char *fmt, ...);
size_t mg_ws_send(struct mg_connection *c, const void *buf, size_t len,
                  int op);
struct mg_str mg_str(const char *s);
int mg_strcmp(struct mg_str a, struct mg_str b);
int mg_match(struct mg_str str, struct mg_str pattern, struct mg_str *caps);
int mg_snprintf(char *buf, size_t len, const char *fmt, ...);

/* WebSocket opcodes */
#define WEBSOCKET_OP_TEXT    1
#define WEBSOCKET_OP_BINARY  2
#define WEBSOCKET_OP_CLOSE   8

#endif /* MONGOOSE_H */
