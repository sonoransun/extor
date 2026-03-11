/* tg_control.c -- Tor control port client for tor-gui.
 *
 * Implements the Tor control protocol (spec: control-spec.txt) over a
 * TCP socket.  A dedicated reader thread receives asynchronous 650
 * event lines and pushes them into a lock-free ring buffer that the
 * main thread drains each iteration.  Synchronous command/reply pairs
 * use a mutex-protected reply buffer with a ready flag.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

#ifndef _WIN32
#include <strings.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include "tg_control.h"
#include "tg_util.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Hex-encode `len` raw bytes from `src` into `dst`.
 *  `dst` must be at least 2*len + 1 bytes. */
static void
hex_encode(const unsigned char *src, size_t len, char *dst)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t i;
  for (i = 0; i < len; i++) {
    dst[i * 2]     = hex[(src[i] >> 4) & 0x0f];
    dst[i * 2 + 1] = hex[src[i] & 0x0f];
  }
  dst[len * 2] = '\0';
}

/** Send exactly `len` bytes over `sock`.  Returns 0 on success, -1
 *  on error. */
static int
sock_send_all(tg_socket_t sock, const char *buf, size_t len)
{
  size_t sent = 0;
  while (sent < len) {
    int n;
#ifdef _WIN32
    n = send(sock, buf + sent, (int)(len - sent), 0);
#else
    n = (int)send(sock, buf + sent, len - sent, 0);
#endif
    if (n <= 0)
      return -1;
    sent += (size_t)n;
  }
  return 0;
}

/** Read a single line (up to \r\n) from the socket into `line`.
 *  Returns the line length (excluding the terminator), or -1 on error.
 *  `line` must be at least TG_CTRL_MAX_LINE bytes. */
static int
sock_read_line(tg_socket_t sock, char *line, size_t maxlen)
{
  size_t pos = 0;
  while (pos + 1 < maxlen) {
    char ch;
    int n;
#ifdef _WIN32
    n = recv(sock, &ch, 1, 0);
#else
    n = (int)recv(sock, &ch, 1, 0);
#endif
    if (n <= 0)
      return -1;  /* connection closed or error */
    if (ch == '\n') {
      /* Strip trailing \r if present */
      if (pos > 0 && line[pos - 1] == '\r')
        pos--;
      line[pos] = '\0';
      return (int)pos;
    }
    line[pos++] = ch;
  }
  line[pos] = '\0';
  return (int)pos;
}

/** Read the 32-byte Tor auth cookie from `path`.
 *  Returns 0 on success with `cookie_out` filled, -1 on error.
 *  Uses O_NOFOLLOW on non-Windows to prevent symlink attacks. */
static int
read_cookie_file(const char *path, unsigned char cookie_out[32])
{
  int fd;
  int total = 0;

#ifdef _WIN32
  fd = _open(path, _O_RDONLY | _O_BINARY);
#else
#ifdef O_NOFOLLOW
  fd = open(path, O_RDONLY | O_NOFOLLOW);
#else
  fd = open(path, O_RDONLY);
#endif
#endif
  if (fd < 0) {
    tg_log(TG_LOG_ERROR, "Cannot open cookie file '%s': %s",
           path, strerror(errno));
    return -1;
  }

  while (total < 32) {
    int n;
#ifdef _WIN32
    n = _read(fd, cookie_out + total, 32 - total);
#else
    n = (int)read(fd, cookie_out + total, (size_t)(32 - total));
#endif
    if (n <= 0) {
      tg_log(TG_LOG_ERROR, "Short read from cookie file '%s' (%d bytes)",
             path, total);
#ifdef _WIN32
      _close(fd);
#else
      close(fd);
#endif
      return -1;
    }
    total += n;
  }

#ifdef _WIN32
  _close(fd);
#else
  close(fd);
#endif
  return 0;
}

/** Parse a PROTOCOLINFO response.  Extracts AUTH METHODS, COOKIEFILE,
 *  and Tor version into the ctrl struct fields. */
