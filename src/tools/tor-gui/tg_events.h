#ifndef TG_EVENTS_H
#define TG_EVENTS_H

#include "tg_control.h"

/* Forward declaration */
struct tg_http;

/* Bandwidth history entry */
typedef struct {
  long read_bytes;
  long written_bytes;
} tg_bw_sample_t;

/* Event state - holds cached event data */
typedef struct tg_events {
  struct tg_http *http;
  tg_ctrl_t *ctrl;

  /* Bandwidth circular buffer (5 min at 1/sec) */
  tg_bw_sample_t bw_history[300];
  int bw_head;
  int bw_count;
} tg_events_t;

/* Initialize event handler */
void tg_events_init(tg_events_t *ev, struct tg_http *http, tg_ctrl_t *ctrl);

/* Control port event callback - translates to JSON and broadcasts via
 * WebSocket */
void tg_events_on_ctrl_event(const char *event_line, void *userdata);

/* Get bandwidth history as JSON */
void tg_events_get_bw_json(tg_events_t *ev, char *buf, size_t bufsize);

/* Get recent bandwidth samples */
const tg_bw_sample_t *tg_events_get_bw_history(tg_events_t *ev, int *count);

#endif /* TG_EVENTS_H */
