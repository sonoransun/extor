/* Minimal Mongoose-compatible HTTP/WebSocket server implementation.
 * For production use, replace with full Mongoose from
 * https://github.com/cesanta/mongoose
 * License: MIT */

#include "mongoose.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET mg_sock_t;
#define MG_INVALID_SOCKET INVALID_SOCKET
#define mg_closesocket closesocket
#define mg_sock_errno() WSAGetLastError()
#define EWOULDBLOCK_VAL WSAEWOULDBLOCK
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int mg_sock_t;
#define MG_INVALID_SOCKET (-1)
#define mg_closesocket close
#define mg_sock_errno() errno
#define EWOULDBLOCK_VAL EWOULDBLOCK
#endif

/* ---- Internal helpers ---- */

static int s_next_id = 1;

static void mg_iobuf_resize(unsigned char **buf, size_t *size, size_t needed) {
  if (needed > *size) {
    size_t new_size = *size ? *size : 256;
    while (new_size < needed)
      new_size *= 2;
    *buf = (unsigned char *) realloc(*buf, new_size);
    *size = new_size;
  }
}

static void mg_iobuf_append(unsigned char **buf, size_t *len, size_t *size,
                             const void *data, size_t dlen) {
  mg_iobuf_resize(buf, size, *len + dlen);
  memcpy(*buf + *len, data, dlen);
  *len += dlen;
}

static void mg_iobuf_del(unsigned char **buf, size_t *len, size_t n) {
  if (n > *len) n = *len;
  if (n > 0) {
    memmove(*buf, *buf + n, *len - n);
    *len -= n;
  }
}

