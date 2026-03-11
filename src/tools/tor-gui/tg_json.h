#ifndef TG_JSON_H
#define TG_JSON_H

#include "tg_util.h"

/* JSON builder - builds JSON strings incrementally */
typedef struct tg_json {
  tg_buf_t buf;
  int depth;
  int count[32]; /* element count at each nesting level */
} tg_json_t;

void tg_json_init(tg_json_t *j);
void tg_json_free(tg_json_t *j);
const char *tg_json_finish(tg_json_t *j);

void tg_json_obj_open(tg_json_t *j);
void tg_json_obj_close(tg_json_t *j);
void tg_json_arr_open(tg_json_t *j);
void tg_json_arr_close(tg_json_t *j);

void tg_json_key(tg_json_t *j, const char *key);
void tg_json_str(tg_json_t *j, const char *val);
void tg_json_int(tg_json_t *j, long long val);
void tg_json_double(tg_json_t *j, double val);
void tg_json_bool(tg_json_t *j, int val);
void tg_json_null(tg_json_t *j);
void tg_json_raw(tg_json_t *j, const char *raw);

/* Convenience: add key-value pairs */
void tg_json_kv_str(tg_json_t *j, const char *key, const char *val);
void tg_json_kv_int(tg_json_t *j, const char *key, long long val);
void tg_json_kv_bool(tg_json_t *j, const char *key, int val);
void tg_json_kv_double(tg_json_t *j, const char *key, double val);

/* JSON parser - find values by key */
const char *tg_json_find(const char *json, size_t json_len,
                         const char *key, size_t *vlen);
char *tg_json_get_str(const char *json, size_t json_len, const char *key);
long long tg_json_get_int(const char *json, size_t json_len, const char *key);
int tg_json_get_bool(const char *json, size_t json_len, const char *key);

#endif /* TG_JSON_H */
