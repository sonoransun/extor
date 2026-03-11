/* tg_config.c -- Bridge and transport configuration parsing for tor-gui
 *
 * Provides parsing, validation, formatting, and URI encoding of Tor
 * bridge lines and pluggable transport configuration.  Supports the
 * torbridge:// URI scheme for QR code bridge sharing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tg_config.h"
#include "tg_util.h"
#include "tg_json.h"

/* ---- Helpers ---- */

/** Return 1 if the string looks like an address (starts with a digit
 *  or '[' and contains ':'). */
static int
looks_like_address(const char *s)
{
  if (s[0] == '[')
    return 1;
  if (!isdigit((unsigned char)s[0]))
    return 0;
  return strchr(s, ':') != NULL;
}

/** Return 1 if the string is exactly 40 hex characters. */
static int
is_hex40(const char *s)
{
  int i;
  for (i = 0; i < 40; i++) {
    if (!isxdigit((unsigned char)s[i]))
      return 0;
  }
  return s[40] == '\0' || isspace((unsigned char)s[40]);
}

/* ---- Bridge line parsing ---- */

int
tg_bridge_parse(const char *line, tg_bridge_t *bridge)
{
  char trimmed[1024];
  const char *tokens[64];
  size_t token_lens[64];
  int ntokens, idx;
  const char *p;

  if (!line || !bridge)
    return -1;

  memset(bridge, 0, sizeof(*bridge));
  bridge->enabled = 1;

  /* Copy and trim the line */
  tg_strlcpy(trimmed, line, sizeof(trimmed));
  {
    char *t = tg_trim(trimmed);
    if (t != trimmed)
      memmove(trimmed, t, strlen(t) + 1);
  }

  /* Skip empty and comment lines */
  if (trimmed[0] == '\0' || trimmed[0] == '#')
    return -1;

  /* Split on whitespace tokens */
  ntokens = 0;
  p = trimmed;
  while (*p && ntokens < 64) {
    while (*p && isspace((unsigned char)*p))
      p++;
    if (*p == '\0')
      break;
    tokens[ntokens] = p;
    while (*p && !isspace((unsigned char)*p))
      p++;
    token_lens[ntokens] = (size_t)(p - tokens[ntokens]);
    ntokens++;
  }

  if (ntokens == 0)
    return -1;

  idx = 0;

  /* First token: transport name or address.
   * If it contains ':' and starts with a digit or '[', it is the
   * address (vanilla bridge, no transport).  Otherwise it is the
   * transport name and the next token is the address. */
  {
    char first[256];
    size_t flen = token_lens[0];
    if (flen >= sizeof(first))
      flen = sizeof(first) - 1;
    memcpy(first, tokens[0], flen);
    first[flen] = '\0';

    if (looks_like_address(first)) {
      bridge->transport[0] = '\0';
    } else {
      tg_strlcpy(bridge->transport, first,
                  sizeof(bridge->transport));
      idx++;
    }
  }

  /* Next token: address (IP:Port) */
  if (idx >= ntokens)
    return -1;
  {
    char addr[256];
    size_t alen = token_lens[idx];
    if (alen >= sizeof(addr))
      alen = sizeof(addr) - 1;
    memcpy(addr, tokens[idx], alen);
    addr[alen] = '\0';
    tg_strlcpy(bridge->address, addr, sizeof(bridge->address));
    idx++;
  }

  /* Next token: fingerprint (exactly 40 hex chars) */
  if (idx < ntokens) {
    char tok[256];
    size_t tlen = token_lens[idx];
    if (tlen >= sizeof(tok))
      tlen = sizeof(tok) - 1;
    memcpy(tok, tokens[idx], tlen);
    tok[tlen] = '\0';
    if (is_hex40(tok)) {
      tg_strlcpy(bridge->fingerprint, tok,
                  sizeof(bridge->fingerprint));
      idx++;
    }
  }

  /* Remaining tokens joined with spaces: transport args */
  if (idx < ntokens) {
    bridge->args[0] = '\0';
    size_t args_used = 0;
    for (int i = idx; i < ntokens; i++) {
      if (args_used > 0 && args_used < sizeof(bridge->args) - 1)
        bridge->args[args_used++] = ' ';
      size_t copylen = token_lens[i];
      if (args_used + copylen >= sizeof(bridge->args))
        copylen = sizeof(bridge->args) - args_used - 1;
      memcpy(bridge->args + args_used, tokens[i], copylen);
      args_used += copylen;
    }
    bridge->args[args_used] = '\0';
  }

  return 0;
}

