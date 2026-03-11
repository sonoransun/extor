#ifndef TG_CONTROL_H
#define TG_CONTROL_H

#include "tg_util.h"

/* Maximum response line length */
#define TG_CTRL_MAX_LINE 65536

/* Control port connection state */
typedef enum {
  TG_CTRL_DISCONNECTED = 0,
  TG_CTRL_CONNECTED,
  TG_CTRL_AUTHENTICATED,
  TG_CTRL_ERROR
} tg_ctrl_state_t;

/* Async event callback */
typedef void (*tg_ctrl_event_cb)(const char *event_line, void *userdata);

/* Control port connection */
typedef struct tg_ctrl {
  tg_socket_t sock;
  tg_ctrl_state_t state;
  char host[256];
  int port;
  char *cookie_path;
  char *password;

  /* Reader thread */
  tg_thread_t reader_thread;
  volatile int running;

  /* Event queue (lock-free ring buffer) */
  char *event_queue[1024];
  volatile int eq_head;
  volatile int eq_tail;

  /* Event callback */
  tg_ctrl_event_cb event_cb;
  void *event_cb_data;

  /* Synchronous reply buffer */
  tg_buf_t reply_buf;
  tg_mutex_t reply_mutex;
  volatile int reply_ready;

  /* Auth info from PROTOCOLINFO */
  char auth_methods[256];
  char auth_cookie_file[1024];
  char tor_version[64];
} tg_ctrl_t;

/* Initialize control port client */
void tg_ctrl_init(tg_ctrl_t *ctrl);

/* Connect to control port. Returns 0 on success */
int tg_ctrl_connect(tg_ctrl_t *ctrl, const char *host, int port);

/* Authenticate. Tries cookie first, then password. Returns 0 on success */
int tg_ctrl_authenticate(tg_ctrl_t *ctrl, const char *password,
                         const char *cookie_path);

/* Send a command and wait for reply. Returns reply string (caller must free) */
char *tg_ctrl_command(tg_ctrl_t *ctrl, const char *fmt, ...);

/* Convenience wrappers */
char *tg_ctrl_getinfo(tg_ctrl_t *ctrl, const char *key);
char *tg_ctrl_getconf(tg_ctrl_t *ctrl, const char *key);
int tg_ctrl_setconf(tg_ctrl_t *ctrl, const char *key, const char *value);
int tg_ctrl_signal(tg_ctrl_t *ctrl, const char *signal_name);
int tg_ctrl_subscribe(tg_ctrl_t *ctrl, const char *events);

/* Start/stop the async reader thread */
int tg_ctrl_start_reader(tg_ctrl_t *ctrl);
void tg_ctrl_stop_reader(tg_ctrl_t *ctrl);

/* Drain event queue (called from main thread) */
void tg_ctrl_drain_events(tg_ctrl_t *ctrl);

/* Set event callback */
void tg_ctrl_set_event_cb(tg_ctrl_t *ctrl, tg_ctrl_event_cb cb, void *data);

/* Clean up */
void tg_ctrl_close(tg_ctrl_t *ctrl);

/* Get current state */
tg_ctrl_state_t tg_ctrl_get_state(tg_ctrl_t *ctrl);

#endif /* TG_CONTROL_H */
