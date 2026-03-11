#ifndef TG_CONFIG_H
#define TG_CONFIG_H

#include <stddef.h>

/* Bridge line representation */
typedef struct tg_bridge {
  char transport[64];       /* e.g., "obfs4", "" for vanilla */
  char address[256];        /* IP:Port */
  char fingerprint[42];     /* 40 hex chars + null */
  char args[512];           /* Transport args: k=v k=v ... */
  int enabled;
} tg_bridge_t;

/* Parse a bridge line into struct. Returns 0 on success */
int tg_bridge_parse(const char *line, tg_bridge_t *bridge);

/* Format a bridge struct back to a bridge line */
void tg_bridge_format(const tg_bridge_t *bridge, char *out, size_t outsize);

/* Parse torbridge:// URI. Returns array of bridges (caller frees).
 * Sets *count to number of bridges parsed. */
tg_bridge_t *tg_bridge_parse_uri(const char *uri, int *count);

/* Format bridges as torbridge:// URI. Caller frees result. */
char *tg_bridge_format_uri(const tg_bridge_t *bridges, int count);

/* Validate bridge fields. Returns NULL on success, error message on
 * failure. */
const char *tg_bridge_validate(const tg_bridge_t *bridge);

/* Format SETCONF command for multiple bridges. Caller frees result. */
char *tg_bridge_format_setconf(const tg_bridge_t *bridges, int count);

#endif /* TG_CONFIG_H */