static void
parse_protocolinfo(tg_ctrl_t *ctrl, const char *response)
{
  const char *p;

  /* AUTH METHODS */
  p = strstr(response, "AUTH METHODS=");
  if (p) {
    const char *start;
    size_t len;
    p += strlen("AUTH METHODS=");
    start = p;
    while (*p && *p != ' ' && *p != '\n' && *p != '\r')
      p++;
    len = (size_t)(p - start);
    if (len >= sizeof(ctrl->auth_methods))
      len = sizeof(ctrl->auth_methods) - 1;
    memcpy(ctrl->auth_methods, start, len);
    ctrl->auth_methods[len] = '\0';
  }

  /* COOKIEFILE */
  p = strstr(response, "COOKIEFILE=\"");
  if (p) {
    const char *start;
    size_t len;
    p += strlen("COOKIEFILE=\"");
    start = p;
    while (*p && *p != '"')
      p++;
    len = (size_t)(p - start);
    if (len >= sizeof(ctrl->auth_cookie_file))
      len = sizeof(ctrl->auth_cookie_file) - 1;
    memcpy(ctrl->auth_cookie_file, start, len);
    ctrl->auth_cookie_file[len] = '\0';
  }

  /* Tor version */
  p = strstr(response, "Tor=");
  if (!p)
    p = strstr(response, "VERSION Tor=");
  if (p) {
    const char *start;
    size_t len;
    p = strstr(p, "Tor=");
    if (p) {
      p += 4;
      if (*p == '"') p++;
      start = p;
      while (*p && *p != '"' && *p != '\n' && *p != '\r' && *p != ' ')
        p++;
      len = (size_t)(p - start);
      if (len >= sizeof(ctrl->tor_version))
        len = sizeof(ctrl->tor_version) - 1;
      memcpy(ctrl->tor_version, start, len);
      ctrl->tor_version[len] = '\0';
    }
  }
}

/** Return 1 if `response` starts with "250", 0 otherwise. */
static int
reply_is_ok(const char *response)
{
  return response && response[0] == '2' && response[1] == '5' &&
         response[2] == '0';
}

/* ------------------------------------------------------------------ */
/* Event ring buffer helpers                                          */
/* ------------------------------------------------------------------ */

#define EQ_SIZE 1024
#define EQ_MASK (EQ_SIZE - 1)

static void
eq_push(tg_ctrl_t *ctrl, const char *line)
{
  int next = (ctrl->eq_head + 1) & EQ_MASK;
  if (next == ctrl->eq_tail) {
    /* Ring buffer full -- drop oldest */
    free(ctrl->event_queue[ctrl->eq_tail]);
    ctrl->event_queue[ctrl->eq_tail] = NULL;
    ctrl->eq_tail = (ctrl->eq_tail + 1) & EQ_MASK;
  }
  ctrl->event_queue[ctrl->eq_head] = tg_strdup(line);
  ctrl->eq_head = next;
}

static char *
eq_pop(tg_ctrl_t *ctrl)
{
  char *line;
  if (ctrl->eq_tail == ctrl->eq_head)
    return NULL;
  line = ctrl->event_queue[ctrl->eq_tail];
  ctrl->event_queue[ctrl->eq_tail] = NULL;
  ctrl->eq_tail = (ctrl->eq_tail + 1) & EQ_MASK;
  return line;
}

/* ------------------------------------------------------------------ */
/* Reader thread                                                      */
/* ------------------------------------------------------------------ */

/** Background thread that reads lines from the control port socket.
 *  - Lines starting with "650" are async events and go to the ring buffer.
 *  - All other lines are synchronous replies and go to reply_buf. */
