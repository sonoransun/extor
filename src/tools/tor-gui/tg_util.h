#ifndef TG_UTIL_H
#define TG_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Platform abstraction */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET tg_socket_t;
#define TG_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
typedef int tg_socket_t;
#define TG_INVALID_SOCKET (-1)
#endif

/* Dynamic string buffer */
typedef struct tg_buf {
  char *data;
  size_t len;
  size_t cap;
} tg_buf_t;

void tg_buf_init(tg_buf_t *b);
void tg_buf_free(tg_buf_t *b);
void tg_buf_append(tg_buf_t *b, const char *data, size_t len);
void tg_buf_printf(tg_buf_t *b, const char *fmt, ...);
void tg_buf_reset(tg_buf_t *b);
#define tg_buf_clear tg_buf_reset
char *tg_buf_strdup(const tg_buf_t *b);

/* String utilities */
char *tg_strdup(const char *s);
int tg_starts_with(const char *s, const char *prefix);
int tg_ends_with(const char *s, const char *suffix);
char *tg_trim(char *s);
void tg_strlcpy(char *dst, const char *src, size_t size);
size_t tg_strlcat(char *dst, const char *src, size_t size);

/* Hex encoding/decoding */
void tg_hex_encode(const uint8_t *data, size_t len, char *out);
int tg_hex_decode(const char *hex, size_t hex_len, uint8_t *out,
                  size_t out_size);

/* URL decoding */
int tg_url_decode(const char *src, size_t src_len, char *dst,
                  size_t dst_len);

/* Base64 encoding */
size_t tg_base64_encode(const uint8_t *data, size_t len, char *out,
                        size_t out_size);
int tg_base64_decode(const char *b64, size_t b64_len, uint8_t *out,
                     size_t out_size);

/* Random token generation */
void tg_random_token(char *out, size_t len);
void tg_random_hex(char *out, size_t outsize);

/* Socket helpers */
tg_socket_t tg_connect_tcp(const char *host, int port);
int tg_socket_set_nonblocking(tg_socket_t sock);
void tg_socket_close(tg_socket_t sock);

/* Logging */
#define TG_LOG_ERROR   0
#define TG_LOG_WARN    1
#define TG_LOG_INFO    2
#define TG_LOG_DEBUG   3

void tg_log(int level, const char *fmt, ...);
void tg_set_log_level(int level);
#define tg_log_set_level tg_set_log_level

/* Filesystem utilities */
int tg_dir_exists(const char *path);

/* Sleep */
void tg_sleep_ms(int ms);

/* Platform init/cleanup (WSAStartup on Windows, no-op elsewhere) */
void tg_platform_init(void);
void tg_platform_cleanup(void);

/* Thread abstraction */
#ifdef _WIN32
#include <windows.h>
typedef HANDLE tg_thread_t;
typedef CRITICAL_SECTION tg_mutex_t;
#else
#include <pthread.h>
typedef pthread_t tg_thread_t;
typedef pthread_mutex_t tg_mutex_t;
#endif

int tg_thread_create(tg_thread_t *t, void *(*fn)(void *), void *arg);

/* Thread function helper macros */
#ifdef _WIN32
#define TG_THREAD_FUNC(name, arg) DWORD WINAPI name(LPVOID arg)
#define TG_THREAD_RETURN 0
#else
#define TG_THREAD_FUNC(name, arg) void *name(void *arg)
#define TG_THREAD_RETURN NULL
#endif
void tg_thread_join(tg_thread_t t);
void tg_mutex_init(tg_mutex_t *m);
void tg_mutex_lock(tg_mutex_t *m);
void tg_mutex_unlock(tg_mutex_t *m);
void tg_mutex_destroy(tg_mutex_t *m);

#endif /* TG_UTIL_H */