static void mg_set_nonblocking(mg_sock_t fd) {
#ifdef _WIN32
  unsigned long on = 1;
  ioctlsocket(fd, FIONBIO, &on);
#else
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static struct mg_connection *mg_conn_new(struct mg_mgr *mgr,
                                          mg_event_handler_t fn,
                                          void *fn_data) {
  struct mg_connection *c =
      (struct mg_connection *) calloc(1, sizeof(struct mg_connection));
  if (!c) return NULL;
  c->mgr = mgr;
  c->fn_data = fn_data;
  c->id = s_next_id++;
  /* Store the handler in userdata since we do not have a fn field */
  c->userdata = (void *) fn;
  c->next = mgr->conns;
  mgr->conns = c;
  return c;
}

static mg_event_handler_t mg_get_handler(struct mg_connection *c) {
  return (mg_event_handler_t) c->userdata;
}

static void mg_call(struct mg_connection *c, int ev, void *ev_data) {
  mg_event_handler_t fn = mg_get_handler(c);
  if (fn) fn(c, ev, ev_data);
}

static void mg_conn_free(struct mg_connection *c) {
  free(c->recv.buf);
  free(c->send.buf);
  free(c);
}

/* ---- SHA-1 (needed for WebSocket handshake) ---- */

typedef struct {
  uint32_t state[5];
  uint32_t count[2];
  unsigned char buffer[64];
} mg_sha1_ctx;

#define SHA1_ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

#define SHA1_BLK0(i)                                                      \
  (block[i] = (SHA1_ROL(block[i], 24) & 0xFF00FF00) |                    \
               (SHA1_ROL(block[i], 8) & 0x00FF00FF))
#define SHA1_BLK(i)                                                       \
  (block[i & 15] = SHA1_ROL(block[(i + 13) & 15] ^ block[(i + 8) & 15] ^ \
                                 block[(i + 2) & 15] ^ block[i & 15],     \
                             1))

#define SHA1_R0(v, w, x, y, z, i)                                    \
  z += ((w & (x ^ y)) ^ y) + SHA1_BLK0(i) + 0x5A827999 +            \
       SHA1_ROL(v, 5);                                               \
  w = SHA1_ROL(w, 30);
#define SHA1_R1(v, w, x, y, z, i)                                    \
  z += ((w & (x ^ y)) ^ y) + SHA1_BLK(i) + 0x5A827999 +             \
       SHA1_ROL(v, 5);                                               \
  w = SHA1_ROL(w, 30);
#define SHA1_R2(v, w, x, y, z, i)                                    \
  z += (w ^ x ^ y) + SHA1_BLK(i) + 0x6ED9EBA1 + SHA1_ROL(v, 5);    \
  w = SHA1_ROL(w, 30);
#define SHA1_R3(v, w, x, y, z, i)                                    \
  z += (((w | x) & y) | (w & x)) + SHA1_BLK(i) + 0x8F1BBCDC +      \
       SHA1_ROL(v, 5);                                               \
  w = SHA1_ROL(w, 30);
#define SHA1_R4(v, w, x, y, z, i)                                    \
  z += (w ^ x ^ y) + SHA1_BLK(i) + 0xCA62C1D6 + SHA1_ROL(v, 5);    \
  w = SHA1_ROL(w, 30);

static void mg_sha1_transform(uint32_t state[5],
                                const unsigned char buf[64]) {
  uint32_t a, b, c, d, e;
  uint32_t block[16];
  memcpy(block, buf, 64);

  a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

  SHA1_R0(a,b,c,d,e, 0); SHA1_R0(e,a,b,c,d, 1); SHA1_R0(d,e,a,b,c, 2);
  SHA1_R0(c,d,e,a,b, 3); SHA1_R0(b,c,d,e,a, 4); SHA1_R0(a,b,c,d,e, 5);
  SHA1_R0(e,a,b,c,d, 6); SHA1_R0(d,e,a,b,c, 7); SHA1_R0(c,d,e,a,b, 8);
  SHA1_R0(b,c,d,e,a, 9); SHA1_R0(a,b,c,d,e,10); SHA1_R0(e,a,b,c,d,11);
  SHA1_R0(d,e,a,b,c,12); SHA1_R0(c,d,e,a,b,13); SHA1_R0(b,c,d,e,a,14);
  SHA1_R0(a,b,c,d,e,15); SHA1_R1(e,a,b,c,d,16); SHA1_R1(d,e,a,b,c,17);
  SHA1_R1(c,d,e,a,b,18); SHA1_R1(b,c,d,e,a,19); SHA1_R2(a,b,c,d,e,20);
  SHA1_R2(e,a,b,c,d,21); SHA1_R2(d,e,a,b,c,22); SHA1_R2(c,d,e,a,b,23);
  SHA1_R2(b,c,d,e,a,24); SHA1_R2(a,b,c,d,e,25); SHA1_R2(e,a,b,c,d,26);
  SHA1_R2(d,e,a,b,c,27); SHA1_R2(c,d,e,a,b,28); SHA1_R2(b,c,d,e,a,29);
  SHA1_R2(a,b,c,d,e,30); SHA1_R2(e,a,b,c,d,31); SHA1_R2(d,e,a,b,c,32);
  SHA1_R2(c,d,e,a,b,33); SHA1_R2(b,c,d,e,a,34); SHA1_R2(a,b,c,d,e,35);
  SHA1_R2(e,a,b,c,d,36); SHA1_R2(d,e,a,b,c,37); SHA1_R2(c,d,e,a,b,38);
  SHA1_R2(b,c,d,e,a,39); SHA1_R3(a,b,c,d,e,40); SHA1_R3(e,a,b,c,d,41);
  SHA1_R3(d,e,a,b,c,42); SHA1_R3(c,d,e,a,b,43); SHA1_R3(b,c,d,e,a,44);
  SHA1_R3(a,b,c,d,e,45); SHA1_R3(e,a,b,c,d,46); SHA1_R3(d,e,a,b,c,47);
  SHA1_R3(c,d,e,a,b,48); SHA1_R3(b,c,d,e,a,49); SHA1_R3(a,b,c,d,e,50);
  SHA1_R3(e,a,b,c,d,51); SHA1_R3(d,e,a,b,c,52); SHA1_R3(c,d,e,a,b,53);
  SHA1_R3(b,c,d,e,a,54); SHA1_R3(a,b,c,d,e,55); SHA1_R3(e,a,b,c,d,56);
  SHA1_R3(d,e,a,b,c,57); SHA1_R3(c,d,e,a,b,58); SHA1_R3(b,c,d,e,a,59);
  SHA1_R4(a,b,c,d,e,60); SHA1_R4(e,a,b,c,d,61); SHA1_R4(d,e,a,b,c,62);
  SHA1_R4(c,d,e,a,b,63); SHA1_R4(b,c,d,e,a,64); SHA1_R4(a,b,c,d,e,65);
  SHA1_R4(e,a,b,c,d,66); SHA1_R4(d,e,a,b,c,67); SHA1_R4(c,d,e,a,b,68);
  SHA1_R4(b,c,d,e,a,69); SHA1_R4(a,b,c,d,e,70); SHA1_R4(e,a,b,c,d,71);
  SHA1_R4(d,e,a,b,c,72); SHA1_R4(c,d,e,a,b,73); SHA1_R4(b,c,d,e,a,74);
  SHA1_R4(a,b,c,d,e,75); SHA1_R4(e,a,b,c,d,76); SHA1_R4(d,e,a,b,c,77);
  SHA1_R4(c,d,e,a,b,78); SHA1_R4(b,c,d,e,a,79);

  state[0] += a; state[1] += b; state[2] += c;
  state[3] += d; state[4] += e;
}

static void mg_sha1_init(mg_sha1_ctx *ctx) {
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xEFCDAB89;
  ctx->state[2] = 0x98BADCFE;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xC3D2E1F0;
  ctx->count[0] = ctx->count[1] = 0;
}

static void mg_sha1_update(mg_sha1_ctx *ctx, const unsigned char *data,
                            size_t len) {
  size_t i, j;
  j = ctx->count[0];
  if ((ctx->count[0] += (uint32_t)(len << 3)) < j)
    ctx->count[1]++;
  ctx->count[1] += (uint32_t)(len >> 29);
  j = (j >> 3) & 63;
  if ((j + len) > 63) {
    memcpy(&ctx->buffer[j], data, (i = 64 - j));
    mg_sha1_transform(ctx->state, ctx->buffer);
    for (; i + 63 < len; i += 64)
      mg_sha1_transform(ctx->state, &data[i]);
    j = 0;
  } else {
    i = 0;
  }
  memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void mg_sha1_final(unsigned char digest[20], mg_sha1_ctx *ctx) {
  unsigned char finalcount[8];
  unsigned char c;
  unsigned int i;
  for (i = 0; i < 8; i++)
    finalcount[i] = (unsigned char)
        ((ctx->count[(i >= 4) ? 0 : 1] >> ((3 - (i & 3)) * 8)) & 255);
  c = 0200;
  mg_sha1_update(ctx, &c, 1);
  while ((ctx->count[0] & 504) != 448) {
    c = 0000;
    mg_sha1_update(ctx, &c, 1);
  }
  mg_sha1_update(ctx, finalcount, 8);
  for (i = 0; i < 20; i++)
    digest[i] = (unsigned char)
        ((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
}

/* ---- Base64 (for WebSocket handshake) ---- */

static const char mg_b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t mg_base64_encode(const unsigned char *data, size_t len,
                                char *out) {
  size_t i, j = 0;
  for (i = 0; i < len; i += 3) {
    uint32_t n = ((uint32_t) data[i]) << 16;
    if (i + 1 < len) n |= ((uint32_t) data[i + 1]) << 8;
    if (i + 2 < len) n |= (uint32_t) data[i + 2];
    out[j++] = mg_b64chars[(n >> 18) & 63];
    out[j++] = mg_b64chars[(n >> 12) & 63];
    out[j++] = (i + 1 < len) ? mg_b64chars[(n >> 6) & 63] : '=';
    out[j++] = (i + 2 < len) ? mg_b64chars[n & 63] : '=';
  }
  out[j] = '\0';
  return j;
}

/* ---- struct mg_str helpers ---- */

struct mg_str mg_str(const char *s) {
  struct mg_str r;
  r.buf = s;
  r.len = s ? strlen(s) : 0;
  return r;
}

int mg_strcmp(struct mg_str a, struct mg_str b) {
  size_t min = a.len < b.len ? a.len : b.len;
  int rc = memcmp(a.buf, b.buf, min);
  if (rc != 0) return rc;
  if (a.len < b.len) return -1;
  if (a.len > b.len) return 1;
  return 0;
}

int mg_match(struct mg_str str, struct mg_str pattern, struct mg_str *caps) {
  /* Simple glob matcher: supports * and ? */
  size_t si = 0, pi = 0;
  size_t star_si = (size_t)-1, star_pi = (size_t)-1;
  (void) caps;
  while (si < str.len) {
    if (pi < pattern.len &&
        (pattern.buf[pi] == '?' || pattern.buf[pi] == str.buf[si])) {
      si++;
      pi++;
    } else if (pi < pattern.len && pattern.buf[pi] == '*') {
      star_si = si;
      star_pi = pi++;
    } else if (star_pi != (size_t)-1) {
      pi = star_pi + 1;
      si = ++star_si;
    } else {
      return 0;
    }
  }
  while (pi < pattern.len && pattern.buf[pi] == '*') pi++;
  return pi == pattern.len;
}

int mg_snprintf(char *buf, size_t len, const char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(buf, len, fmt, ap);
  va_end(ap);
  if (n < 0) n = 0;
  if ((size_t) n >= len && len > 0) n = (int) len - 1;
  return n;
}

/* ---- HTTP parsing ---- */

static int mg_ncasecmp(const char *a, const char *b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    int ca = tolower((unsigned char) a[i]);
    int cb = tolower((unsigned char) b[i]);
    if (ca != cb) return ca - cb;
  }
  return 0;
}

/* Find \r\n\r\n in buffer, return offset past it or 0 if not found */
static size_t mg_find_header_end(const unsigned char *buf, size_t len) {
  size_t i;
  for (i = 0; i + 3 < len; i++) {
    if (buf[i] == '\r' && buf[i+1] == '\n' &&
        buf[i+2] == '\r' && buf[i+3] == '\n')
      return i + 4;
  }
  return 0;
}

/* Parse a single line from buf (up to \r\n). Return bytes consumed */
static size_t mg_parse_line(const char *buf, size_t len,
                             struct mg_str *line) {
  size_t i;
  for (i = 0; i + 1 < len; i++) {
    if (buf[i] == '\r' && buf[i+1] == '\n') {
      line->buf = buf;
      line->len = i;
      return i + 2;
    }
  }
  return 0;
}

static int mg_parse_http_msg(const char *buf, size_t len,
                              struct mg_http_message *hm) {
  struct mg_str line;
  size_t off, n;
  const char *p, *end;

  memset(hm, 0, sizeof(*hm));

  /* Request line */
  off = mg_parse_line(buf, len, &line);
  if (off == 0) return -1;

  /* Method */
  p = line.buf;
  end = line.buf + line.len;
  hm->method.buf = p;
  while (p < end && *p != ' ') p++;
  hm->method.len = (size_t)(p - hm->method.buf);
  if (p < end) p++;

  /* URI */
  hm->uri.buf = p;
  while (p < end && *p != ' ' && *p != '?') p++;
  hm->uri.len = (size_t)(p - hm->uri.buf);

  /* Query */
  if (p < end && *p == '?') {
    p++;
    hm->query.buf = p;
    while (p < end && *p != ' ') p++;
    hm->query.len = (size_t)(p - hm->query.buf);
  }
  if (p < end) p++;

  /* Protocol */
  hm->proto.buf = p;
  hm->proto.len = (size_t)(end - p);

  /* Headers */
  hm->header_count = 0;
  while (off < len && hm->header_count < 15) {
    n = mg_parse_line(buf + off, len - off, &line);
    if (n == 0) break;
    if (line.len == 0) {
      off += n;
      break; /* empty line = end of headers */
    }
    /* Find colon */
    p = line.buf;
    end = line.buf + line.len;
    while (p < end && *p != ':') p++;
    if (p < end) {
      int idx = hm->header_count * 2;
      hm->headers[idx].buf = line.buf;
      hm->headers[idx].len = (size_t)(p - line.buf);
      p++; /* skip colon */
      while (p < end && *p == ' ') p++; /* skip space */
      hm->headers[idx + 1].buf = p;
      hm->headers[idx + 1].len = (size_t)(end - p);
      hm->header_count++;
    }
    off += n;
  }

  /* Body is whatever remains */
  hm->body.buf = buf + off;
  hm->body.len = len - off;

  return (int) off;
}

struct mg_str mg_http_get_header(const struct mg_http_message *hm,
                                 const char *name) {
  struct mg_str empty = {NULL, 0};
  int i;
  size_t nlen = strlen(name);
  for (i = 0; i < hm->header_count; i++) {
    int idx = i * 2;
    if (hm->headers[idx].len == nlen &&
        mg_ncasecmp(hm->headers[idx].buf, name, nlen) == 0) {
      return hm->headers[idx + 1];
    }
  }
  return empty;
}

/* ---- URL decoding ---- */

static int mg_url_decode_char(const char *s) {
  int hi, lo;
  if (s[0] >= '0' && s[0] <= '9') hi = s[0] - '0';
  else if (s[0] >= 'A' && s[0] <= 'F') hi = s[0] - 'A' + 10;
  else if (s[0] >= 'a' && s[0] <= 'f') hi = s[0] - 'a' + 10;
  else return -1;
  if (s[1] >= '0' && s[1] <= '9') lo = s[1] - '0';
  else if (s[1] >= 'A' && s[1] <= 'F') lo = s[1] - 'A' + 10;
  else if (s[1] >= 'a' && s[1] <= 'f') lo = s[1] - 'a' + 10;
  else return -1;
  return (hi << 4) | lo;
}

int mg_http_get_var(const struct mg_str *buf, const char *name, char *dst,
                    size_t dst_len) {
  size_t nlen = strlen(name);
  const char *p = buf->buf;
  const char *end = buf->buf + buf->len;
  size_t di = 0;

  if (!buf->buf || !dst || dst_len == 0) return -1;
  dst[0] = '\0';

  while (p < end) {
    const char *key = p;
    const char *eq = NULL;
    const char *amp = NULL;
    /* find = and & */
    const char *s;
    for (s = p; s < end; s++) {
      if (*s == '=' && !eq) eq = s;
      if (*s == '&') { amp = s; break; }
    }
    if (!amp) amp = end;
    if (eq && (size_t)(eq - key) == nlen &&
        memcmp(key, name, nlen) == 0) {
      /* Decode value */
      const char *v = eq + 1;
      di = 0;
      while (v < amp && di + 1 < dst_len) {
        if (*v == '%' && v + 2 < amp) {
          int ch = mg_url_decode_char(v + 1);
          if (ch >= 0) {
            dst[di++] = (char) ch;
            v += 3;
            continue;
          }
        }
        if (*v == '+') {
          dst[di++] = ' ';
        } else {
          dst[di++] = *v;
        }
        v++;
      }
      dst[di] = '\0';
      return (int) di;
    }
    p = amp;
    if (p < end) p++; /* skip '&' */
  }
  return -2; /* not found */
}

int mg_http_match_uri(const struct mg_http_message *hm, const char *glob) {
  struct mg_str pat = mg_str(glob);
  return mg_match(hm->uri, pat, NULL);
}

/* ---- Content types ---- */

static const char *mg_guess_content_type(const char *path) {
  size_t len = strlen(path);
  if (len > 5 && strcmp(path + len - 5, ".html") == 0)
    return "text/html; charset=utf-8";
  if (len > 4 && strcmp(path + len - 4, ".htm") == 0)
    return "text/html; charset=utf-8";
  if (len > 4 && strcmp(path + len - 4, ".css") == 0)
    return "text/css";
  if (len > 3 && strcmp(path + len - 3, ".js") == 0)
    return "application/javascript";
  if (len > 5 && strcmp(path + len - 5, ".json") == 0)
    return "application/json";
  if (len > 4 && strcmp(path + len - 4, ".png") == 0)
    return "image/png";
  if (len > 4 && strcmp(path + len - 4, ".jpg") == 0)
    return "image/jpeg";
  if (len > 5 && strcmp(path + len - 5, ".jpeg") == 0)
    return "image/jpeg";
  if (len > 4 && strcmp(path + len - 4, ".gif") == 0)
    return "image/gif";
  if (len > 4 && strcmp(path + len - 4, ".svg") == 0)
    return "image/svg+xml";
  if (len > 4 && strcmp(path + len - 4, ".ico") == 0)
    return "image/x-icon";
  if (len > 4 && strcmp(path + len - 4, ".txt") == 0)
    return "text/plain";
  if (len > 5 && strcmp(path + len - 5, ".woff") == 0)
    return "font/woff";
  if (len > 6 && strcmp(path + len - 6, ".woff2") == 0)
    return "font/woff2";
  return "application/octet-stream";
}

/* ---- Static file serving ---- */

void mg_http_serve_dir(struct mg_connection *c, struct mg_http_message *hm,
                       const char *root_dir) {
  char path[1024];
  const char *uri;
  size_t uri_len;
  FILE *fp;
  struct stat st;
  char *body;
  const char *ctype;

  uri = hm->uri.buf;
  uri_len = hm->uri.len;

  /* Sanitize: reject paths with ".." */
  if (uri_len >= 2 && strstr(uri, "..") != NULL) {
    mg_http_reply(c, 403, "", "Forbidden\n");
    return;
  }

  /* Build path */
  if (uri_len == 1 && uri[0] == '/') {
    mg_snprintf(path, sizeof(path), "%s/index.html", root_dir);
  } else {
    size_t rlen = strlen(root_dir);
    size_t copy_len = uri_len;
    if (rlen + copy_len + 1 >= sizeof(path)) {
      mg_http_reply(c, 414, "", "URI too long\n");
      return;
    }
    memcpy(path, root_dir, rlen);
    memcpy(path + rlen, uri, copy_len);
    path[rlen + copy_len] = '\0';
  }

  /* Check for directory, append index.html */
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
    size_t plen = strlen(path);
    if (plen + 12 < sizeof(path)) {
      if (plen > 0 && path[plen - 1] != '/') {
        path[plen++] = '/';
      }
      memcpy(path + plen, "index.html", 11);
    }
  }

  fp = fopen(path, "rb");
  if (!fp) {
    mg_http_reply(c, 404, "", "Not found: %.*s\n",
                  (int) hm->uri.len, hm->uri.buf);
    return;
  }

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fsize < 0 || fsize > 10 * 1024 * 1024) {
    fclose(fp);
    mg_http_reply(c, 500, "", "File too large\n");
    return;
  }

  body = (char *) malloc((size_t) fsize);
  if (!body) {
    fclose(fp);
    mg_http_reply(c, 500, "", "Out of memory\n");
    return;
  }

  if (fread(body, 1, (size_t) fsize, fp) != (size_t) fsize) {
    free(body);
    fclose(fp);
    mg_http_reply(c, 500, "", "Read error\n");
    return;
  }
  fclose(fp);

  ctype = mg_guess_content_type(path);

  /* Send response */
  {
    char hdr[256];
    mg_snprintf(hdr, sizeof(hdr),
                "Content-Type: %s\r\nContent-Length: %ld\r\n", ctype, fsize);
    mg_http_reply(c, 200, hdr, "%.*s", (int) fsize, body);
  }
  free(body);
}

/* ---- HTTP response ---- */

void mg_http_reply(struct mg_connection *c, int status, const char *headers,
                   const char *fmt, ...) {
  char body[8192];
  char head[1024];
  va_list ap;
  int body_len, head_len;
  const char *reason;

  va_start(ap, fmt);
  body_len = vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  if (body_len < 0) body_len = 0;
  if ((size_t) body_len >= sizeof(body)) body_len = (int) sizeof(body) - 1;

  switch (status) {
    case 101: reason = "Switching Protocols"; break;
    case 200: reason = "OK"; break;
    case 301: reason = "Moved Permanently"; break;
    case 302: reason = "Found"; break;
    case 304: reason = "Not Modified"; break;
    case 400: reason = "Bad Request"; break;
    case 403: reason = "Forbidden"; break;
    case 404: reason = "Not Found"; break;
    case 405: reason = "Method Not Allowed"; break;
    case 414: reason = "URI Too Long"; break;
    case 500: reason = "Internal Server Error"; break;
    default: reason = "OK"; break;
  }

  if (!headers) headers = "";

  /* If headers don't contain Content-Length and this is not 101, add it */
  if (status != 101) {
    head_len = mg_snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n%sContent-Length: %d\r\n\r\n",
        status, reason, headers, body_len);
  } else {
    head_len = mg_snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n%s\r\n",
        status, reason, headers);
  }

  mg_iobuf_append(&c->send.buf, &c->send.len, &c->send.size,
                   head, (size_t) head_len);
  if (body_len > 0) {
    mg_iobuf_append(&c->send.buf, &c->send.len, &c->send.size,
                     body, (size_t) body_len);
  }
}

