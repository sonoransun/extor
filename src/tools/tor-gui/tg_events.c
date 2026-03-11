/* tg_events.c -- Tor control port event translation for tor-gui
 *
 * Receives raw 650-class async event lines from the control port,
 * parses them, translates to JSON, and broadcasts to all connected
 * WebSocket clients.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tg_events.h"
#include "tg_http.h"
#include "tg_json.h"

/* Maximum broadcast JSON message size */
#define TG_EV_MAX_MSG 4096

/* ---- Initialization ---- */

void
tg_events_init(tg_events_t *ev, struct tg_http *http, tg_ctrl_t *ctrl)
{
  memset(ev, 0, sizeof(*ev));
  ev->http = http;
  ev->ctrl = ctrl;
  ev->bw_head = 0;
  ev->bw_count = 0;
}

/* ---- Bandwidth history accessors ---- */

const tg_bw_sample_t *
tg_events_get_bw_history(tg_events_t *ev, int *count)
{
  if (count)
    *count = ev->bw_count;
  return ev->bw_history;
}

void
tg_events_get_bw_json(tg_events_t *ev, char *buf, size_t bufsize)
{
  tg_json_t j;
  tg_json_init(&j);
  tg_json_arr_open(&j);

  int cap = 300;
  int total = ev->bw_count;
  int head = ev->bw_head;
  for (int i = 0; i < total; i++) {
    int idx = (head - total + i + cap) % cap;
    tg_json_obj_open(&j);
    tg_json_kv_int(&j, "read", ev->bw_history[idx].read_bytes);
    tg_json_kv_int(&j, "written",
                   ev->bw_history[idx].written_bytes);
    tg_json_obj_close(&j);
  }

  tg_json_arr_close(&j);
  const char *result = tg_json_finish(&j);
  if (result) {
    size_t rlen = strlen(result);
    if (rlen >= bufsize)
      rlen = bufsize - 1;
    memcpy(buf, result, rlen);
    buf[rlen] = '\0';
  } else {
    buf[0] = '\0';
  }
  tg_json_free(&j);
}

/* ---- Broadcast helper ---- */

static void
broadcast(tg_events_t *ev, const char *json)
{
  if (ev->http)
    tg_http_ws_broadcast(ev->http, json, strlen(json));
}

/* ---- Individual event parsers ---- */

/** Parse and broadcast BW event: "650 BW read written" */
static void
handle_bw(tg_events_t *ev, const char *data)
{
  long rd = 0, wr = 0;
  if (sscanf(data, "%ld %ld", &rd, &wr) < 2)
    return;

  /* Store in circular buffer */
  ev->bw_history[ev->bw_head].read_bytes = rd;
  ev->bw_history[ev->bw_head].written_bytes = wr;
  ev->bw_head = (ev->bw_head + 1) % 300;
  if (ev->bw_count < 300)
    ev->bw_count++;

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "type", "bandwidth");
  tg_json_kv_int(&j, "read", rd);
  tg_json_kv_int(&j, "written", wr);
  tg_json_obj_close(&j);
  broadcast(ev, tg_json_finish(&j));
  tg_json_free(&j);
}

/** Parse and broadcast CIRC event:
 *  "650 CIRC id status [$fp~name,...] [PURPOSE=x]" */
static void
handle_circ(tg_events_t *ev, const char *data)
{
  char circ_id[16] = "";
  char status[32] = "";
  char path_buf[2048] = "";

  int n = sscanf(data, "%15s %31s %2047s", circ_id, status, path_buf);
  if (n < 2)
    return;

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "type", "circuit");
  tg_json_kv_str(&j, "id", circ_id);
  tg_json_kv_str(&j, "status", status);

  /* Parse path into array */
  tg_json_key(&j, "path");
  tg_json_arr_open(&j);
  if (n >= 3 && path_buf[0] == '$') {
    char *saveptr = NULL;
    char *relay = strtok_r(path_buf, ",", &saveptr);
    while (relay) {
      tg_json_str(&j, relay);
      relay = strtok_r(NULL, ",", &saveptr);
    }
  }
  tg_json_arr_close(&j);

  /* Check for PURPOSE */
  const char *pp = strstr(data, "PURPOSE=");
  if (pp) {
    char purpose[64];
    if (sscanf(pp + 8, "%63s", purpose) == 1)
      tg_json_kv_str(&j, "purpose", purpose);
  }

  tg_json_obj_close(&j);
  broadcast(ev, tg_json_finish(&j));
  tg_json_free(&j);
}

/** Parse and broadcast STREAM event:
 *  "650 STREAM id status circId target" */
static void
handle_stream(tg_events_t *ev, const char *data)
{
  char stream_id[16] = "";
  char status[32] = "";
  char circ_id[16] = "";
  char target[512] = "";

  if (sscanf(data, "%15s %31s %15s %511s",
             stream_id, status, circ_id, target) < 3)
    return;

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "type", "stream");
  tg_json_kv_str(&j, "id", stream_id);
  tg_json_kv_str(&j, "status", status);
  tg_json_kv_str(&j, "circuit", circ_id);
  tg_json_kv_str(&j, "target", target);
  tg_json_obj_close(&j);
  broadcast(ev, tg_json_finish(&j));
  tg_json_free(&j);
}

/** Parse and broadcast ORCONN event:
 *  "650 ORCONN target status" */
static void
handle_orconn(tg_events_t *ev, const char *data)
{
  char target[256] = "";
  char status[32] = "";

  if (sscanf(data, "%255s %31s", target, status) < 2)
    return;

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "type", "orconn");
  tg_json_kv_str(&j, "target", target);
  tg_json_kv_str(&j, "status", status);
  tg_json_obj_close(&j);
  broadcast(ev, tg_json_finish(&j));
  tg_json_free(&j);
}

