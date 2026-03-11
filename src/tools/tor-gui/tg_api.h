#ifndef TG_API_H
#define TG_API_H

#include "vendor/mongoose.h"
#include "tg_control.h"

/* Forward declaration */
struct tg_http;

/* Handle an API request. Returns 1 if handled, 0 if not an API route */
int tg_api_handle(struct mg_connection *c, struct mg_http_message *hm,
                  struct tg_http *http);

#endif /* TG_API_H */