/* ---- WebSocket ---- */

static const char *mg_ws_magic = "258EAFA5-E914-47DA-95CA-5AB5DC11AD46";

void mg_ws_upgrade(struct mg_connection *c, struct mg_http_message *hm,
                   const char *fmt, ...) {
  struct mg_str key;
  mg_sha1_ctx sha;
  unsigned char hash[20];
  char accept_key[64];
  char headers[512];
  (void) fmt;

  key = mg_http_get_header(hm, "Sec-WebSocket-Key");
  if (!key.buf || key.len == 0) {
    mg_http_reply(c, 400, "", "Missing WebSocket key\n");
    return;
  }

  /* Compute accept key: SHA1(key + magic) -> base64 */
  mg_sha1_init(&sha);
  mg_sha1_update(&sha, (const unsigned char *) key.buf, key.len);
  mg_sha1_update(&sha, (const unsigned char *) mg_ws_magic,
                  strlen(mg_ws_magic));
  mg_sha1_final(hash, &sha);
  mg_base64_encode(hash, 20, accept_key);

  mg_snprintf(headers, sizeof(headers),
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: %s\r\n",
      accept_key);

  mg_http_reply(c, 101, headers, "");
  c->is_websocket = 1;

  mg_call(c, MG_EV_WS_OPEN, NULL);
}

