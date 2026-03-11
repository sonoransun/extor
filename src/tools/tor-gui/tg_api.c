/* tg_api.c -- REST API handlers for tor-gui
 *
 * Maps HTTP requests to Tor control port commands and returns JSON
 * responses. Each handler extracts parameters, calls the control port,
 * parses the reply, builds JSON, and sends the HTTP response.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef _WIN32
#include <strings.h>
#endif

#include "tg_api.h"
#include "tg_http.h"
#include "tg_json.h"
#include "tg_events.h"
#include "tg_config.h"
#include "tg_qr.h"

/* ---------------- Helpers ---------------- */

/** Send a JSON error response. */
static void
api_error(struct mg_connection *c, int status, const char *message)
{
  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "error", message);
  tg_json_obj_close(&j);
  mg_http_reply(c, status, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
}

/** Extract the tail of a URI after a prefix.  Returns pointer into
 *  hm->uri.buf or NULL. Copies at most dst_size-1 bytes into dst. */
static int
uri_tail(const struct mg_http_message *hm, const char *prefix,
         char *dst, size_t dst_size)
{
  size_t plen = strlen(prefix);
  if (hm->uri.len <= plen)
    return -1;
  if (memcmp(hm->uri.buf, prefix, plen) != 0)
    return -1;
  size_t tlen = hm->uri.len - plen;
  if (tlen >= dst_size)
    tlen = dst_size - 1;
  memcpy(dst, hm->uri.buf + plen, tlen);
  dst[tlen] = '\0';
  return 0;
}

/** Return 1 if hm->method matches meth (case-sensitive). */
static int
method_is(const struct mg_http_message *hm, const char *meth)
{
  size_t mlen = strlen(meth);
  return hm->method.len == mlen &&
         memcmp(hm->method.buf, meth, mlen) == 0;
}

/** Copy hm->body into a NUL-terminated malloc'd string. */
static char *
body_str(const struct mg_http_message *hm)
{
  char *s = (char *)malloc(hm->body.len + 1);
  if (!s)
    return NULL;
  memcpy(s, hm->body.buf, hm->body.len);
  s[hm->body.len] = '\0';
  return s;
}

/** Extract a query parameter value from hm->query into dst.
 *  Returns 0 on success, -1 on missing/error. */
static int
query_get(const struct mg_http_message *hm, const char *name,
          char *dst, size_t dst_size)
{
  return mg_http_get_var(&hm->query, name, dst, dst_size);
}

/* -------------- Handler: GET /api/status -------------- */

static void
api_get_status(struct mg_connection *c, struct tg_http *http)
{
  char *phase = tg_ctrl_getinfo(http->ctrl, "status/bootstrap-phase");
  char *version = tg_ctrl_getinfo(http->ctrl, "version");
  char *pid = tg_ctrl_getinfo(http->ctrl, "process/pid");
  char *uptime = tg_ctrl_getinfo(http->ctrl, "uptime");

  int progress = 0;
  if (phase) {
    const char *p = strstr(phase, "PROGRESS=");
    if (p)
      progress = atoi(p + 9);
  }

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "version", version ? version : "unknown");
  tg_json_kv_str(&j, "bootstrap_phase", phase ? phase : "");
  tg_json_kv_str(&j, "pid", pid ? pid : "");
  tg_json_kv_str(&j, "uptime", uptime ? uptime : "0");
  tg_json_kv_int(&j, "bootstrap_progress", progress);
  tg_json_obj_close(&j);

  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);

  free(phase);
  free(version);
  free(pid);
  free(uptime);
}

/* -------------- Handler: GET /api/bandwidth -------------- */

static void
api_get_bandwidth(struct mg_connection *c, struct tg_http *http)
{
  int count = 0;
  const tg_bw_sample_t *samples =
    tg_events_get_bw_history(http->events, &count);

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_key(&j, "samples");
  tg_json_arr_open(&j);

  /* Walk circular buffer from oldest to newest. */
  int head = http->events->bw_head;
  int total = http->events->bw_count;
  int cap = 300;
  for (int i = 0; i < total; i++) {
    int idx = (head - total + i + cap) % cap;
    tg_json_obj_open(&j);
    tg_json_kv_int(&j, "read", samples[idx].read_bytes);
    tg_json_kv_int(&j, "written", samples[idx].written_bytes);
    tg_json_obj_close(&j);
  }

  tg_json_arr_close(&j);
  tg_json_kv_int(&j, "count", count);
  tg_json_obj_close(&j);

  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
}