/** Parse and broadcast STATUS_CLIENT event:
 *  "650 STATUS_CLIENT NOTICE|WARN BOOTSTRAP ..." */
static void
handle_status_client(tg_events_t *ev, const char *data)
{
  int progress = 0;
  char summary[256] = "";
  char action[32] = "";

  /* Extract action (first token) */
  sscanf(data, "%31s", action);

  /* Extract PROGRESS= if present */
  const char *pp = strstr(data, "PROGRESS=");
  if (pp)
    progress = atoi(pp + 9);

  /* Extract SUMMARY= if present (quoted string) */
  const char *sp = strstr(data, "SUMMARY=\"");
  if (sp) {
    sp += 9;
    const char *eq = strchr(sp, '"');
    if (eq) {
      size_t slen = (size_t)(eq - sp);
      if (slen >= sizeof(summary))
        slen = sizeof(summary) - 1;
      memcpy(summary, sp, slen);
      summary[slen] = '\0';
    }
  }

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "type", "bootstrap");
  tg_json_kv_str(&j, "action", action);
  tg_json_kv_int(&j, "progress", progress);
  tg_json_kv_str(&j, "summary", summary);
  tg_json_obj_close(&j);
  broadcast(ev, tg_json_finish(&j));
  tg_json_free(&j);
}

/** Parse and broadcast CONF_CHANGED event */
static void
handle_conf_changed(tg_events_t *ev, const char *data)
{
  (void)data;
  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "type", "config_changed");
  tg_json_obj_close(&j);
  broadcast(ev, tg_json_finish(&j));
  tg_json_free(&j);
}

/** Parse and broadcast PT_STATUS event:
 *  "650 PT_STATUS transport status" */
static void
handle_pt_status(tg_events_t *ev, const char *data)
{
  char transport[64] = "";
  char status[256] = "";

  sscanf(data, "%63s", transport);
  /* Rest is the status message */
  const char *rest = data;
  while (*rest && !isspace((unsigned char)*rest))
    rest++;
  while (*rest && isspace((unsigned char)*rest))
    rest++;
  if (*rest) {
    size_t rlen = strlen(rest);
    if (rlen >= sizeof(status))
      rlen = sizeof(status) - 1;
    memcpy(status, rest, rlen);
    status[rlen] = '\0';
    /* Trim trailing whitespace */
    while (rlen > 0 && isspace((unsigned char)status[rlen-1]))
      status[--rlen] = '\0';
  }

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "type", "pt_status");
  tg_json_kv_str(&j, "transport", transport);
  tg_json_kv_str(&j, "status", status);
  tg_json_obj_close(&j);
  broadcast(ev, tg_json_finish(&j));
  tg_json_free(&j);
}

/** Parse and broadcast PT_LOG event:
 *  "650 PT_LOG transport message" */
static void
handle_pt_log(tg_events_t *ev, const char *data)
{
  char transport[64] = "";
  char message[512] = "";

  sscanf(data, "%63s", transport);
  /* Rest is the log message */
  const char *rest = data;
  while (*rest && !isspace((unsigned char)*rest))
    rest++;
  while (*rest && isspace((unsigned char)*rest))
    rest++;
  if (*rest) {
    size_t rlen = strlen(rest);
    if (rlen >= sizeof(message))
      rlen = sizeof(message) - 1;
    memcpy(message, rest, rlen);
    message[rlen] = '\0';
    while (rlen > 0 && isspace((unsigned char)message[rlen-1]))
      message[--rlen] = '\0';
  }

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "type", "pt_log");
  tg_json_kv_str(&j, "transport", transport);
  tg_json_kv_str(&j, "message", message);
  tg_json_obj_close(&j);
  broadcast(ev, tg_json_finish(&j));
  tg_json_free(&j);
}

/* ============================================================
 * Main event callback
 * ============================================================ */

void
tg_events_on_ctrl_event(const char *event_line, void *userdata)
{
  tg_events_t *ev = (tg_events_t *)userdata;
  if (!ev || !event_line)
    return;

  /* Event lines start with "650 " or "650-" or "650+" */
  const char *line = event_line;

  /* Skip "650" prefix and separator */
  if (strncmp(line, "650", 3) != 0)
    return;
  line += 3;
  if (*line == ' ' || *line == '-' || *line == '+')
    line++;
  else
    return;

  /* Now line points to "EVENT_TYPE data..." */
  /* Determine event type by keyword match */

  if (strncmp(line, "BW ", 3) == 0) {
    handle_bw(ev, line + 3);
  } else if (strncmp(line, "CIRC ", 5) == 0) {
    handle_circ(ev, line + 5);
  } else if (strncmp(line, "STREAM ", 7) == 0) {
    handle_stream(ev, line + 7);
  } else if (strncmp(line, "ORCONN ", 7) == 0) {
    handle_orconn(ev, line + 7);
  } else if (strncmp(line, "STATUS_CLIENT ", 14) == 0) {
    handle_status_client(ev, line + 14);
  } else if (strncmp(line, "CONF_CHANGED", 12) == 0) {
    handle_conf_changed(ev, line + 12);
  } else if (strncmp(line, "PT_STATUS ", 10) == 0) {
    handle_pt_status(ev, line + 10);
  } else if (strncmp(line, "PT_LOG ", 7) == 0) {
    handle_pt_log(ev, line + 7);
  }
  /* Unknown event types are silently ignored */
}
