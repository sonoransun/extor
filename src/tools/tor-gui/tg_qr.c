/* tg_qr.c -- QR code to PNG conversion for tor-gui
 *
 * Uses qrcodegen library to generate QR bitmap, then encodes it as a
 * minimal valid PNG with uncompressed deflate (no zlib dependency).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tg_qr.h"
#include "tg_util.h"
#include "vendor/qrcodegen.h"

/* ---- CRC32 implementation (polynomial 0xEDB88320) ---- */

static const uint32_t crc32_table[256] = {
  0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu,
  0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
  0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Bu, 0x97D2D988u,
  0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D09u, 0x90BF1D9Fu,
  0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu,
  0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
  0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu,
  0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
  0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u,
  0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
  0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u,
  0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
  0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u,
  0x21B4F6B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
  0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
  0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
  0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au,
  0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
  0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u,
  0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
  0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
  0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
  0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu,
  0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
  0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u,
  0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
  0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u,
  0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u,
  0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u,
  0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
  0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u,
  0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
  0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au,
  0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
  0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
  0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
  0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu,
  0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
  0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu,
  0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
  0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u,
  0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
  0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u,
  0xDF60EFC3u, 0xA8670955u, 0x31684D6Fu, 0x466D7561u,
  0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
  0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu,
  0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u,
  0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
  0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au,
  0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
  0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u,
  0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u,
  0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu,
  0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
  0x88085AE6u, 0xFF0F6B70u, 0x66063BCAu, 0x11010B5Cu,
  0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
  0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u,
  0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu,
  0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u,
  0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
  0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u,
  0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu,
  0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u,
  0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du
};

/* ---- PNG helpers ---- */

static void
put32be(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)((v >> 24) & 0xFF);
  p[1] = (uint8_t)((v >> 16) & 0xFF);
  p[2] = (uint8_t)((v >> 8) & 0xFF);
  p[3] = (uint8_t)(v & 0xFF);
}

static void
write_chunk(tg_buf_t *out, const char *type,
            const uint8_t *data, size_t len)
{
  uint8_t hdr[4];
  uint8_t crc_buf[4];
  uint32_t c;

  /* 4-byte length (big endian) */
  put32be(hdr, (uint32_t)len);
  tg_buf_append(out, (const char *)hdr, 4);

  /* 4-byte type */
  tg_buf_append(out, type, 4);

  /* Data */
  if (data && len > 0)
    tg_buf_append(out, (const char *)data, len);

  /* CRC covers type + data */
  {
    uint32_t r = 0xFFFFFFFFu;
    for (size_t i = 0; i < 4; i++)
      r = crc32_table[(r ^ (uint8_t)type[i]) & 0xFF] ^ (r >> 8);
    if (data && len > 0) {
      for (size_t i = 0; i < len; i++)
        r = crc32_table[(r ^ data[i]) & 0xFF] ^ (r >> 8);
    }
    c = r ^ 0xFFFFFFFFu;
  }

  put32be(crc_buf, c);
  tg_buf_append(out, (const char *)crc_buf, 4);
}

/* ---- Adler-32 ---- */

static uint32_t
adler32(const uint8_t *data, size_t len)
{
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; i++) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

/* ---- Main QR-to-PNG function ---- */