/* -------------- Handler: GET /api/circuits -------------- */

static void
api_get_circuits(struct mg_connection *c, struct tg_http *http)
{
  char *raw = tg_ctrl_getinfo(http->ctrl, "circuit-status");
  tg_json_t j;
  tg_json_init(&j);
  tg_json_arr_open(&j);

  if (raw && *raw) {
    /* Each line: CircID Status Path [Purpose=...] */
    char *saveptr = NULL;
    char *line = strtok_r(raw, "\n", &saveptr);
    while (line) {
      /* Skip empty lines */
      while (*line == ' ' || *line == '\r')
        line++;
      if (*line == '\0') {
        line = strtok_r(NULL, "\n", &saveptr);
        continue;
      }

      char circ_id[16] = "";
      char status[32] = "";
      char path_buf[2048] = "";
      char purpose[64] = "";

      /* Parse: ID STATUS $fp~name,$fp~name,... [PURPOSE=x] */
      int n = sscanf(line, "%15s %31s %2047s", circ_id, status, path_buf);

      /* Look for PURPOSE= */
      const char *pp = strstr(line, "PURPOSE=");
      if (pp)
        sscanf(pp + 8, "%63s", purpose);

      tg_json_obj_open(&j);
      tg_json_kv_str(&j, "id", circ_id);
      tg_json_kv_str(&j, "status", status);
      if (purpose[0])
        tg_json_kv_str(&j, "purpose", purpose);

      /* Parse path into array */
      tg_json_key(&j, "path");
      tg_json_arr_open(&j);
      if (n >= 3 && path_buf[0] == '$') {
        char *relay_save = NULL;
        char *relay = strtok_r(path_buf, ",", &relay_save);
        while (relay) {
          tg_json_str(&j, relay);
          relay = strtok_r(NULL, ",", &relay_save);
        }
      }
      tg_json_arr_close(&j);

      tg_json_obj_close(&j);
      line = strtok_r(NULL, "\n", &saveptr);
    }
  }

  tg_json_arr_close(&j);
  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(raw);
}

/* -------------- Handler: GET /api/streams -------------- */

static void
api_get_streams(struct mg_connection *c, struct tg_http *http)
{
  char *raw = tg_ctrl_getinfo(http->ctrl, "stream-status");
  tg_json_t j;
  tg_json_init(&j);
  tg_json_arr_open(&j);

  if (raw && *raw) {
    char *saveptr = NULL;
    char *line = strtok_r(raw, "\n", &saveptr);
    while (line) {
      while (*line == ' ' || *line == '\r')
        line++;
      if (*line == '\0') {
        line = strtok_r(NULL, "\n", &saveptr);
        continue;
      }

      /* StreamID Status CircID Target */
      char stream_id[16] = "";
      char status[32] = "";
      char circ_id[16] = "";
      char target[512] = "";

      sscanf(line, "%15s %31s %15s %511s",
             stream_id, status, circ_id, target);

      tg_json_obj_open(&j);
      tg_json_kv_str(&j, "id", stream_id);
      tg_json_kv_str(&j, "status", status);
      tg_json_kv_str(&j, "circuit", circ_id);
      tg_json_kv_str(&j, "target", target);
      tg_json_obj_close(&j);

      line = strtok_r(NULL, "\n", &saveptr);
    }
  }

  tg_json_arr_close(&j);
  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(raw);
}

/* -------------- Handler: GET /api/guards -------------- */