static TG_THREAD_FUNC(reader_thread_fn, arg)
{
  tg_ctrl_t *ctrl = (tg_ctrl_t *)arg;
  char line[TG_CTRL_MAX_LINE];

  while (ctrl->running) {
    int len = sock_read_line(ctrl->sock, line, sizeof(line));
    if (len < 0) {
      if (ctrl->running) {
        tg_log(TG_LOG_ERROR, "Control port read error, disconnecting");
        ctrl->state = TG_CTRL_ERROR;
      }
      break;
    }
    if (len == 0)
      continue;

    /* Is this an async event? (650 status code) */
    if (len >= 4 && line[0] == '6' && line[1] == '5' && line[2] == '0' &&
        (line[3] == ' ' || line[3] == '-' || line[3] == '+')) {
      eq_push(ctrl, line);

      /* If this is a multi-line event (650-), keep reading until 650 <sp> */
      if (line[3] == '-' || line[3] == '+') {
        int is_data = (line[3] == '+');
        for (;;) {
          int slen = sock_read_line(ctrl->sock, line, sizeof(line));
          if (slen < 0) {
            ctrl->state = TG_CTRL_ERROR;
            goto reader_done;
          }
          eq_push(ctrl, line);
          if (is_data) {
            /* Data block ends with "." on a line by itself */
            if (slen == 1 && line[0] == '.')
              is_data = 0;
            /* Then expect a final 650 SP line */
            continue;
          }
          /* Multi-line ends when we see 650 SP */
          if (slen >= 4 && line[0] == '6' && line[1] == '5' &&
              line[2] == '0' && line[3] == ' ')
            break;
          /* Also break if line doesn't start with 650 at all
           * (malformed but let's not loop forever) */
          if (slen < 3 || line[0] != '6' || line[1] != '5' ||
              line[2] != '0')
            break;
        }
      }
    } else {
      /* Synchronous reply -- append to reply_buf */
      tg_mutex_lock(&ctrl->reply_mutex);
      tg_buf_append(&ctrl->reply_buf, line, (size_t)len);
      tg_buf_append(&ctrl->reply_buf, "\n", 1);

      /* If this is a final reply line (code + space), signal ready.
       * Also handle mid-reply continuations (code + '-') and data
       * blocks (code + '+').  We only set reply_ready when we see
       * the final line. */
      if (len >= 4 && line[3] == ' ') {
        ctrl->reply_ready = 1;
      } else if (len >= 4 && line[3] == '+') {
        /* Data reply -- keep reading until ".\n" then final line */
        for (;;) {
          int dlen = sock_read_line(ctrl->sock, line, sizeof(line));
          if (dlen < 0) {
            ctrl->state = TG_CTRL_ERROR;
            tg_mutex_unlock(&ctrl->reply_mutex);
            goto reader_done;
          }
          tg_buf_append(&ctrl->reply_buf, line, (size_t)dlen);
          tg_buf_append(&ctrl->reply_buf, "\n", 1);
          if (dlen == 1 && line[0] == '.')
            break;
        }
        /* After data block, there may be a final reply line */
      }
      tg_mutex_unlock(&ctrl->reply_mutex);
    }
  }

reader_done:
  return TG_THREAD_RETURN;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void
tg_ctrl_init(tg_ctrl_t *ctrl)
{
  memset(ctrl, 0, sizeof(*ctrl));
  ctrl->sock = TG_INVALID_SOCKET;
  ctrl->state = TG_CTRL_DISCONNECTED;
  ctrl->running = 0;
  ctrl->eq_head = 0;
  ctrl->eq_tail = 0;
  ctrl->reply_ready = 0;
  tg_buf_init(&ctrl->reply_buf);
  tg_mutex_init(&ctrl->reply_mutex);
}

int
tg_ctrl_connect(tg_ctrl_t *ctrl, const char *host, int port)
{
  struct sockaddr_in addr;
  tg_socket_t sock;

  if (!host || port <= 0 || port > 65535) {
    tg_log(TG_LOG_ERROR, "Invalid control port address %s:%d",
           host ? host : "(null)", port);
    return -1;
  }

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == TG_INVALID_SOCKET) {
    tg_log(TG_LOG_ERROR, "Failed to create socket: %s", strerror(errno));
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);

  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    /* Try resolving as hostname */
    struct hostent *he = gethostbyname(host);
    if (!he) {
      tg_log(TG_LOG_ERROR, "Cannot resolve host '%s'", host);
#ifdef _WIN32
      closesocket(sock);
#else
      close(sock);
#endif
      return -1;
    }
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
  }

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    tg_log(TG_LOG_ERROR, "Cannot connect to %s:%d: %s",
           host, port, strerror(errno));
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return -1;
  }

  ctrl->sock = sock;
  ctrl->state = TG_CTRL_CONNECTED;
  tg_strlcpy(ctrl->host, host, sizeof(ctrl->host));
  ctrl->port = port;

  tg_log(TG_LOG_INFO, "Connected to control port %s:%d", host, port);
  return 0;
}