/* ---- Bridge line formatting ---- */

void
tg_bridge_format(const tg_bridge_t *bridge, char *out, size_t outsize)
{
  size_t pos;

  if (!bridge || !out || outsize == 0)
    return;

  out[0] = '\0';
  pos = 0;

  /* [transport ]address[ fingerprint][ args] */
  if (bridge->transport[0]) {
    int n = snprintf(out + pos, outsize - pos, "%s ",
                     bridge->transport);
    if (n > 0)
      pos += (size_t)n;
  }

  {
    int n = snprintf(out + pos, outsize - pos, "%s",
                     bridge->address);
    if (n > 0)
      pos += (size_t)n;
  }

  if (bridge->fingerprint[0]) {
    int n = snprintf(out + pos, outsize - pos, " %s",
                     bridge->fingerprint);
    if (n > 0)
      pos += (size_t)n;
  }

  if (bridge->args[0]) {
    int n = snprintf(out + pos, outsize - pos, " %s",
                     bridge->args);
    if (n > 0)
      pos += (size_t)n;
  }
}

/* ---- torbridge:// URI parsing ---- */

tg_bridge_t *
tg_bridge_parse_uri(const char *uri, int *count)
{
  const char *prefix = "torbridge://1/";
  size_t plen;
  const char *b64_src;
  size_t b64_len;
  char *b64_std;
  uint8_t decoded[8192];
  int dec_len;
  const char *json;
  tg_bridge_t *bridges;
  int n;
  const char *arr_start, *pos;

  if (!uri || !count)
    return NULL;
  *count = 0;

  /* Check prefix */
  plen = strlen(prefix);
  if (strncmp(uri, prefix, plen) != 0)
    return NULL;

  /* Convert base64url to standard base64: '-' -> '+', '_' -> '/' */
  b64_src = uri + plen;
  b64_len = strlen(b64_src);
  b64_std = (char *)malloc(b64_len + 4); /* room for padding */
  if (!b64_std)
    return NULL;
  for (size_t i = 0; i < b64_len; i++) {
    if (b64_src[i] == '-')
      b64_std[i] = '+';
    else if (b64_src[i] == '_')
      b64_std[i] = '/';
    else
      b64_std[i] = b64_src[i];
  }
  /* Add padding if needed */
  {
    size_t pad = (4 - (b64_len % 4)) % 4;
    for (size_t i = 0; i < pad; i++)
      b64_std[b64_len + i] = '=';
    b64_std[b64_len + pad] = '\0';
    b64_len += pad;
  }

  /* Decode base64 */
  dec_len = tg_base64_decode(b64_std, b64_len, decoded,
                             sizeof(decoded) - 1);
  free(b64_std);
  if (dec_len <= 0)
    return NULL;
  decoded[dec_len] = '\0';

  json = (const char *)decoded;

  /* Find "bridges" array */
  arr_start = strstr(json, "\"bridges\"");
  if (!arr_start)
    return NULL;
  arr_start = strchr(arr_start, '[');
  if (!arr_start)
    return NULL;

  bridges = (tg_bridge_t *)malloc(sizeof(tg_bridge_t) * 64);
  if (!bridges)
    return NULL;

  n = 0;
  pos = arr_start + 1;

  /* Scan for each bridge object in the array */
  while (n < 64) {
    const char *obj_start = strchr(pos, '{');
    const char *obj_end;
    tg_bridge_t *br;
    size_t obj_len;

    if (!obj_start)
      break;
    obj_end = strchr(obj_start + 1, '}');
    if (!obj_end)
      break;
    obj_end++; /* include the '}' */
    obj_len = (size_t)(obj_end - obj_start);

    br = &bridges[n];
    memset(br, 0, sizeof(*br));
    br->enabled = 1;

    /* Extract "t" (transport) */
    {
      char *t = tg_json_get_str(obj_start, obj_len, "t");
      if (t) {
        tg_strlcpy(br->transport, t, sizeof(br->transport));
        free(t);
      }
    }

    /* Extract "a" (address) */
    {
      char *a = tg_json_get_str(obj_start, obj_len, "a");
      if (a) {
        /* Extract "p" (port) */
        long long p_val = tg_json_get_int(obj_start, obj_len, "p");
        if (p_val > 0)
          snprintf(br->address, sizeof(br->address),
                   "%s:%lld", a, p_val);
        else
          tg_strlcpy(br->address, a, sizeof(br->address));
        free(a);
      }
    }

    /* Extract "f" (fingerprint) */
    {
      char *f = tg_json_get_str(obj_start, obj_len, "f");
      if (f) {
        tg_strlcpy(br->fingerprint, f,
                    sizeof(br->fingerprint));
        free(f);
      }
    }

    /* Extract "args" object as key=value pairs */
    {
      const char *argp = strstr(obj_start, "\"args\"");
      if (argp && argp < obj_end) {
        const char *ab = strchr(argp + 6, '{');
        if (ab && ab < obj_end) {
          ab++;
          const char *ae = strchr(ab, '}');
          if (ae && ae <= obj_end) {
            br->args[0] = '\0';
            size_t args_used = 0;
            const char *kp = ab;
            while (kp < ae) {
              const char *kq1 = strchr(kp, '"');
              if (!kq1 || kq1 >= ae) break;
              kq1++;
              const char *kq2 = strchr(kq1, '"');
              if (!kq2 || kq2 >= ae) break;

              const char *vq1 = strchr(kq2 + 1, '"');
              if (!vq1 || vq1 >= ae) break;
              vq1++;
              const char *vq2 = strchr(vq1, '"');
              if (!vq2 || vq2 > ae) break;

              if (args_used > 0 &&
                  args_used < sizeof(br->args) - 1)
                br->args[args_used++] = ' ';

              size_t klen = (size_t)(kq2 - kq1);
              size_t vlen = (size_t)(vq2 - vq1);
              if (args_used + klen + 1 + vlen <
                  sizeof(br->args) - 1) {
                memcpy(br->args + args_used, kq1, klen);
                args_used += klen;
                br->args[args_used++] = '=';
                memcpy(br->args + args_used, vq1, vlen);
                args_used += vlen;
              }
              br->args[args_used] = '\0';

              kp = vq2 + 1;
            }
          }
        }
      }
    }

    /* Only count if we got at least an address */
    if (br->address[0])
      n++;

    pos = obj_end;
  }

  *count = n;
  if (n == 0) {
    free(bridges);
    return NULL;
  }
  return bridges;
}