static void
api_get_guards(struct mg_connection *c, struct tg_http *http)
{
  char *raw = tg_ctrl_getinfo(http->ctrl, "entry-guards");
  tg_json_t j;
  tg_json_init(&j);
  tg_json_arr_open(&j);

  if (raw && *raw) {
    char *saveptr = NULL;
    char *line = strtok_r(raw, "\n", &saveptr);
    while (line) {
      while (*line == ' ' || *line == '\r')
        line++;
      if (*line == '\0') {
        line = strtok_r(NULL, "\n", &saveptr);
        continue;
      }

      /* name $fingerprint status [timestamp] */
      char name[128] = "";
      char fp[64] = "";
      char guard_status[32] = "";

      int n = sscanf(line, "%127s %63s %31s", name, fp, guard_status);
      if (n >= 2) {
        tg_json_obj_open(&j);
        tg_json_kv_str(&j, "name", name);
        tg_json_kv_str(&j, "fingerprint", fp);
        tg_json_kv_str(&j, "status", n >= 3 ? guard_status : "unknown");
        tg_json_obj_close(&j);
      }

      line = strtok_r(NULL, "\n", &saveptr);
    }
  }

  tg_json_arr_close(&j);
  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(raw);
}

/* -------------- Handler: GET /api/config/:key -------------- */

static void
api_get_config(struct mg_connection *c, struct tg_http *http,
               const char *key)
{
  char *value = tg_ctrl_getconf(http->ctrl, key);
  if (!value) {
    api_error(c, 500, "Failed to get config value");
    return;
  }

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "key", key);
  tg_json_kv_str(&j, "value", value);
  tg_json_obj_close(&j);

  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(value);
}

/* -------------- Handler: PUT /api/config/:key -------------- */

static void
api_put_config(struct mg_connection *c, struct tg_http *http,
               const char *key, const struct mg_http_message *hm)
{
  char *val = body_str(hm);
  if (!val) {
    api_error(c, 400, "Missing request body");
    return;
  }

  int rc = tg_ctrl_setconf(http->ctrl, key, val);
  free(val);

  if (rc != 0) {
    api_error(c, 500, "Failed to set config value");
    return;
  }

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "status", "ok");
  tg_json_obj_close(&j);

  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
}

/* -------------- Handler: POST /api/config/load -------------- */

static void
api_post_config_load(struct mg_connection *c, struct tg_http *http)
{
  char *reply = tg_ctrl_command(http->ctrl, "LOADCONF\r\n");
  if (!reply) {
    api_error(c, 500, "LOADCONF failed");
    return;
  }

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "status", "ok");
  tg_json_kv_str(&j, "reply", reply);
  tg_json_obj_close(&j);

  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(reply);
}

/* -------------- Handler: POST /api/config/save -------------- */

static void
api_post_config_save(struct mg_connection *c, struct tg_http *http)
{
  char *reply = tg_ctrl_command(http->ctrl, "SAVECONF\r\n");
  if (!reply) {
    api_error(c, 500, "SAVECONF failed");
    return;
  }

  int ok = (strstr(reply, "250") != NULL);

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "status", ok ? "ok" : "error");
  tg_json_kv_str(&j, "reply", reply);
  tg_json_obj_close(&j);

  mg_http_reply(c, ok ? 200 : 500,
                "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(reply);
}

/* -------------- Handler: POST /api/config/reload -------------- */

static void
api_post_config_reload(struct mg_connection *c, struct tg_http *http)
{
  int rc = tg_ctrl_signal(http->ctrl, "RELOAD");

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "status", rc == 0 ? "ok" : "error");
  tg_json_obj_close(&j);

  mg_http_reply(c, rc == 0 ? 200 : 500,
                "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
}

/* -------------- Handler: GET /api/bridges -------------- */

static void
api_get_bridges(struct mg_connection *c, struct tg_http *http)
{
  char *raw = tg_ctrl_getconf(http->ctrl, "Bridge");

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_key(&j, "bridges");
  tg_json_arr_open(&j);

  if (raw && *raw) {
    /* Response contains one or more lines of Bridge=value */
    char *saveptr = NULL;
    char *line = strtok_r(raw, "\n", &saveptr);
    while (line) {
      /* Skip leading whitespace */
      while (*line == ' ' || *line == '\r')
        line++;
      if (*line) {
        /* Strip "Bridge=" prefix if present */
        const char *val = line;
        if (strncmp(val, "Bridge=", 7) == 0)
          val += 7;

        tg_bridge_t br;
        if (tg_bridge_parse(val, &br) == 0) {
          tg_json_obj_open(&j);
          tg_json_kv_str(&j, "transport", br.transport);
          tg_json_kv_str(&j, "address", br.address);
          tg_json_kv_str(&j, "fingerprint", br.fingerprint);
          tg_json_kv_str(&j, "args", br.args);
          tg_json_kv_str(&j, "line", val);
          tg_json_obj_close(&j);
        } else {
          /* Unparseable, include raw */
          tg_json_obj_open(&j);
          tg_json_kv_str(&j, "line", val);
          tg_json_obj_close(&j);
        }
      }
      line = strtok_r(NULL, "\n", &saveptr);
    }
  }

  tg_json_arr_close(&j);
  tg_json_obj_close(&j);

  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(raw);
}