size_t mg_ws_send(struct mg_connection *c, const void *buf, size_t len,
                  int op) {
  /* Build a WebSocket frame (server -> client: no masking) */
  unsigned char hdr[10];
  size_t hdr_len = 0;

  hdr[0] = (unsigned char)(0x80 | (op & 0x0F)); /* FIN + opcode */
  if (len < 126) {
    hdr[1] = (unsigned char) len;
    hdr_len = 2;
  } else if (len < 65536) {
    hdr[1] = 126;
    hdr[2] = (unsigned char)((len >> 8) & 0xFF);
    hdr[3] = (unsigned char)(len & 0xFF);
    hdr_len = 4;
  } else {
    hdr[1] = 127;
    hdr[2] = 0; hdr[3] = 0; hdr[4] = 0; hdr[5] = 0;
    hdr[6] = (unsigned char)((len >> 24) & 0xFF);
    hdr[7] = (unsigned char)((len >> 16) & 0xFF);
    hdr[8] = (unsigned char)((len >> 8) & 0xFF);
    hdr[9] = (unsigned char)(len & 0xFF);
    hdr_len = 10;
  }

  mg_iobuf_append(&c->send.buf, &c->send.len, &c->send.size,
                   hdr, hdr_len);
  mg_iobuf_append(&c->send.buf, &c->send.len, &c->send.size,
                   buf, len);
  return hdr_len + len;
}