/* ---- torbridge:// URI formatting ---- */

char *
tg_bridge_format_uri(const tg_bridge_t *bridges, int count)
{
  tg_json_t j;
  const char *json_str;
  size_t json_len;
  char b64_buf[16384];
  size_t b64_len;
  char *uri;
  size_t uri_len;

  if (!bridges || count <= 0)
    return NULL;

  /* Build JSON using tg_json builder */
  tg_json_init(&j);
  tg_json_obj_open(&j);

  tg_json_kv_int(&j, "v", 1);
  tg_json_key(&j, "bridges");
  tg_json_arr_open(&j);

  for (int i = 0; i < count; i++) {
    const tg_bridge_t *br = &bridges[i];
    char ip[256] = "";
    int port = 0;
    const char *colon = strrchr(br->address, ':');

    if (colon) {
      size_t iplen = (size_t)(colon - br->address);
      if (iplen >= sizeof(ip))
        iplen = sizeof(ip) - 1;
      memcpy(ip, br->address, iplen);
      ip[iplen] = '\0';
      port = atoi(colon + 1);
    } else {
      tg_strlcpy(ip, br->address, sizeof(ip));
    }

    tg_json_obj_open(&j);
    tg_json_kv_str(&j, "t", br->transport);
    tg_json_kv_str(&j, "a", ip);
    tg_json_kv_int(&j, "p", port);

    if (br->fingerprint[0])
      tg_json_kv_str(&j, "f", br->fingerprint);

    if (br->args[0]) {
      char args_copy[512];
      char *saveptr = NULL;
      char *tok;

      tg_json_key(&j, "args");
      tg_json_obj_open(&j);

      tg_strlcpy(args_copy, br->args, sizeof(args_copy));
      tok = strtok_r(args_copy, " ", &saveptr);
      while (tok) {
        char *eq = strchr(tok, '=');
        if (eq) {
          *eq = '\0';
          tg_json_kv_str(&j, tok, eq + 1);
        }
        tok = strtok_r(NULL, " ", &saveptr);
      }

      tg_json_obj_close(&j);
    }

    tg_json_obj_close(&j);
  }

  tg_json_arr_close(&j);
  tg_json_obj_close(&j);

  json_str = tg_json_finish(&j);
  json_len = strlen(json_str);

  /* Base64-encode the JSON */
  b64_len = tg_base64_encode((const uint8_t *)json_str, json_len,
                             b64_buf, sizeof(b64_buf));
  tg_json_free(&j);

  if (b64_len == 0)
    return NULL;

  /* Convert standard base64 to base64url: '+' -> '-', '/' -> '_',
   * strip '=' padding */
  {
    size_t out = 0;
    for (size_t k = 0; k < b64_len; k++) {
      if (b64_buf[k] == '+')
        b64_buf[out++] = '-';
      else if (b64_buf[k] == '/')
        b64_buf[out++] = '_';
      else if (b64_buf[k] == '=')
        continue;
      else
        b64_buf[out++] = b64_buf[k];
    }
    b64_buf[out] = '\0';
    b64_len = out;
  }

  /* Build URI: "torbridge://1/" + base64url */
  uri_len = strlen("torbridge://1/") + b64_len + 1;
  uri = (char *)malloc(uri_len);
  if (!uri)
    return NULL;
  snprintf(uri, uri_len, "torbridge://1/%s", b64_buf);
  return uri;
}

