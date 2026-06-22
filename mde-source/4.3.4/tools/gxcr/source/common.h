// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

// Some constants used by both crypto.c and ipsec.c.

// Length, in bytes, of an MD5 digest field.
#define GXCR_MD5_DIGEST_FIELD_SIZE 16

// Length, in bytes, of a SHA-1 digest field.
#define GXCR_SHA1_DIGEST_FIELD_SIZE 20

// Length, in bytes, of a SHA-2 224- or 256-bit digest field.
#define GXCR_SHA2_224_256_DIGEST_FIELD_SIZE 32

// Length, in bytes, of a SHA-2 384- or 512-bit digest field.
#define GXCR_SHA2_384_512_DIGEST_FIELD_SIZE 64

// Length, in bytes, of the "digest count" field.
#define GXCR_DIGEST_COUNT_FIELD_SIZE 4

// Number of bytes that the packet processing engine considers to be a block
// of digest data when performing successive operations.  Used in the updating
// of the digest_count field.
#define GXCR_DIGEST_SUC_BYTES_PER_BLOCK 64

// The maximum size, in bytes, of the "extra data" used by the MiCA shim.
#define GXCR_MAX_EXTRA_DATA_SIZE \
  ((1 << (MICA_OPCODE__EXTRA_DATA_SIZE_WIDTH - 1)) * 4)

// The maximum size, in bytes, that an EIP-96 context record can be.
// See the Appendix B of the EIP-96 Hardware Specification for details on
// the EIP-96 context record.
#define GXCR_MAX_CONTEXT_RECORD_SIZE (55 * 4)

// The maximum size, in bytes, that a token for an EIP-96 can be.
// Actually it can theoretically be a little bigger than this if the token
// does not happen to make use of all possible context record fields, but
// we should never see one anywhere near this size in practical usage.
#define GXCR_MAX_TOKEN_LEN (GXCR_MAX_EXTRA_DATA_SIZE - \
                            - sizeof(gxcr_result_token_t) \
                            - GXCR_MAX_CONTEXT_RECORD_SIZE)

#ifdef __BIG_ENDIAN__
// Convert a little-endian 4-byte int to the cpu's format.
#define le32_to_cpu(x) __builtin_bswap32(x)
// Convert a 4-byte int to little-endian format.
#define cpu_to_le32(x) le32_to_cpu(x)
#else
// Convert a little-endian 4-byte int to the cpu's format.
#define le32_to_cpu(x) (x)
// Convert a 4-byte int to little-endian format.
#define cpu_to_le32(x) (x)
#endif

static void __USUALLY_INLINE
gxcr_read_context_control_words(gxcr_context_control_words_t* dst_ccw,
                                void* src)
{
  gxcr_context_control_words_t* pccw = (gxcr_context_control_words_t*)src;
  int i;
  for (i = 0; i < sizeof(*dst_ccw)/4; i++)
    dst_ccw->word[i] = cpu_to_le32(pccw->word[i]);
}

static void __USUALLY_INLINE
gxcr_write_context_control_words(gxcr_context_control_words_t* src_ccw,
                                 void* dst)
{
  gxcr_context_control_words_t* pccw = (gxcr_context_control_words_t*)dst;
  int i;
  for (i = 0; i < sizeof(*src_ccw)/4; i++)
    pccw->word[i] = cpu_to_le32(src_ccw->word[i]);
}

extern size_t
gxcr_xcbc_precalc_calc_metadata_bytes(void);

extern int
__gxcr_hmac_precalc(gxio_mica_context_t* mica_context,
                    gxcr_token_info_t context_token_info,
                    unsigned char* context_metadata_mem,
                    void* scratch_mem, int scratch_mem_len,
                    void* hmac_key, int hmac_key_len);

extern int
__gxcr_xcbc_precalc(gxio_mica_context_t* mica_context,
                    gxcr_token_info_t context_token_info,
                    unsigned char* context_metadata_mem,
                    void* scratch_mem, int scratch_mem_len,
                    void* xcbc_key, int xcbc_key_len);

extern int
__gxcr_ghash_precalc(gxio_mica_context_t* mica_context,
                     gxcr_token_info_t context_token_info,
                     unsigned char* context_metadata_mem,
                     void* scratch_mem, int scratch_mem_len,
                     void* ghash_key, int ghash_key_len);

// A wrapper function of memset(), perform a tight loop to clear memory
// if the size is less than 128 bytes.

static inline void *gxcr_memclr(void *buf, size_t n)
{
  if (n < 128)
  {
    if ((((uintptr_t) buf | n) & (sizeof(uint64_t) - 1)) == 0)
    {
      uint64_t *buf64 = (uint64_t *)buf;
      for (; n; n -= sizeof(uint64_t))
        *buf64++ = 0;
      return buf;
    }
    else if ((((uintptr_t) buf | n) & (sizeof(uint32_t) - 1)) == 0)
    {
      uint32_t *buf32 = (uint32_t *)buf;
      for (; n; n -= sizeof(uint32_t))
        *buf32++ = 0;
      return buf;
    }
  }

  return memset(buf, 0, n);
}

// A wrapper function of memcpy(), perform a tight copy loop
// if the size is less than 128 bytes.

static inline void *gxcr_memcpy(void *dst, const void *src, size_t n)
{
  if (n < 128)
  {
    if ((((uintptr_t) dst | (uintptr_t) src | n) &
         (sizeof(uint64_t) - 1)) == 0)
    {
      const uint64_t *src64 = (const uint64_t *)src;
      uint64_t *dst64 = (uint64_t *)dst;

      for (; n; n -= sizeof(uint64_t))
      {
        *dst64++ = *src64++;
      }
      return dst;
    }
    else if ((((uintptr_t) dst | (uintptr_t) src | n) &
              (sizeof(uint32_t) - 1)) == 0)
    {
      const uint32_t *src32 = (const uint32_t *)src;
      uint32_t *dst32 = (uint32_t *)dst;

      for (; n; n -= sizeof(uint32_t))
      {
        *dst32++ = *src32++;
      }
      return dst;
    }
  }

  return memcpy(dst, src, n);
}