/* Parse a WebSocket frame from recv buffer. Returns frame size or 0 */
static size_t mg_ws_parse_frame(const unsigned char *buf, size_t len,
                                 struct mg_ws_message *wm) {
  size_t hdr_len, payload_len, i;
  int masked;
  unsigned char mask[4];

  if (len < 2) return 0;

  wm->flags = buf[0];
  masked = (buf[1] & 0x80) != 0;
  payload_len = buf[1] & 0x7F;
  hdr_len = 2;

  if (payload_len == 126) {
    if (len < 4) return 0;
    payload_len = ((size_t) buf[2] << 8) | (size_t) buf[3];
    hdr_len = 4;
  } else if (payload_len == 127) {
    if (len < 10) return 0;
    payload_len = 0;
    for (i = 0; i < 8; i++)
      payload_len = (payload_len << 8) | buf[2 + i];
    hdr_len = 10;
  }

  if (masked) {
    if (len < hdr_len + 4) return 0;
    memcpy(mask, buf + hdr_len, 4);
    hdr_len += 4;
  }

  if (len < hdr_len + payload_len) return 0;

  /* Unmask in place if needed (we can since it's our buffer) */
  if (masked) {
    unsigned char *p = (unsigned char *) buf + hdr_len;
    for (i = 0; i < payload_len; i++)
      p[i] ^= mask[i & 3];
  }

  wm->data.buf = (const char *) buf + hdr_len;
  wm->data.len = payload_len;

  return hdr_len + payload_len;
}