int
tg_ctrl_authenticate(tg_ctrl_t *ctrl, const char *password,
                     const char *cookie_path)
{
  char *reply;
  const char *effective_cookie_path;
  int rc = -1;

  if (ctrl->state != TG_CTRL_CONNECTED) {
    tg_log(TG_LOG_ERROR, "Cannot authenticate: not connected");
    return -1;
  }

  /* Step 1: Send PROTOCOLINFO to discover auth methods */
  reply = tg_ctrl_command(ctrl, "PROTOCOLINFO 1");
  if (!reply) {
    tg_log(TG_LOG_ERROR, "No response to PROTOCOLINFO");
    return -1;
  }

  parse_protocolinfo(ctrl, reply);
  free(reply);

  tg_log(TG_LOG_DEBUG, "PROTOCOLINFO: methods=%s cookiefile='%s' tor=%s",
         ctrl->auth_methods, ctrl->auth_cookie_file, ctrl->tor_version);

  /* Step 2: Try cookie authentication first */
  effective_cookie_path = cookie_path;
  if (!effective_cookie_path && ctrl->auth_cookie_file[0] != '\0')
    effective_cookie_path = ctrl->auth_cookie_file;

  if (effective_cookie_path &&
      (strstr(ctrl->auth_methods, "COOKIE") ||
       strstr(ctrl->auth_methods, "SAFECOOKIE"))) {
    unsigned char cookie[32];
    char cookie_hex[65];

    if (read_cookie_file(effective_cookie_path, cookie) == 0) {
      hex_encode(cookie, 32, cookie_hex);
      reply = tg_ctrl_command(ctrl, "AUTHENTICATE %s", cookie_hex);
      if (reply && reply_is_ok(reply)) {
        free(reply);
        ctrl->state = TG_CTRL_AUTHENTICATED;
        tg_log(TG_LOG_INFO, "Authenticated via cookie");
        return 0;
      }
      if (reply) {
        tg_log(TG_LOG_WARN, "Cookie auth failed: %s", reply);
        free(reply);
      }
    }
  }

  /* Step 3: Try password authentication */
  if (password && strstr(ctrl->auth_methods, "HASHEDPASSWORD")) {
    reply = tg_ctrl_command(ctrl, "AUTHENTICATE \"%s\"", password);
    if (reply && reply_is_ok(reply)) {
      free(reply);
      ctrl->state = TG_CTRL_AUTHENTICATED;
      tg_log(TG_LOG_INFO, "Authenticated via password");
      return 0;
    }
    if (reply) {
      tg_log(TG_LOG_WARN, "Password auth failed: %s", reply);
      free(reply);
    }
  }

  /* Step 4: Try NULL authentication (for ControlPort with no auth) */
  if (strstr(ctrl->auth_methods, "NULL") ||
      strlen(ctrl->auth_methods) == 0) {
    reply = tg_ctrl_command(ctrl, "AUTHENTICATE");
    if (reply && reply_is_ok(reply)) {
      free(reply);
      ctrl->state = TG_CTRL_AUTHENTICATED;
      tg_log(TG_LOG_INFO, "Authenticated (no auth required)");
      return 0;
    }
    if (reply) {
      tg_log(TG_LOG_WARN, "NULL auth failed: %s", reply);
      free(reply);
    }
  }

  tg_log(TG_LOG_ERROR, "All authentication methods failed "
         "(methods=%s)", ctrl->auth_methods);
  ctrl->state = TG_CTRL_ERROR;
  (void)rc;
  return -1;
}