/* -------------- Handler: PUT /api/bridges -------------- */

static void
api_put_bridges(struct mg_connection *c, struct tg_http *http,
                const struct mg_http_message *hm)
{
  /*
   * Expect JSON body with array of bridge line strings:
   *   ["obfs4 1.2.3.4:443 FP cert=... iat-mode=0", ...]
   * We parse them, validate, and issue SETCONF.
   */
  char *raw = body_str(hm);
  if (!raw) {
    api_error(c, 400, "Missing request body");
    return;
  }

  /* Simple JSON array-of-strings parser: find each quoted string. */
  tg_bridge_t bridges[64];
  int count = 0;

  const char *p = raw;
  while (*p && count < 64) {
    /* Find next quoted string */
    const char *q1 = strchr(p, '"');
    if (!q1)
      break;
    q1++;
    const char *q2 = strchr(q1, '"');
    if (!q2)
      break;

    size_t slen = (size_t)(q2 - q1);
    char line_buf[1024];
    if (slen >= sizeof(line_buf))
      slen = sizeof(line_buf) - 1;
    memcpy(line_buf, q1, slen);
    line_buf[slen] = '\0';

    if (tg_bridge_parse(line_buf, &bridges[count]) == 0) {
      const char *err = tg_bridge_validate(&bridges[count]);
      if (err) {
        api_error(c, 400, err);
        free(raw);
        return;
      }
      count++;
    }
    p = q2 + 1;
  }

  char *cmd = tg_bridge_format_setconf(bridges, count);
  if (!cmd) {
    api_error(c, 500, "Failed to format SETCONF command");
    free(raw);
    return;
  }

  char *reply = tg_ctrl_command(http->ctrl, "%s\r\n", cmd);
  free(cmd);
  free(raw);

  int ok = reply && strstr(reply, "250") != NULL;

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "status", ok ? "ok" : "error");
  tg_json_kv_int(&j, "count", count);
  if (reply)
    tg_json_kv_str(&j, "reply", reply);
  tg_json_obj_close(&j);

  mg_http_reply(c, ok ? 200 : 500,
                "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(reply);
}

/* -------------- Handler: GET /api/transports -------------- */

static void
api_get_transports(struct mg_connection *c, struct tg_http *http)
{
  char *raw = tg_ctrl_getinfo(http->ctrl, "transport/metadata");
  char *conf = tg_ctrl_getconf(http->ctrl, "ClientTransportPlugin");

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);

  tg_json_key(&j, "transports");
  tg_json_arr_open(&j);

  if (conf && *conf) {
    char *saveptr = NULL;
    char *line = strtok_r(conf, "\n", &saveptr);
    while (line) {
      while (*line == ' ' || *line == '\r')
        line++;
      /* Strip prefix */
      const char *val = line;
      if (strncmp(val, "ClientTransportPlugin=", 22) == 0)
        val += 22;
      if (*val) {
        tg_json_obj_open(&j);
        tg_json_kv_str(&j, "line", val);
        tg_json_obj_close(&j);
      }
      line = strtok_r(NULL, "\n", &saveptr);
    }
  }

  tg_json_arr_close(&j);

  if (raw)
    tg_json_kv_str(&j, "metadata", raw);

  tg_json_obj_close(&j);

  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(raw);
  free(conf);
}

/* -------------- Handler: PUT /api/transports -------------- */

static void
api_put_transports(struct mg_connection *c, struct tg_http *http,
                   const struct mg_http_message *hm)
{
  char *val = body_str(hm);
  if (!val) {
    api_error(c, 400, "Missing request body");
    return;
  }

  int rc = tg_ctrl_setconf(http->ctrl, "ClientTransportPlugin", val);
  free(val);

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "status", rc == 0 ? "ok" : "error");
  tg_json_obj_close(&j);

  mg_http_reply(c, rc == 0 ? 200 : 500,
                "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
}