/* ---- Manager and event loop ---- */

void mg_mgr_init(struct mg_mgr *mgr) {
  memset(mgr, 0, sizeof(*mgr));
#ifdef _WIN32
  {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
  }
#endif
}

void mg_mgr_free(struct mg_mgr *mgr) {
  struct mg_connection *c = mgr->conns;
  while (c) {
    struct mg_connection *next = c->next;
    if (c->is_listening || c->is_accepted) {
      mg_sock_t fd;
      memcpy(&fd, c->data, sizeof(fd));
      if (fd != MG_INVALID_SOCKET) mg_closesocket(fd);
    }
    mg_call(c, MG_EV_CLOSE, NULL);
    mg_conn_free(c);
    c = next;
  }
  mgr->conns = NULL;
}

struct mg_connection *mg_http_listen(struct mg_mgr *mgr, const char *url,
                                     mg_event_handler_t fn, void *fn_data) {
  mg_sock_t sock;
  struct sockaddr_in addr;
  int port = 8080;
  const char *p;
  int one = 1;
  struct mg_connection *c;

  /* Parse port from url like "http://0.0.0.0:8080" */
  p = strrchr(url, ':');
  if (p) port = atoi(p + 1);

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == MG_INVALID_SOCKET) return NULL;

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &one,
             sizeof(one));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t) port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  /* Check for specific bind address */
  {
    const char *host_start = strstr(url, "://");
    if (host_start) {
      host_start += 3;
      if (host_start < p) {
        char host[64];
        size_t hlen = (size_t)(p - host_start);
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        memcpy(host, host_start, hlen);
        host[hlen] = '\0';
        if (strcmp(host, "0.0.0.0") != 0 && strcmp(host, "localhost") != 0) {
          addr.sin_addr.s_addr = inet_addr(host);
        }
        if (strcmp(host, "localhost") == 0) {
          addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
      }
    }
  }

  if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
    mg_closesocket(sock);
    return NULL;
  }

  if (listen(sock, 128) != 0) {
    mg_closesocket(sock);
    return NULL;
  }

  mg_set_nonblocking(sock);

  c = mg_conn_new(mgr, fn, fn_data);
  if (!c) {
    mg_closesocket(sock);
    return NULL;
  }
  c->is_listening = 1;
  c->loc.port = (uint16_t) port;
  memcpy(c->data, &sock, sizeof(sock));

  mg_call(c, MG_EV_OPEN, NULL);
  return c;
}

