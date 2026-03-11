/* tor-gui JSON builder and parser implementation */

#include "tg_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ================================================================
 * JSON Builder
 * ================================================================ */

void
tg_json_init(tg_json_t *j)
{
  tg_buf_init(&j->buf);
  j->depth = 0;
  memset(j->count, 0, sizeof(j->count));
}

void
tg_json_free(tg_json_t *j)
{
  tg_buf_free(&j->buf);
  j->depth = 0;
  memset(j->count, 0, sizeof(j->count));
}

const char *
tg_json_finish(tg_json_t *j)
{
  /* Null-terminate the buffer contents */
  if (j->buf.data) {
    /* Ensure null termination by appending a NUL if not already present */
    if (j->buf.len == 0 || j->buf.data[j->buf.len - 1] != '\0') {
      tg_buf_append(&j->buf, "\0", 1);
      /* The NUL is part of the buffer but we don't want it counted
       * as content length for future appends, so back up len. */
      j->buf.len--;
    }
    return j->buf.data;
  }
  return "";
}

/* Add comma separator if needed (between elements at same depth).
 * Called before emitting an element that is NOT preceded by a key. */
static void
json_sep(tg_json_t *j)
{
  if (j->depth > 0 && j->depth < 32 && j->count[j->depth] > 0) {
    tg_buf_append(&j->buf, ",", 1);
  }
}

/* Increment element count at current depth. Called after emitting a
 * complete value so the next sibling knows to prepend a comma. */
static void
json_inc(tg_json_t *j)
{
  if (j->depth > 0 && j->depth < 32)
    j->count[j->depth]++;
}

/* Return 1 if the last character in the buffer is ':', meaning a key
 * was just written and the upcoming value should NOT emit a comma. */
static int
json_after_key(const tg_json_t *j)
{
  return (j->buf.len > 0 && j->buf.data[j->buf.len - 1] == ':');
}

/* ----------------------------------------------------------------
 * Structure: objects and arrays
 * ---------------------------------------------------------------- */

void
tg_json_obj_open(tg_json_t *j)
{
  if (!json_after_key(j))
    json_sep(j);
  tg_buf_append(&j->buf, "{", 1);
  j->depth++;
  if (j->depth < 32)
    j->count[j->depth] = 0;
}

void
tg_json_obj_close(tg_json_t *j)
{
  tg_buf_append(&j->buf, "}", 1);
  if (j->depth > 0)
    j->depth--;
  json_inc(j);
}

void
tg_json_arr_open(tg_json_t *j)
{
  if (!json_after_key(j))
    json_sep(j);
  tg_buf_append(&j->buf, "[", 1);
  j->depth++;
  if (j->depth < 32)
    j->count[j->depth] = 0;
}

void
tg_json_arr_close(tg_json_t *j)
{
  tg_buf_append(&j->buf, "]", 1);
  if (j->depth > 0)
    j->depth--;
  json_inc(j);
}

/* ----------------------------------------------------------------
 * String escaping (builder side)
 * ---------------------------------------------------------------- */

/** Escape and write a JSON string (including surrounding quotes).
 * Handles: \", \\, \n, \r, \t, \b, \f, and control chars as \uXXXX. */
static void
json_write_escaped(tg_json_t *j, const char *s)
{
  const unsigned char *p;

  tg_buf_append(&j->buf, "\"", 1);
  if (s) {
    for (p = (const unsigned char *)s; *p; p++) {
      switch (*p) {
        case '"':
          tg_buf_append(&j->buf, "\\\"", 2);
          break;
        case '\\':
          tg_buf_append(&j->buf, "\\\\", 2);
          break;
        case '\b':
          tg_buf_append(&j->buf, "\\b", 2);
          break;
        case '\f':
          tg_buf_append(&j->buf, "\\f", 2);
          break;
        case '\n':
          tg_buf_append(&j->buf, "\\n", 2);
          break;
        case '\r':
          tg_buf_append(&j->buf, "\\r", 2);
          break;
        case '\t':
          tg_buf_append(&j->buf, "\\t", 2);
          break;
        default:
          if (*p < 0x20) {
            /* Control characters encoded as \uXXXX */
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)*p);
            tg_buf_append(&j->buf, esc, 6);
          } else {
            tg_buf_append(&j->buf, (const char *)p, 1);
          }
          break;
      }
    }
  }
  tg_buf_append(&j->buf, "\"", 1);
}