/* -------------- Handler: POST /api/onion -------------- */

static void
api_post_onion(struct mg_connection *c, struct tg_http *http,
               const struct mg_http_message *hm)
{
  char *raw = body_str(hm);
  if (!raw) {
    api_error(c, 400, "Missing request body");
    return;
  }

  /*
   * Expect JSON: {"port": "80,127.0.0.1:8080", "key": "optional"}
   * Simple extraction: find "port":"value" and optionally "key":"value"
   */
  char port_val[256] = "";
  char key_val[1024] = "";

  /* Extract "port" */
  const char *pport = strstr(raw, "\"port\"");
  if (pport) {
    const char *colon = strchr(pport + 6, ':');
    if (colon) {
      const char *q1 = strchr(colon, '"');
      if (q1) {
        q1++;
        const char *q2 = strchr(q1, '"');
        if (q2) {
          size_t len = (size_t)(q2 - q1);
          if (len >= sizeof(port_val))
            len = sizeof(port_val) - 1;
          memcpy(port_val, q1, len);
          port_val[len] = '\0';
        }
      }
    }
  }

  /* Extract "key" */
  const char *pkey = strstr(raw, "\"key\"");
  if (pkey) {
    const char *colon = strchr(pkey + 5, ':');
    if (colon) {
      const char *q1 = strchr(colon, '"');
      if (q1) {
        q1++;
        const char *q2 = strchr(q1, '"');
        if (q2) {
          size_t len = (size_t)(q2 - q1);
          if (len >= sizeof(key_val))
            len = sizeof(key_val) - 1;
          memcpy(key_val, q1, len);
          key_val[len] = '\0';
        }
      }
    }
  }

  free(raw);

  if (port_val[0] == '\0') {
    api_error(c, 400, "Missing 'port' field");
    return;
  }

  /* Build ADD_ONION command */
  char cmd[2048];
  if (key_val[0]) {
    snprintf(cmd, sizeof(cmd),
             "ADD_ONION %s Port=%s", key_val, port_val);
  } else {
    snprintf(cmd, sizeof(cmd),
             "ADD_ONION NEW:ED25519-V3 Port=%s", port_val);
  }

  char *reply = tg_ctrl_command(http->ctrl, "%s\r\n", cmd);
  if (!reply) {
    api_error(c, 500, "ADD_ONION failed");
    return;
  }

  /* Parse ServiceID from response */
  char service_id[128] = "";
  char private_key[512] = "";

  char *saveptr = NULL;
  char *line = strtok_r(reply, "\n", &saveptr);
  while (line) {
    if (strncmp(line, "250-ServiceID=", 14) == 0) {
      char *val_start = line + 14;
      /* Strip trailing \r */
      size_t vlen = strlen(val_start);
      while (vlen > 0 && (val_start[vlen-1] == '\r' ||
                          val_start[vlen-1] == '\n'))
        vlen--;
      if (vlen >= sizeof(service_id))
        vlen = sizeof(service_id) - 1;
      memcpy(service_id, val_start, vlen);
      service_id[vlen] = '\0';
    } else if (strncmp(line, "250-PrivateKey=", 15) == 0) {
      char *val_start = line + 15;
      size_t vlen = strlen(val_start);
      while (vlen > 0 && (val_start[vlen-1] == '\r' ||
                          val_start[vlen-1] == '\n'))
        vlen--;
      if (vlen >= sizeof(private_key))
        vlen = sizeof(private_key) - 1;
      memcpy(private_key, val_start, vlen);
      private_key[vlen] = '\0';
    }
    line = strtok_r(NULL, "\n", &saveptr);
  }

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  if (service_id[0]) {
    tg_json_kv_str(&j, "status", "ok");
    tg_json_kv_str(&j, "service_id", service_id);
    /* Build full .onion address */
    char onion_addr[256];
    snprintf(onion_addr, sizeof(onion_addr), "%s.onion", service_id);
    tg_json_kv_str(&j, "onion_address", onion_addr);
    if (private_key[0])
      tg_json_kv_str(&j, "private_key", private_key);
  } else {
    tg_json_kv_str(&j, "status", "error");
    tg_json_kv_str(&j, "reply", reply);
  }
  tg_json_obj_close(&j);

  mg_http_reply(c, service_id[0] ? 200 : 500,
                "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(reply);
}

/* -------------- Handler: DELETE /api/onion/:id -------------- */

static void
api_delete_onion(struct mg_connection *c, struct tg_http *http,
                 const char *service_id)
{
  if (!service_id || !*service_id) {
    api_error(c, 400, "Missing service ID");
    return;
  }

  char *reply = tg_ctrl_command(http->ctrl,
                                "DEL_ONION %s\r\n", service_id);
  int ok = reply && strstr(reply, "250") != NULL;

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "status", ok ? "ok" : "error");
  if (reply)
    tg_json_kv_str(&j, "reply", reply);
  tg_json_obj_close(&j);

  mg_http_reply(c, ok ? 200 : 500,
                "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(reply);
}

/* -------------- Handler: POST /api/signal/:name -------------- */

static void
api_post_signal(struct mg_connection *c, struct tg_http *http,
                const char *signal_name)
{
  /* Validate signal name */
  static const char *valid_signals[] = {
    "RELOAD", "SHUTDOWN", "DUMP", "DEBUG", "HALT",
    "NEWNYM", "CLEARDNSCACHE", "HEARTBEAT", NULL
  };

  int valid = 0;
  for (int i = 0; valid_signals[i]; i++) {
    if (strcasecmp(signal_name, valid_signals[i]) == 0) {
      valid = 1;
      break;
    }
  }

  if (!valid) {
    api_error(c, 400, "Invalid signal name");
    return;
  }

  int rc = tg_ctrl_signal(http->ctrl, signal_name);

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "status", rc == 0 ? "ok" : "error");
  tg_json_kv_str(&j, "signal", signal_name);
  tg_json_obj_close(&j);

  mg_http_reply(c, rc == 0 ? 200 : 500,
                "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
}

/* -------------- Handler: GET /api/qr -------------- */

static void
api_get_qr(struct mg_connection *c, const struct mg_http_message *hm)
{
  char data[4096];
  if (query_get(hm, "data", data, sizeof(data)) < 0) {
    api_error(c, 400, "Missing 'data' query parameter");
    return;
  }

  size_t png_len = 0;
  uint8_t *png = tg_qr_generate_png(data, &png_len, 8, 4);
  if (!png) {
    api_error(c, 500, "QR generation failed");
    return;
  }

  /* Send binary PNG response. Build headers manually. */
  char headers[256];
  snprintf(headers, sizeof(headers),
           "Content-Type: image/png\r\n"
           "Content-Length: %lu\r\n",
           (unsigned long)png_len);

  mg_http_reply(c, 200, headers, "");

  /* Append raw PNG data to the connection's send buffer.
   * Since mg_http_reply already wrote headers, we append the body. */
  /* Use mg_ws_send with BINARY type would be wrong for HTTP.
   * Instead, directly copy to send buffer. */
  if (c->send.size < c->send.len + png_len) {
    size_t new_size = c->send.len + png_len + 256;
    unsigned char *nb =
      (unsigned char *)realloc(c->send.buf, new_size);
    if (nb) {
      c->send.buf = nb;
      c->send.size = new_size;
    }
  }
  if (c->send.size >= c->send.len + png_len) {
    memcpy(c->send.buf + c->send.len, png, png_len);
    c->send.len += png_len;
  }

  free(png);
}

/* -------------- Handler: GET /api/geoip/:ip -------------- */

static void
api_get_geoip(struct mg_connection *c, struct tg_http *http,
              const char *ip_addr)
{
  if (!ip_addr || !*ip_addr) {
    api_error(c, 400, "Missing IP address");
    return;
  }

  char key[300];
  snprintf(key, sizeof(key), "ip-to-country/%s", ip_addr);
  char *country = tg_ctrl_getinfo(http->ctrl, key);

  tg_json_t j;
  tg_json_init(&j);
  tg_json_obj_open(&j);
  tg_json_kv_str(&j, "ip", ip_addr);
  tg_json_kv_str(&j, "country", country ? country : "??");
  tg_json_obj_close(&j);

  mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                "%s", tg_json_finish(&j));
  tg_json_free(&j);
  free(country);
}