/* ---- Bridge validation ---- */

const char *
tg_bridge_validate(const tg_bridge_t *bridge)
{
  if (!bridge)
    return "NULL bridge";

  /* Transport: must be empty or alphanumeric+underscore, max 63 */
  if (bridge->transport[0]) {
    size_t tlen = strlen(bridge->transport);
    if (tlen > 63)
      return "Transport name too long (max 63 chars)";
    for (const char *p = bridge->transport; *p; p++) {
      if (!isalnum((unsigned char)*p) && *p != '_')
        return "Transport name must be alphanumeric/underscore";
    }
  }

  /* Address: must contain ':', port part must be 1-65535 */
  if (!bridge->address[0])
    return "Address is required";
  {
    const char *colon = strrchr(bridge->address, ':');
    int port;
    if (!colon)
      return "Address must be in IP:Port format";
    port = atoi(colon + 1);
    if (port < 1 || port > 65535)
      return "Port must be between 1 and 65535";
  }

  /* Fingerprint: must be empty or exactly 40 hex chars */
  if (bridge->fingerprint[0]) {
    size_t flen = strlen(bridge->fingerprint);
    if (flen != 40)
      return "Fingerprint must be exactly 40 hex characters";
    for (size_t i = 0; i < 40; i++) {
      if (!isxdigit((unsigned char)bridge->fingerprint[i]))
        return "Fingerprint must contain only hex characters";
    }
  }

  /* Args: length must be <= 510 */
  if (strlen(bridge->args) > 510)
    return "Transport args too long (max 510 bytes)";

  return NULL;
}

/* ---- SETCONF command formatting ---- */

char *
tg_bridge_format_setconf(const tg_bridge_t *bridges, int count)
{
  tg_buf_t buf;
  int any;

  if (!bridges)
    return NULL;

  tg_buf_init(&buf);
  tg_buf_printf(&buf, "SETCONF");

  any = 0;
  for (int i = 0; i < count; i++) {
    char line[1024];
    char escaped[2048];
    size_t ei;

    if (!bridges[i].enabled)
      continue;

    tg_bridge_format(&bridges[i], line, sizeof(line));

    /* Escape double quotes in the bridge line */
    ei = 0;
    for (size_t li = 0; line[li] && ei < sizeof(escaped) - 2;
         li++) {
      if (line[li] == '"')
        escaped[ei++] = '\\';
      escaped[ei++] = line[li];
    }
    escaped[ei] = '\0';

    tg_buf_printf(&buf, " Bridge=\"%s\"", escaped);
    any = 1;
  }

  /* If no enabled bridges, reset the Bridge config */
  if (!any)
    tg_buf_printf(&buf, " Bridge");

  {
    char *result = tg_buf_strdup(&buf);
    tg_buf_free(&buf);
    return result;
  }
}