/* ----------------------------------------------------------------
 * Primitive value emitters
 * ---------------------------------------------------------------- */

void
tg_json_key(tg_json_t *j, const char *key)
{
  json_sep(j);
  json_write_escaped(j, key);
  tg_buf_append(&j->buf, ":", 1);
  /* Do NOT call json_inc here. The value that follows the key will
   * call json_inc, which counts the key-value pair as one element. */
}

void
tg_json_str(tg_json_t *j, const char *val)
{
  if (!json_after_key(j))
    json_sep(j);
  json_write_escaped(j, val);
  json_inc(j);
}

void
tg_json_int(tg_json_t *j, long long val)
{
  if (!json_after_key(j))
    json_sep(j);
  tg_buf_printf(&j->buf, "%lld", val);
  json_inc(j);
}

void
tg_json_double(tg_json_t *j, double val)
{
  if (!json_after_key(j))
    json_sep(j);
  tg_buf_printf(&j->buf, "%g", val);
  json_inc(j);
}

void
tg_json_bool(tg_json_t *j, int val)
{
  if (!json_after_key(j))
    json_sep(j);
  if (val)
    tg_buf_append(&j->buf, "true", 4);
  else
    tg_buf_append(&j->buf, "false", 5);
  json_inc(j);
}

void
tg_json_null(tg_json_t *j)
{
  if (!json_after_key(j))
    json_sep(j);
  tg_buf_append(&j->buf, "null", 4);
  json_inc(j);
}

void
tg_json_raw(tg_json_t *j, const char *raw)
{
  if (!json_after_key(j))
    json_sep(j);
  if (raw)
    tg_buf_append(&j->buf, raw, strlen(raw));
  json_inc(j);
}

/* ----------------------------------------------------------------
 * Convenience key-value helpers
 * ---------------------------------------------------------------- */

void
tg_json_kv_str(tg_json_t *j, const char *key, const char *val)
{
  tg_json_key(j, key);
  tg_json_str(j, val);
}

void
tg_json_kv_int(tg_json_t *j, const char *key, long long val)
{
  tg_json_key(j, key);
  tg_json_int(j, val);
}

void
tg_json_kv_bool(tg_json_t *j, const char *key, int val)
{
  tg_json_key(j, key);
  tg_json_bool(j, val);
}

void
tg_json_kv_double(tg_json_t *j, const char *key, double val)
{
  tg_json_key(j, key);
  tg_json_double(j, val);
}

/* ================================================================
 * JSON Parser
 * ================================================================ */

/* ----------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------- */

/** Skip whitespace characters (space, tab, newline, carriage return). */
static const char *
json_skip_ws(const char *p, const char *end)
{
  while (p < end && (*p == ' ' || *p == '\t' ||
                     *p == '\n' || *p == '\r'))
    p++;
  return p;
}

/** Skip a JSON string starting at the opening '"'.
 * Returns a pointer just past the closing '"'. */
static const char *
json_skip_string(const char *p, const char *end)
{
  if (p >= end || *p != '"')
    return p;
  p++; /* skip opening quote */
  while (p < end) {
    if (*p == '\\') {
      p += 2; /* skip escape sequence */
      continue;
    }
    if (*p == '"') {
      return p + 1; /* past closing quote */
    }
    p++;
  }
  return p; /* unterminated string */
}

/** Skip an entire JSON value (object, array, string, number, boolean,
 * or null).  Returns pointer past the end of the value.
 *
 * For objects and arrays, properly tracks nesting depth and handles
 * strings inside them (so that braces/brackets within strings are
 * not mistakenly counted as structure delimiters). */