/* ============================================================
 * Main API dispatcher
 * ============================================================ */

int
tg_api_handle(struct mg_connection *c, struct mg_http_message *hm,
              struct tg_http *http)
{
  /* Quick prefix check */
  if (hm->uri.len < 4 ||
      memcmp(hm->uri.buf, "/api", 4) != 0)
    return 0;

  /* --- Status --- */
  if (mg_http_match_uri(hm, "/api/status") && method_is(hm, "GET")) {
    api_get_status(c, http);
    return 1;
  }

  /* --- Bandwidth --- */
  if (mg_http_match_uri(hm, "/api/bandwidth") && method_is(hm, "GET")) {
    api_get_bandwidth(c, http);
    return 1;
  }

  /* --- Circuits --- */
  if (mg_http_match_uri(hm, "/api/circuits") && method_is(hm, "GET")) {
    api_get_circuits(c, http);
    return 1;
  }

  /* --- Streams --- */
  if (mg_http_match_uri(hm, "/api/streams") && method_is(hm, "GET")) {
    api_get_streams(c, http);
    return 1;
  }

  /* --- Guards --- */
  if (mg_http_match_uri(hm, "/api/guards") && method_is(hm, "GET")) {
    api_get_guards(c, http);
    return 1;
  }

  /* --- Config load/save/reload (check before wildcard) --- */
  if (mg_http_match_uri(hm, "/api/config/load") &&
      method_is(hm, "POST")) {
    api_post_config_load(c, http);
    return 1;
  }
  if (mg_http_match_uri(hm, "/api/config/save") &&
      method_is(hm, "POST")) {
    api_post_config_save(c, http);
    return 1;
  }
  if (mg_http_match_uri(hm, "/api/config/reload") &&
      method_is(hm, "POST")) {
    api_post_config_reload(c, http);
    return 1;
  }

