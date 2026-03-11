#ifndef TG_QR_H
#define TG_QR_H

#include <stddef.h>
#include <stdint.h>

/* Generate QR code as PNG image data.
 * Returns malloc'd PNG data, sets *png_len to the size.
 * Caller must free the returned pointer.
 * scale: pixels per QR module (default 8)
 * border: number of quiet-zone modules (default 4)
 * Returns NULL on error */
uint8_t *tg_qr_generate_png(const char *data, size_t *png_len,
                             int scale, int border);

#endif /* TG_QR_H */