static const char *
skip_json_value(const char *p, const char *end)
{
  p = json_skip_ws(p, end);
  if (p >= end)
    return p;

  switch (*p) {

    /* --- String --- */
    case '"':
      return json_skip_string(p, end);

    /* --- Object --- */
    case '{': {
      int depth = 1;
      p++;
      while (p < end && depth > 0) {
        if (*p == '"') {
          p = json_skip_string(p, end);
          continue;
        }
        if (*p == '{')
          depth++;
        else if (*p == '}')
          depth--;
        if (depth > 0)
          p++;
      }
      if (p < end)
        p++; /* skip final '}' */
      return p;
    }

    /* --- Array --- */
    case '[': {
      int depth = 1;
      p++;
      while (p < end && depth > 0) {
        if (*p == '"') {
          p = json_skip_string(p, end);
          continue;
        }
        if (*p == '[')
          depth++;
        else if (*p == ']')
          depth--;
        if (depth > 0)
          p++;
      }
      if (p < end)
        p++; /* skip final ']' */
      return p;
    }

    /* --- true --- */
    case 't':
      if (p + 4 <= end && memcmp(p, "true", 4) == 0)
        return p + 4;
      return p + 1;

    /* --- false --- */
    case 'f':
      if (p + 5 <= end && memcmp(p, "false", 5) == 0)
        return p + 5;
      return p + 1;

    /* --- null --- */
    case 'n':
      if (p + 4 <= end && memcmp(p, "null", 4) == 0)
        return p + 4;
      return p + 1;

    /* --- Number --- */
    default:
      if (*p == '-' || (*p >= '0' && *p <= '9')) {
        while (p < end &&
               (*p == '-' || *p == '+' || *p == '.' ||
                *p == 'e' || *p == 'E' ||
                (*p >= '0' && *p <= '9')))
          p++;
        return p;
      }
      /* Unknown character -- advance past it to avoid infinite loop */
      return p + 1;
  }
}

/** Compare a JSON string token (which includes the surrounding quotes)
 * with a plain C string key.  Returns 1 on match, 0 otherwise.
 *
 * This is a simple byte-for-byte comparison and does NOT handle
 * escape sequences inside the JSON key.  That is sufficient for the
 * plain ASCII keys used by the Tor control protocol. */
static int
json_key_match(const char *json_str, size_t json_len, const char *key)
{
  size_t klen = strlen(key);
  /* json_str starts and ends with '"' */
  if (json_len < 2)
    return 0;
  if (json_len - 2 != klen)
    return 0;
  return memcmp(json_str + 1, key, klen) == 0;
}

/* ----------------------------------------------------------------
 * Public parser API
 * ---------------------------------------------------------------- */

/** Search a top-level JSON object for <key> and return a pointer to
 * the start of the corresponding value.  On success *vlen is set to
 * the byte length of the value (string including its quotes, entire
 * object/array including delimiters, literal for number/bool/null).
 *
 * Only searches the outermost object -- does not recurse into nested
 * structures.
 *
 * Returns NULL if the key is not found or the JSON is malformed. */
const char *
tg_json_find(const char *json, size_t json_len,
             const char *key, size_t *vlen)
{
  const char *p = json;
  const char *end = json + json_len;

  if (!json || json_len == 0 || !key)
    return NULL;

  p = json_skip_ws(p, end);
  if (p >= end || *p != '{')
    return NULL;
  p++; /* skip opening '{' */

  while (p < end) {
    const char *key_start, *key_end;
    const char *val_start, *val_end;

    p = json_skip_ws(p, end);
    if (p >= end || *p == '}')
      return NULL;

    /* Skip comma between key-value pairs */
    if (*p == ',') {
      p++;
      continue;
    }

    /* --- Parse key string --- */
    if (*p != '"')
      return NULL; /* malformed */
    key_start = p;
    key_end = json_skip_string(p, end);
    if (key_end <= key_start)
      return NULL;

    /* --- Skip colon --- */
    p = json_skip_ws(key_end, end);
    if (p >= end || *p != ':')
      return NULL;
    p++;

    /* --- Locate value --- */
    p = json_skip_ws(p, end);
    val_start = p;
    val_end = skip_json_value(p, end);

    /* --- Check for match --- */
    if (json_key_match(key_start,
                       (size_t)(key_end - key_start), key)) {
      if (vlen)
        *vlen = (size_t)(val_end - val_start);
      return val_start;
    }

    p = val_end;
  }

  return NULL;
}