char *
tg_ctrl_command(tg_ctrl_t *ctrl, const char *fmt, ...)
{
  char cmd[TG_CTRL_MAX_LINE];
  va_list ap;
  int len;
  char *result;
  int timeout_ms = 10000;  /* 10 second timeout */
  int elapsed = 0;

  if (ctrl->state == TG_CTRL_DISCONNECTED ||
      ctrl->state == TG_CTRL_ERROR) {
    return NULL;
  }

  /* Format the command */
  va_start(ap, fmt);
  len = vsnprintf(cmd, sizeof(cmd) - 2, fmt, ap);
  va_end(ap);

  if (len < 0 || (size_t)len >= sizeof(cmd) - 2) {
    tg_log(TG_LOG_ERROR, "Command too long");
    return NULL;
  }

  /* Append \r\n */
  cmd[len] = '\r';
  cmd[len + 1] = '\n';
  cmd[len + 2] = '\0';

  /* If reader thread is running, use the async reply mechanism */
  if (ctrl->running) {
    tg_mutex_lock(&ctrl->reply_mutex);
    tg_buf_clear(&ctrl->reply_buf);
    ctrl->reply_ready = 0;
    tg_mutex_unlock(&ctrl->reply_mutex);

    if (sock_send_all(ctrl->sock, cmd, (size_t)(len + 2)) != 0) {
      tg_log(TG_LOG_ERROR, "Failed to send command");
      return NULL;
    }

    /* Wait for reply_ready */
    while (!ctrl->reply_ready && elapsed < timeout_ms) {
      tg_sleep_ms(1);
      elapsed++;
    }

    if (!ctrl->reply_ready) {
      tg_log(TG_LOG_ERROR, "Command timed out: %.40s", cmd);
      return NULL;
    }

    tg_mutex_lock(&ctrl->reply_mutex);
    result = tg_buf_strdup(&ctrl->reply_buf);
    tg_buf_clear(&ctrl->reply_buf);
    ctrl->reply_ready = 0;
    tg_mutex_unlock(&ctrl->reply_mutex);

    return result;
  }

  /* No reader thread -- synchronous read */
  if (sock_send_all(ctrl->sock, cmd, (size_t)(len + 2)) != 0) {
    tg_log(TG_LOG_ERROR, "Failed to send command");
    return NULL;
  }

  /* Read lines until we get a final reply (code + space) */
  {
    tg_buf_t buf;
    tg_buf_init(&buf);

    for (;;) {
      char line[TG_CTRL_MAX_LINE];
      int llen = sock_read_line(ctrl->sock, line, sizeof(line));
      if (llen < 0) {
        tg_log(TG_LOG_ERROR, "Read error during command");
        tg_buf_free(&buf);
        return NULL;
      }

      tg_buf_append(&buf, line, (size_t)llen);
      tg_buf_append(&buf, "\n", 1);

      /* Check for data block */
      if (llen >= 4 && line[3] == '+') {
        /* Read data until "." on a line by itself */
        for (;;) {
          int dlen = sock_read_line(ctrl->sock, line, sizeof(line));
          if (dlen < 0) {
            tg_buf_free(&buf);
            return NULL;
          }
          tg_buf_append(&buf, line, (size_t)dlen);
          tg_buf_append(&buf, "\n", 1);
          if (dlen == 1 && line[0] == '.')
            break;
        }
        continue;
      }

      /* Final reply line: 3-digit code + space */
      if (llen >= 4 && line[3] == ' ')
        break;
    }

    result = tg_buf_strdup(&buf);
    tg_buf_free(&buf);
    return result;
  }
}

char *
tg_ctrl_getinfo(tg_ctrl_t *ctrl, const char *key)
{
  char *reply;
  char *value = NULL;
  char prefix[512];
  const char *p;
  size_t plen;

  reply = tg_ctrl_command(ctrl, "GETINFO %s", key);
  if (!reply)
    return NULL;

  /* Look for "250-key=" or "250 key=" */
  snprintf(prefix, sizeof(prefix), "%s=", key);
  plen = strlen(prefix);

  p = reply;
  while (*p) {
    /* Skip the 3-digit code and separator */
    if (strlen(p) > 4 && (p[3] == '-' || p[3] == ' ')) {
      const char *after_code = p + 4;
      if (strncmp(after_code, prefix, plen) == 0) {
        const char *val_start = after_code + plen;
        const char *val_end = val_start;
        while (*val_end && *val_end != '\n' && *val_end != '\r')
          val_end++;
        value = (char *)malloc((size_t)(val_end - val_start) + 1);
        if (value) {
          memcpy(value, val_start, (size_t)(val_end - val_start));
          value[val_end - val_start] = '\0';
        }
        break;
      }
    }
    /* Advance to next line */
    while (*p && *p != '\n')
      p++;
    if (*p == '\n')
      p++;
  }

  free(reply);
  return value;
}

char *
tg_ctrl_getconf(tg_ctrl_t *ctrl, const char *key)
{
  char *reply;
  char *value = NULL;
  char prefix[512];
  const char *p;
  size_t plen;

  reply = tg_ctrl_command(ctrl, "GETCONF %s", key);
  if (!reply)
    return NULL;

  /* Look for "250 key=value" or "250-key=value" */
  snprintf(prefix, sizeof(prefix), "%s=", key);
  plen = strlen(prefix);

  p = reply;
  while (*p) {
    if (strlen(p) > 4 && (p[3] == '-' || p[3] == ' ')) {
      const char *after_code = p + 4;
      if (strncasecmp(after_code, prefix, plen) == 0) {
        const char *val_start = after_code + plen;
        const char *val_end = val_start;
        while (*val_end && *val_end != '\n' && *val_end != '\r')
          val_end++;
        value = (char *)malloc((size_t)(val_end - val_start) + 1);
        if (value) {
          memcpy(value, val_start, (size_t)(val_end - val_start));
          value[val_end - val_start] = '\0';
        }
        break;
      }
    }
    while (*p && *p != '\n')
      p++;
    if (*p == '\n')
      p++;
  }

  free(reply);
  return value;
}

