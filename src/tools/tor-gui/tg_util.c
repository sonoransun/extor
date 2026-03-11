/* tor-gui utility functions implementation */

#include "tg_util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <process.h>
#else
#include <netdb.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/* ---- Logging ---- */

static int s_log_level = TG_LOG_INFO;

void tg_set_log_level(int level)
{
  s_log_level = level;
}

void tg_log(int level, const char *fmt, ...)
{
  va_list ap;
  const char *prefix;
  time_t now;
  struct tm *tm_info;
  char timebuf[32];

  if (level > s_log_level)
    return;

  switch (level) {
    case TG_LOG_ERROR: prefix = "ERROR"; break;
    case TG_LOG_WARN:  prefix = "WARN "; break;
    case TG_LOG_INFO:  prefix = "INFO "; break;
    case TG_LOG_DEBUG: prefix = "DEBUG"; break;
    default:           prefix = "?????"; break;
  }

  now = time(NULL);
  tm_info = localtime(&now);
  strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);

  fprintf(stderr, "[%s] [%s] ", prefix, timebuf);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  fflush(stderr);
}

/* ---- Dynamic buffer ---- */

#define TG_BUF_INIT_CAP 256

void tg_buf_init(tg_buf_t *b)
{
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

void tg_buf_free(tg_buf_t *b)
{
  free(b->data);
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

static void tg_buf_grow(tg_buf_t *b, size_t needed)
{
  if (b->len + needed + 1 > b->cap) {
    size_t new_cap = b->cap ? b->cap : TG_BUF_INIT_CAP;
    while (new_cap < b->len + needed + 1)
      new_cap *= 2;
    b->data = (char *) realloc(b->data, new_cap);
    if (!b->data) {
      tg_log(TG_LOG_ERROR, "tg_buf_grow: out of memory");
      abort();
    }
    b->cap = new_cap;
  }
}

void tg_buf_append(tg_buf_t *b, const char *data, size_t len)
{
  tg_buf_grow(b, len);
  memcpy(b->data + b->len, data, len);
  b->len += len;
  b->data[b->len] = '\0';
}

void tg_buf_printf(tg_buf_t *b, const char *fmt, ...)
{
  va_list ap, ap2;
  int n;

  va_start(ap, fmt);
  va_copy(ap2, ap);
  n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);

  if (n < 0) {
    va_end(ap2);
    return;
  }

  tg_buf_grow(b, (size_t) n);
  vsnprintf(b->data + b->len, (size_t) n + 1, fmt, ap2);
  va_end(ap2);
  b->len += (size_t) n;
}

void tg_buf_reset(tg_buf_t *b)
{
  b->len = 0;
  if (b->data)
    b->data[0] = '\0';
}

char *tg_buf_strdup(const tg_buf_t *b)
{
  char *s;
  if (!b->data || b->len == 0) {
    s = (char *) malloc(1);
    if (s)
      s[0] = '\0';
    return s;
  }
  s = (char *) malloc(b->len + 1);
  if (s) {
    memcpy(s, b->data, b->len);
    s[b->len] = '\0';
  }
  return s;
}

/* ---- String utilities ---- */

char *tg_strdup(const char *s)
{
  size_t len;
  char *d;
  if (!s)
    return NULL;
  len = strlen(s);
  d = (char *) malloc(len + 1);
  if (d)
    memcpy(d, s, len + 1);
  return d;
}

int tg_starts_with(const char *s, const char *prefix)
{
  size_t slen, plen;
  if (!s || !prefix)
    return 0;
  slen = strlen(s);
  plen = strlen(prefix);
  if (plen > slen)
    return 0;
  return memcmp(s, prefix, plen) == 0;
}

int tg_ends_with(const char *s, const char *suffix)
{
  size_t slen, xlen;
  if (!s || !suffix)
    return 0;
  slen = strlen(s);
  xlen = strlen(suffix);
  if (xlen > slen)
    return 0;
  return memcmp(s + slen - xlen, suffix, xlen) == 0;
}

char *tg_trim(char *s)
{
  char *end;
  if (!s)
    return s;
  while (*s && isspace((unsigned char) *s))
    s++;
  if (*s == '\0')
    return s;
  end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char) *end))
    end--;
  end[1] = '\0';
  return s;
}