void mg_mgr_poll(struct mg_mgr *mgr, int timeout_ms) {
  struct mg_connection *c, *next;
  fd_set rfds, wfds;
  struct timeval tv;
  mg_sock_t maxfd = 0;
  int nfds;

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);

  for (c = mgr->conns; c; c = c->next) {
    mg_sock_t fd;
    memcpy(&fd, c->data, sizeof(fd));
    if (fd == MG_INVALID_SOCKET) continue;
    if (c->is_closing) continue;
    FD_SET(fd, &rfds);
    if (c->send.len > 0) FD_SET(fd, &wfds);
    if (fd > maxfd) maxfd = fd;
  }

  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  nfds = select((int) maxfd + 1, &rfds, &wfds, NULL, &tv);

  /* Fire poll event for all connections */
  for (c = mgr->conns; c; c = c->next) {
    mg_call(c, MG_EV_POLL, NULL);
  }

  if (nfds <= 0) return;

  for (c = mgr->conns; c; c = c->next) {
    mg_sock_t fd;
    memcpy(&fd, c->data, sizeof(fd));
    if (fd == MG_INVALID_SOCKET) continue;
    if (c->is_closing) continue;

    /* Accept new connections on listening socket */
    if (c->is_listening && FD_ISSET(fd, &rfds)) {
      struct sockaddr_in client_addr;
      socklen_t addrlen = sizeof(client_addr);
      mg_sock_t newfd = accept(fd, (struct sockaddr *) &client_addr,
                                &addrlen);
      if (newfd != MG_INVALID_SOCKET) {
        struct mg_connection *nc;
        mg_set_nonblocking(newfd);
        nc = mg_conn_new(mgr, mg_get_handler(c), c->fn_data);
        if (nc) {
          nc->is_accepted = 1;
          nc->rem.ip = ntohl(client_addr.sin_addr.s_addr);
          nc->rem.port = ntohs(client_addr.sin_port);
          memcpy(nc->data, &newfd, sizeof(newfd));
          mg_call(nc, MG_EV_OPEN, NULL);
          mg_call(nc, MG_EV_ACCEPT, NULL);
        } else {
          mg_closesocket(newfd);
        }
      }
      continue;
    }

    /* Read data */
    if (FD_ISSET(fd, &rfds)) {
      unsigned char tmp[4096];
      int nr;
#ifdef _WIN32
      nr = recv(fd, (char *) tmp, sizeof(tmp), 0);
#else
      nr = (int) read(fd, tmp, sizeof(tmp));
#endif
      if (nr > 0) {
        mg_iobuf_append(&c->recv.buf, &c->recv.len, &c->recv.size,
                         tmp, (size_t) nr);
        mg_call(c, MG_EV_READ, NULL);

        /* Process WebSocket frames */
        if (c->is_websocket) {
          struct mg_ws_message wm;
          size_t frame_sz;
          while ((frame_sz = mg_ws_parse_frame(c->recv.buf, c->recv.len,
                                                &wm)) > 0) {
            int opcode = wm.flags & 0x0F;
            if (opcode == WEBSOCKET_OP_CLOSE) {
              c->is_closing = 1;
              break;
            }
            if (opcode == WEBSOCKET_OP_TEXT ||
                opcode == WEBSOCKET_OP_BINARY) {
              mg_call(c, MG_EV_WS_MSG, &wm);
            } else {
              mg_call(c, MG_EV_WS_CTL, &wm);
            }
            mg_iobuf_del(&c->recv.buf, &c->recv.len, frame_sz);
          }
        } else {
          /* Try to parse HTTP message */
          size_t hdr_end = mg_find_header_end(c->recv.buf, c->recv.len);
          if (hdr_end > 0) {
            struct mg_http_message hm;
            if (mg_parse_http_msg((const char *) c->recv.buf,
                                   c->recv.len, &hm) > 0) {
              /* Check for Content-Length to wait for body */
              struct mg_str cl = mg_http_get_header(&hm, "Content-Length");
              size_t content_len = 0;
              if (cl.buf) content_len = (size_t) atol(cl.buf);
              if (c->recv.len >= hdr_end + content_len) {
                hm.body.buf = (const char *) c->recv.buf + hdr_end;
                hm.body.len = content_len;
                mg_call(c, MG_EV_HTTP_MSG, &hm);
                mg_iobuf_del(&c->recv.buf, &c->recv.len,
                              hdr_end + content_len);
              }
            }
          }
        }
      } else if (nr == 0) {
        c->is_closing = 1;
      } else {
        int err = mg_sock_errno();
        if (err != EWOULDBLOCK_VAL && err != EAGAIN) {
          c->is_closing = 1;
        }
      }
    }

    /* Write data */
    if (FD_ISSET(fd, &wfds) && c->send.len > 0) {
      int nw;
#ifdef _WIN32
      nw = send(fd, (const char *) c->send.buf, (int) c->send.len, 0);
#else
      nw = (int) write(fd, c->send.buf, c->send.len);
#endif
      if (nw > 0) {
        mg_iobuf_del(&c->send.buf, &c->send.len, (size_t) nw);
        mg_call(c, MG_EV_WRITE, NULL);
      } else if (nw < 0) {
        int err = mg_sock_errno();
        if (err != EWOULDBLOCK_VAL && err != EAGAIN) {
          c->is_closing = 1;
        }
      }
    }
  }

  /* Close connections marked for closing */
  c = mgr->conns;
  while (c) {
    next = c->next;
    if (c->is_closing || c->is_draining) {
      /* If draining, only close when send buffer empty */
      if (c->is_draining && c->send.len > 0) {
        c = next;
        continue;
      }
      /* Remove from list */
      if (c == mgr->conns) {
        mgr->conns = c->next;
      } else {
        struct mg_connection *prev;
        for (prev = mgr->conns; prev && prev->next != c; prev = prev->next)
          ;
        if (prev) prev->next = c->next;
      }
      {
        mg_sock_t fd;
        memcpy(&fd, c->data, sizeof(fd));
        if (fd != MG_INVALID_SOCKET) mg_closesocket(fd);
      }
      mg_call(c, MG_EV_CLOSE, NULL);
      mg_conn_free(c);
    }
    c = next;
  }
}