int
tg_ctrl_setconf(tg_ctrl_t *ctrl, const char *key, const char *value)
{
  char *reply;
  int rc;

  if (value)
    reply = tg_ctrl_command(ctrl, "SETCONF %s=%s", key, value);
  else
    reply = tg_ctrl_command(ctrl, "SETCONF %s", key);

  if (!reply)
    return -1;

  rc = reply_is_ok(reply) ? 0 : -1;
  if (rc != 0)
    tg_log(TG_LOG_WARN, "SETCONF %s failed: %s", key, reply);
  free(reply);
  return rc;
}

int
tg_ctrl_signal(tg_ctrl_t *ctrl, const char *signal_name)
{
  char *reply;
  int rc;

  reply = tg_ctrl_command(ctrl, "SIGNAL %s", signal_name);
  if (!reply)
    return -1;

  rc = reply_is_ok(reply) ? 0 : -1;
  if (rc != 0)
    tg_log(TG_LOG_WARN, "SIGNAL %s failed: %s", signal_name, reply);
  free(reply);
  return rc;
}

int
tg_ctrl_subscribe(tg_ctrl_t *ctrl, const char *events)
{
  char *reply;
  int rc;

  reply = tg_ctrl_command(ctrl, "SETEVENTS %s", events);
  if (!reply)
    return -1;

  rc = reply_is_ok(reply) ? 0 : -1;
  if (rc != 0)
    tg_log(TG_LOG_WARN, "SETEVENTS failed: %s", reply);
  free(reply);
  return rc;
}

int
tg_ctrl_start_reader(tg_ctrl_t *ctrl)
{
  if (ctrl->running)
    return 0;

  ctrl->running = 1;

  if (tg_thread_create(&ctrl->reader_thread, reader_thread_fn, ctrl) != 0) {
    tg_log(TG_LOG_ERROR, "Failed to create reader thread");
    ctrl->running = 0;
    return -1;
  }

  tg_log(TG_LOG_DEBUG, "Control port reader thread started");
  return 0;
}

void
tg_ctrl_stop_reader(tg_ctrl_t *ctrl)
{
  if (!ctrl->running)
    return;

  ctrl->running = 0;

  /* Close the socket to unblock the reader thread's recv() call */
  if (ctrl->sock != TG_INVALID_SOCKET) {
#ifdef _WIN32
    shutdown(ctrl->sock, SD_BOTH);
#else
    shutdown(ctrl->sock, SHUT_RDWR);
#endif
  }

  tg_thread_join(ctrl->reader_thread);
  tg_log(TG_LOG_DEBUG, "Control port reader thread stopped");
}

void
tg_ctrl_drain_events(tg_ctrl_t *ctrl)
{
  char *line;
  while ((line = eq_pop(ctrl)) != NULL) {
    if (ctrl->event_cb)
      ctrl->event_cb(line, ctrl->event_cb_data);
    free(line);
  }
}

void
tg_ctrl_set_event_cb(tg_ctrl_t *ctrl, tg_ctrl_event_cb cb, void *data)
{
  ctrl->event_cb = cb;
  ctrl->event_cb_data = data;
}

void
tg_ctrl_close(tg_ctrl_t *ctrl)
{
  char *line;

  tg_ctrl_stop_reader(ctrl);

  if (ctrl->sock != TG_INVALID_SOCKET) {
#ifdef _WIN32
    closesocket(ctrl->sock);
#else
    close(ctrl->sock);
#endif
    ctrl->sock = TG_INVALID_SOCKET;
  }

  ctrl->state = TG_CTRL_DISCONNECTED;

  /* Free any remaining events in the ring buffer */
  while ((line = eq_pop(ctrl)) != NULL)
    free(line);

  /* Free buffers */
  tg_buf_free(&ctrl->reply_buf);
  tg_mutex_destroy(&ctrl->reply_mutex);

  free(ctrl->cookie_path);
  ctrl->cookie_path = NULL;
  free(ctrl->password);
  ctrl->password = NULL;

  tg_log(TG_LOG_DEBUG, "Control port connection closed");
}

tg_ctrl_state_t
tg_ctrl_get_state(tg_ctrl_t *ctrl)
{
  return ctrl->state;
}