/* ----------------------------------------------------------------
 * String unescaping (parser side)
 * ---------------------------------------------------------------- */

/** Unescape a JSON string value.  The input <s> must point at the
 * opening '"' and <len> is the total length including both quotes.
 *
 * Returns a malloc'd NUL-terminated string with all JSON escape
 * sequences resolved:
 *   \"  \\  \/  \b  \f  \n  \r  \t  \uXXXX (encoded as UTF-8)
 *
 * Returns NULL on allocation failure or if the input is not a valid
 * JSON string token. */
static char *
json_unescape(const char *s, size_t len)
{
  char *out;
  size_t i, j;

  if (len < 2 || s[0] != '"')
    return NULL;

  /* Allocate worst-case size (original length is always enough) */
  out = (char *)malloc(len);
  if (!out)
    return NULL;

  j = 0;
  for (i = 1; i < len; i++) {
    if (s[i] == '"')
      break; /* closing quote */

    if (s[i] == '\\' && i + 1 < len) {
      i++;
      switch (s[i]) {
        case '"':  out[j++] = '"';  break;
        case '\\': out[j++] = '\\'; break;
        case '/':  out[j++] = '/';  break;
        case 'b':  out[j++] = '\b'; break;
        case 'f':  out[j++] = '\f'; break;
        case 'n':  out[j++] = '\n'; break;
        case 'r':  out[j++] = '\r'; break;
        case 't':  out[j++] = '\t'; break;
        case 'u': {
          /* Parse exactly 4 hex digits and encode as UTF-8 */
          unsigned int cp = 0;
          int k;
          for (k = 0; k < 4 && i + 1 + k < len; k++) {
            char c = s[i + 1 + k];
            cp <<= 4;
            if (c >= '0' && c <= '9')
              cp |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
              cp |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
              cp |= (unsigned)(c - 'A' + 10);
            else
              break; /* invalid hex digit */
          }
          i += k; /* advance past the hex digits we consumed */

          /* Encode code point as UTF-8 */
          if (cp < 0x80) {
            out[j++] = (char)cp;
          } else if (cp < 0x800) {
            out[j++] = (char)(0xC0 | (cp >> 6));
            out[j++] = (char)(0x80 | (cp & 0x3F));
          } else {
            out[j++] = (char)(0xE0 | (cp >> 12));
            out[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[j++] = (char)(0x80 | (cp & 0x3F));
          }
          break;
        }
        default:
          /* Unknown escape -- pass through literally */
          out[j++] = s[i];
          break;
      }
    } else {
      out[j++] = s[i];
    }
  }
  out[j] = '\0';
  return out;
}

/* ----------------------------------------------------------------
 * Typed value extractors
 * ---------------------------------------------------------------- */

/** Find <key> in <json> and return its string value (unescaped,
 * malloc'd).  Returns NULL if the key is not found or the value is
 * not a string. */
char *
tg_json_get_str(const char *json, size_t json_len, const char *key)
{
  size_t vlen;
  const char *v = tg_json_find(json, json_len, key, &vlen);
  if (!v || vlen < 2 || v[0] != '"')
    return NULL;
  return json_unescape(v, vlen);
}

/** Find <key> in <json> and return its integer value.
 * Returns 0 if the key is not found. */
long long
tg_json_get_int(const char *json, size_t json_len, const char *key)
{
  size_t vlen;
  const char *v = tg_json_find(json, json_len, key, &vlen);
  if (!v)
    return 0;
  return strtoll(v, NULL, 10);
}

/** Find <key> in <json> and return its boolean value.
 * Returns 0 (false) if the key is not found.  Recognises the JSON
 * literals "true" and "false" as well as non-zero numbers. */
int
tg_json_get_bool(const char *json, size_t json_len, const char *key)
{
  size_t vlen;
  const char *v = tg_json_find(json, json_len, key, &vlen);
  if (!v || vlen == 0)
    return 0;
  if (vlen >= 4 && memcmp(v, "true", 4) == 0)
    return 1;
  /* Also treat non-zero numbers as truthy */
  if (*v >= '1' && *v <= '9')
    return 1;
  return 0;
}