uint8_t *
tg_qr_generate_png(const char *data, size_t *png_len,
                    int scale, int border)
{
  uint8_t qr_buf[qrcodegen_BUFFER_LEN_MAX];
  uint8_t temp_buf[qrcodegen_BUFFER_LEN_MAX];
  int qr_size;
  uint32_t img_size, row_bytes, raw_size;
  uint8_t *raw;
  uint32_t adler_val;
  uint32_t max_block, nblocks, deflate_size;
  uint8_t *deflate_data;
  size_t dp;
  tg_buf_t out;
  uint8_t *result;

  if (!data || !png_len)
    return NULL;
  if (scale <= 0)
    scale = 8;
  if (border < 0)
    border = 4;

  /* Generate QR code */
  if (!qrcodegen_encodeText(data, temp_buf, qr_buf,
                            qrcodegen_Ecc_LOW,
                            qrcodegen_VERSION_MIN,
                            qrcodegen_VERSION_MAX,
                            qrcodegen_Mask_AUTO, 1)) {
    return NULL;
  }

  qr_size = qrcodegen_getSize(qr_buf);
  if (qr_size <= 0)
    return NULL;

  /* Calculate image dimensions */
  img_size = (uint32_t)(qr_size + 2 * border) * (uint32_t)scale;

  /* Build raw scanline data: filter(1) + pixels(img_size) per row */
  row_bytes = 1 + img_size;
  raw_size = row_bytes * img_size;

  raw = (uint8_t *)malloc(raw_size);
  if (!raw)
    return NULL;

  for (uint32_t y = 0; y < img_size; y++) {
    uint32_t row_off = y * row_bytes;
    int mod_y = (int)(y / (uint32_t)scale) - border;

    raw[row_off] = 0x00; /* filter byte: None */

    for (uint32_t x = 0; x < img_size; x++) {
      int mod_x = (int)(x / (uint32_t)scale) - border;
      uint8_t pixel;

      if (mod_x >= 0 && mod_x < qr_size &&
          mod_y >= 0 && mod_y < qr_size &&
          qrcodegen_getModule(qr_buf, mod_x, mod_y)) {
        pixel = 0x00; /* black */
      } else {
        pixel = 0xFF; /* white */
      }
      raw[row_off + 1 + x] = pixel;
    }
  }

  /*
   * Build uncompressed deflate stream.
   * zlib header (2) + stored blocks (5 + payload each) + adler32 (4)
   * Max block payload: 65535 bytes.
   */
  adler_val = adler32(raw, raw_size);

  max_block = 65535;
  nblocks = (raw_size + max_block - 1) / max_block;
  deflate_size = 2 + nblocks * 5 + raw_size + 4;

  deflate_data = (uint8_t *)malloc(deflate_size);
  if (!deflate_data) {
    free(raw);
    return NULL;
  }

  dp = 0;

  /* zlib header */
  deflate_data[dp++] = 0x78;
  deflate_data[dp++] = 0x01;

  /* Write stored blocks */
  {
    uint32_t remaining = raw_size;
    uint32_t offset = 0;

    while (remaining > 0) {
      uint32_t blen = remaining;
      if (blen > max_block)
        blen = max_block;
      remaining -= blen;

      /* BFINAL=1 if last block, BTYPE=00 */
      deflate_data[dp++] = (remaining == 0) ? 0x01 : 0x00;
      /* LEN (16-bit LE) */
      deflate_data[dp++] = (uint8_t)(blen & 0xFF);
      deflate_data[dp++] = (uint8_t)((blen >> 8) & 0xFF);
      /* NLEN (~LEN, 16-bit LE) */
      deflate_data[dp++] = (uint8_t)(~blen & 0xFF);
      deflate_data[dp++] = (uint8_t)((~blen >> 8) & 0xFF);
      /* Raw data */
      memcpy(deflate_data + dp, raw + offset, blen);
      dp += blen;
      offset += blen;
    }
  }

  /* Adler-32 checksum (big endian) */
  put32be(deflate_data + dp, adler_val);
  dp += 4;

  free(raw);

  /*
   * Assemble PNG:
   *   8-byte signature
   *   IHDR chunk (13 data bytes)
   *   IDAT chunk (deflate data)
   *   IEND chunk (empty)
   */
  tg_buf_init(&out);

  /* PNG signature */
  {
    static const char png_sig[8] = {
      '\x89', 'P', 'N', 'G', '\r', '\n', '\x1A', '\n'
    };
    tg_buf_append(&out, png_sig, 8);
  }

  /* IHDR chunk */
  {
    uint8_t ihdr[13];
    put32be(ihdr + 0, img_size);   /* width */
    put32be(ihdr + 4, img_size);   /* height */
    ihdr[8] = 8;                    /* bit depth */
    ihdr[9] = 0;                    /* color type: grayscale */
    ihdr[10] = 0;                   /* compression: deflate */
    ihdr[11] = 0;                   /* filter: adaptive */
    ihdr[12] = 0;                   /* interlace: none */
    write_chunk(&out, "IHDR", ihdr, 13);
  }

  /* IDAT chunk */
  write_chunk(&out, "IDAT", deflate_data, dp);
  free(deflate_data);

  /* IEND chunk */
  write_chunk(&out, "IEND", NULL, 0);

  /* Copy tg_buf to malloc'd output */
  *png_len = out.len;
  result = (uint8_t *)malloc(out.len);
  if (result)
    memcpy(result, out.data, out.len);
  else
    *png_len = 0;

  tg_buf_free(&out);
  return result;
}