void tg_strlcpy(char *dst, const char *src, size_t size)
{
  size_t i;
  if (size == 0)
    return;
  for (i = 0; i + 1 < size && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

size_t tg_strlcat(char *dst, const char *src, size_t size)
{
  size_t dlen = strlen(dst);
  size_t slen = strlen(src);
  size_t i;
  if (dlen >= size)
    return size + slen;
  for (i = 0; i < slen && dlen + i + 1 < size; i++)
    dst[dlen + i] = src[i];
  dst[dlen + i] = '\0';
  return dlen + slen;
}

/* ---- Hex encoding/decoding ---- */

static const char hex_chars[] = "0123456789abcdef";

void tg_hex_encode(const uint8_t *data, size_t len, char *out)
{
  size_t i;
  for (i = 0; i < len; i++) {
    out[i * 2]     = hex_chars[(data[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex_chars[data[i] & 0x0F];
  }
  out[len * 2] = '\0';
}

static int hex_val(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int tg_hex_decode(const char *hex, size_t hex_len, uint8_t *out,
                  size_t out_size)
{
  size_t i;
  if (hex_len % 2 != 0)
    return -1;
  if (hex_len / 2 > out_size)
    return -1;
  for (i = 0; i < hex_len; i += 2) {
    int hi = hex_val(hex[i]);
    int lo = hex_val(hex[i + 1]);
    if (hi < 0 || lo < 0)
      return -1;
    out[i / 2] = (uint8_t)((hi << 4) | lo);
  }
  return (int)(hex_len / 2);
}

/* ---- URL decoding ---- */

int tg_url_decode(const char *src, size_t src_len, char *dst,
                  size_t dst_len)
{
  size_t si = 0, di = 0;
  while (si < src_len && di + 1 < dst_len) {
    if (src[si] == '%' && si + 2 < src_len) {
      int hi = hex_val(src[si + 1]);
      int lo = hex_val(src[si + 2]);
      if (hi >= 0 && lo >= 0) {
        dst[di++] = (char)((hi << 4) | lo);
        si += 3;
        continue;
      }
    }
    if (src[si] == '+') {
      dst[di++] = ' ';
    } else {
      dst[di++] = src[si];
    }
    si++;
  }
  dst[di] = '\0';
  return (int) di;
}

/* ---- Base64 encoding/decoding ---- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t tg_base64_encode(const uint8_t *data, size_t len, char *out,
                        size_t out_size)
{
  size_t i, j = 0;
  size_t needed = ((len + 2) / 3) * 4 + 1;
  if (out_size < needed)
    return 0;

  for (i = 0; i < len; i += 3) {
    uint32_t n = ((uint32_t) data[i]) << 16;
    if (i + 1 < len)
      n |= ((uint32_t) data[i + 1]) << 8;
    if (i + 2 < len)
      n |= (uint32_t) data[i + 2];
    out[j++] = b64_table[(n >> 18) & 63];
    out[j++] = b64_table[(n >> 12) & 63];
    out[j++] = (i + 1 < len) ? b64_table[(n >> 6) & 63] : '=';
    out[j++] = (i + 2 < len) ? b64_table[n & 63] : '=';
  }
  out[j] = '\0';
  return j;
}

static int b64_val(char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

int tg_base64_decode(const char *b64, size_t b64_len, uint8_t *out,
                     size_t out_size)
{
  size_t i, j = 0;

  /* Strip trailing padding and whitespace for length calculation */
  while (b64_len > 0 && (b64[b64_len - 1] == '=' ||
         b64[b64_len - 1] == '\n' || b64[b64_len - 1] == '\r'))
    b64_len--;

  for (i = 0; i < b64_len; i += 4) {
    int a = (i < b64_len)     ? b64_val(b64[i])     : 0;
    int b = (i + 1 < b64_len) ? b64_val(b64[i + 1]) : 0;
    int c = (i + 2 < b64_len) ? b64_val(b64[i + 2]) : 0;
    int d = (i + 3 < b64_len) ? b64_val(b64[i + 3]) : 0;
    uint32_t n;

    if (a < 0 || b < 0)
      return -1;

    n = ((uint32_t) a << 18) | ((uint32_t) b << 12) |
        ((uint32_t)(c >= 0 ? c : 0) << 6) |
        (uint32_t)(d >= 0 ? d : 0);

    if (j < out_size)
      out[j++] = (uint8_t)((n >> 16) & 0xFF);
    if (i + 2 < b64_len && c >= 0 && j < out_size)
      out[j++] = (uint8_t)((n >> 8) & 0xFF);
    if (i + 3 < b64_len && d >= 0 && j < out_size)
      out[j++] = (uint8_t)(n & 0xFF);
  }
  return (int) j;
}

/* ---- Random bytes helper ---- */

static void tg_get_random_bytes(uint8_t *buf, size_t num_bytes)
{
  size_t i;
#ifdef _WIN32
  {
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL,
                            CRYPT_VERIFYCONTEXT)) {
      CryptGenRandom(hProv, (DWORD) num_bytes, buf);
      CryptReleaseContext(hProv, 0);
      return;
    }
    /* Fallback: not cryptographically secure */
    srand((unsigned int)(time(NULL) ^ _getpid()));
    for (i = 0; i < num_bytes; i++)
      buf[i] = (uint8_t)(rand() & 0xFF);
  }
#else
  {
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
      if (fread(buf, 1, num_bytes, f) == num_bytes) {
        fclose(f);
        return;
      }
      fclose(f);
    }
    /* Fallback: not cryptographically secure */
    srand((unsigned int)(time(NULL) ^ getpid()));
    for (i = 0; i < num_bytes; i++)
      buf[i] = (uint8_t)(rand() & 0xFF);
  }
#endif
}

/* ---- Random token generation ---- */

void tg_random_token(char *out, size_t len)
{
  size_t num_bytes = (len + 1) / 2;
  uint8_t *buf;

  if (len == 0) {
    out[0] = '\0';
    return;
  }

  buf = (uint8_t *) malloc(num_bytes);
  if (!buf) {
    out[0] = '\0';
    return;
  }

  tg_get_random_bytes(buf, num_bytes);
  tg_hex_encode(buf, num_bytes, out);
  out[len] = '\0'; /* truncate if odd len */
  free(buf);
}

void tg_random_hex(char *out, size_t outsize)
{
  size_t hex_len;
  size_t num_bytes;
  uint8_t *buf;

  if (outsize == 0)
    return;
  if (outsize == 1) {
    out[0] = '\0';
    return;
  }

  /* outsize includes the null terminator */
  hex_len = outsize - 1;
  num_bytes = (hex_len + 1) / 2;

  buf = (uint8_t *) malloc(num_bytes);
  if (!buf) {
    out[0] = '\0';
    return;
  }

  tg_get_random_bytes(buf, num_bytes);
  tg_hex_encode(buf, num_bytes, out);
  out[hex_len] = '\0'; /* truncate to requested size */
  free(buf);
}

/* ---- Socket helpers ---- */

tg_socket_t tg_connect_tcp(const char *host, int port)
{
  tg_socket_t sock;
  struct addrinfo hints, *res, *rp;
  char port_str[16];
  int rc;

  snprintf(port_str, sizeof(port_str), "%d", port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  rc = getaddrinfo(host, port_str, &hints, &res);
  if (rc != 0) {
    tg_log(TG_LOG_ERROR, "getaddrinfo(%s:%d): %s", host, port,
           gai_strerror(rc));
    return TG_INVALID_SOCKET;
  }

  sock = TG_INVALID_SOCKET;
  for (rp = res; rp; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock == TG_INVALID_SOCKET)
      continue;
    if (connect(sock, rp->ai_addr, (int) rp->ai_addrlen) == 0)
      break;
    tg_socket_close(sock);
    sock = TG_INVALID_SOCKET;
  }

  freeaddrinfo(res);

  if (sock == TG_INVALID_SOCKET) {
    tg_log(TG_LOG_ERROR, "Failed to connect to %s:%d", host, port);
  }

  return sock;
}

int tg_socket_set_nonblocking(tg_socket_t sock)
{
#ifdef _WIN32
  unsigned long on = 1;
  return ioctlsocket(sock, FIONBIO, &on) == 0 ? 0 : -1;
#else
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags < 0)
    return -1;
  return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

void tg_socket_close(tg_socket_t sock)
{
  if (sock == TG_INVALID_SOCKET)
    return;
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

/* ---- Filesystem utilities ---- */

int tg_dir_exists(const char *path)
{
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  if (attr == INVALID_FILE_ATTRIBUTES)
    return 0;
  return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

/* ---- Sleep ---- */

void tg_sleep_ms(int ms)
{
#ifdef _WIN32
  Sleep((DWORD) ms);
#else
  usleep((useconds_t) ms * 1000);
#endif
}

/* ---- Platform init/cleanup ---- */

void tg_platform_init(void)
{
#ifdef _WIN32
  WSADATA wsaData;
  int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (err != 0) {
    tg_log(TG_LOG_ERROR, "WSAStartup failed: error %d", err);
  }
#endif
  /* No-op on Unix */
}

void tg_platform_cleanup(void)
{
#ifdef _WIN32
  WSACleanup();
#endif
  /* No-op on Unix */
}

/* ---- Thread abstraction ---- */

#ifdef _WIN32

typedef struct {
  void *(*fn)(void *);
  void *arg;
} tg_win_thread_ctx;

static DWORD WINAPI tg_win_thread_proc(LPVOID param)
{
  tg_win_thread_ctx *ctx = (tg_win_thread_ctx *) param;
  void *(*fn)(void *) = ctx->fn;
  void *arg = ctx->arg;
  free(ctx);
  fn(arg);
  return 0;
}

int tg_thread_create(tg_thread_t *t, void *(*fn)(void *), void *arg)
{
  tg_win_thread_ctx *ctx =
      (tg_win_thread_ctx *) malloc(sizeof(tg_win_thread_ctx));
  if (!ctx)
    return -1;
  ctx->fn = fn;
  ctx->arg = arg;
  *t = CreateThread(NULL, 0, tg_win_thread_proc, ctx, 0, NULL);
  if (*t == NULL) {
    free(ctx);
    return -1;
  }
  return 0;
}

void tg_thread_join(tg_thread_t t)
{
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
}

void tg_mutex_init(tg_mutex_t *m)
{
  InitializeCriticalSection(m);
}

void tg_mutex_lock(tg_mutex_t *m)
{
  EnterCriticalSection(m);
}

void tg_mutex_unlock(tg_mutex_t *m)
{
  LeaveCriticalSection(m);
}

void tg_mutex_destroy(tg_mutex_t *m)
{
  DeleteCriticalSection(m);
}

#else /* POSIX */

int tg_thread_create(tg_thread_t *t, void *(*fn)(void *), void *arg)
{
  return pthread_create(t, NULL, fn, arg);
}

void tg_thread_join(tg_thread_t t)
{
  pthread_join(t, NULL);
}

void tg_mutex_init(tg_mutex_t *m)
{
  pthread_mutex_init(m, NULL);
}

void tg_mutex_lock(tg_mutex_t *m)
{
  pthread_mutex_lock(m);
}

void tg_mutex_unlock(tg_mutex_t *m)
{
  pthread_mutex_unlock(m);
}

void tg_mutex_destroy(tg_mutex_t *m)
{
  pthread_mutex_destroy(m);
}

#endif