  /* --- Config get/put wildcard --- */
  {
    char key[256];
    if (uri_tail(hm, "/api/config/", key, sizeof(key)) == 0 &&
        key[0] != '\0') {
      if (method_is(hm, "GET")) {
        api_get_config(c, http, key);
        return 1;
      }
      if (method_is(hm, "PUT")) {
        api_put_config(c, http, key, hm);
        return 1;
      }
    }
  }

  /* --- Bridges --- */
  if (mg_http_match_uri(hm, "/api/bridges")) {
    if (method_is(hm, "GET")) {
      api_get_bridges(c, http);
      return 1;
    }
    if (method_is(hm, "PUT")) {
      api_put_bridges(c, http, hm);
      return 1;
    }
  }

  /* --- Transports --- */
  if (mg_http_match_uri(hm, "/api/transports")) {
    if (method_is(hm, "GET")) {
      api_get_transports(c, http);
      return 1;
    }
    if (method_is(hm, "PUT")) {
      api_put_transports(c, http, hm);
      return 1;
    }
  }

  /* --- Onion services --- */
  if (mg_http_match_uri(hm, "/api/onion") && method_is(hm, "POST")) {
    api_post_onion(c, http, hm);
    return 1;
  }
  {
    char sid[256];
    if (uri_tail(hm, "/api/onion/", sid, sizeof(sid)) == 0 &&
        sid[0] != '\0' && method_is(hm, "DELETE")) {
      api_delete_onion(c, http, sid);
      return 1;
    }
  }

  /* --- Signal --- */
  {
    char sig[64];
    if (uri_tail(hm, "/api/signal/", sig, sizeof(sig)) == 0 &&
        sig[0] != '\0' && method_is(hm, "POST")) {
      api_post_signal(c, http, sig);
      return 1;
    }
  }

  /* --- QR code --- */
  if (mg_http_match_uri(hm, "/api/qr") && method_is(hm, "GET")) {
    api_get_qr(c, hm);
    return 1;
  }

  /* --- GeoIP --- */
  {
    char ip[128];
    if (uri_tail(hm, "/api/geoip/", ip, sizeof(ip)) == 0 &&
        ip[0] != '\0' && method_is(hm, "GET")) {
      api_get_geoip(c, http, ip);
      return 1;
    }
  }

  /* Not an API route we recognize */
  return 0;
}
