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


#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gxcr/pka.h>
#include "pka_test_utils.h"


#define NOINLINE __attribute__((noinline))

#define MAX(a, b)  (((a) <= (b)) ? (b) : (a))
#define MIN(a, b)  (((a) <= (b)) ? (a) : (b))

#define LOG(min_verbosity, fmt_and_args...)   \
    ({                                        \
        if (min_verbosity <= verbosity)       \
            printf(fmt_and_args);             \
    })


// All of the following constants are in big-endian format.

static char P256_p_string[] =
    "ffffffff 00000001 00000000 00000000 00000000 ffffffff"
    "ffffffff ffffffff";

static char P256_a_string[] =
    "ffffffff 00000001 00000000 00000000 00000000 ffffffff"
    "ffffffff fffffffc";

static char P256_b_string[] =
    "5ac635d8 aa3a93e7 b3ebbd55 769886bc 651d06b0 cc53b0f6"
    "3bce3c3e 27d2604b";

static char P256_xg_string[] =
    "6b17d1f2 e12c4247 f8bce6e5 63a440f2 77037d81 2deb33a0"
    "f4a13945 d898c296";

static char P256_yg_string[] =
    "4fe342e2 fe1a7f9b 8ee7eb4a 7c0f9e16 2bce3357 6b315ece"
    "cbb64068 37bf51f5";

static char P256_n_string[] =
    "ffffffff 00000000 ffffffff ffffffff bce6faad a7179e84"
    "f3b9cac2 fc632551";

static char P256_d_string[] =
    "70a12c2d b16845ed 56ff68cf c21a472b 3f04d7d6 851bf634"
    "9f2d7d5b 3452b38a";

static char P256_xq_string[] =
    "8101ece4 7464a6ea d70cf69a 6e2bd3d8 8691a326 2d22cba4"
    "f7635eaf f26680a8";

static char P256_yq_string[] =
    "d8a12ba6 1d599235 f67d9cb4 d58f1783 d3ca43e7 8f0a5aba"
    "a6240799 36c0c3a9";

static char P256_k_string[] =
    "580ec00d 85643433 4cef3f71 ecaed496 5b12ae37 fa47055b"
    "1965c7b1 34ee45d0";

static char P256_kinv_string[] =
    "6a664fa1 15356d33 f16331b5 4c4e7ce9 67965386 c7dcbf29"
    "04604d0c 132b4a74";

static char P256_hash_string[] =
    "7c3e883d dc8bd688 f96eac5e 9324222c 8f30f9d6 bb59e9c5"
    "f020bd39 ba2b8377";

static char P256_s_string[] =
    "7d1ff961 980f961b daa3233b 6209f401 3317d3e3 f9e14935"
    "92dbeaa1 af2bc367";



static char P384_p_string[] =
    "ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff"
    "ffffffff fffffffe ffffffff 00000000 00000000 ffffffff";

static char P384_a_string[] =
    "ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff"
    "ffffffff fffffffe ffffffff 00000000 00000000 fffffffc";

static char P384_b_string[] =
    "b3312fa7 e23ee7e4 988e056b e3f82d19 181d9c6e fe814112"
    "0314088f 5013875a c656398d 8a2ed19d 2a85c8ed d3ec2aef";

static char P384_xg_string[] =
    "aa87ca22 be8b0537 8eb1c71e f320ad74 6e1d3b62 8ba79b98"
    "59f741e0 82542a38 5502f25d bf55296c 3a545e38 72760ab7";

static char P384_yg_string[] =
    "3617de4a 96262c6f 5d9e98bf 9292dc29 f8f41dbd 289a147c"
    "e9da3113 b5f0b8c0 0a60b1ce 1d7e819d 7a431d7c 90ea0e5f";

static char P384_n_string[] =
    "ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff"
    "c7634d81 f4372ddf 581a0db2 48b0a77a ecec196a ccc52973";

static char P384_d_string[] =
    "c838b852 53ef8dc7 394fa580 8a518398 1c7deef5 a69ba8f4"
    "f2117ffe a39cfcd9 0e95f6cb c854abac ab701d50 c1f3cf24";

static char P384_xq_string[] =
    "1fbac8ee bd0cbf35 640b39ef e0808dd7 74debff2 0a2a329e"
    "91713baf 7d7f3c3e 81546d88 3730bee7 e48678f8 57b02ca0";

static char P384_yq_string[] =
    "eb213103 bd68ce34 3365a8a4 c3d4555f a385f533 0203bdd7"
    "6ffad1f3 affb9575 1c132007 e1b24035 3cb0a4cf 1693bdf9";

static char P384_k_string[] =
    "dc6b4403 6989a196 e39d1cda c000812f 4bdd8b2d b41bb33a"
    "f5137258 5ebd1db6 3f0ce827 5aa1fd45 e2d2a735 f8749359";

static char P384_kinv_string[] =
    "7436f030 88e65c37 ba8e7b33 887fbc87 757514d6 11f7d1fb"
    "df6d2104 a297ad31 8cdbf740 4e4ba37e 599666df 37b8d8be";

static char P384_hash_string[] =
    "b9210c9d 7e20897a b8659726 6a9d5077 e8db1b06 f7220ed6"
    "ee75bd8b 45db3789 1f8ba555 03040041 59f4453d c5b3f5a1";

static char P384_s_string[] =
    "20ab3f45 b74f10b6 e11f96a2 c8eb694d 206b9dda 86d3c7e3"
    "31c26b22 c987b753 77265776 67adadf1 68ebbe80 3794a402";



static char P521_p_string[] =
        "01ff ffffffff ffffffff ffffffff ffffffff ffffffff"
    "ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff"
    "ffffffff ffffffff ffffffff ffffffff ffffffff";

static char P521_a_string[] =
        "01ff ffffffff ffffffff ffffffff ffffffff ffffffff"
    "ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff"
    "ffffffff ffffffff ffffffff ffffffff fffffffc";

static char P521_b_string[] =
          "51 953eb961 8e1c9a1f 929a21a0 b68540ee a2da725b"
    "99b315f3 b8b48991 8ef109e1 56193951 ec7e937b 1652c0bd"
    "3bb1bf07 3573df88 3d2c34f1 ef451fd4 6b503f00";

static char P521_xg_string[] =
          "c6 858e06b7 0404e9cd 9e3ecb66 2395b442 9c648139"
    "053fb521 f828af60 6b4d3dba a14b5e77 efe75928 fe1dc127"
    "a2ffa8de 3348b3c1 856a429b f97e7e31 c2e5bd66";

static char P521_yg_string[] =
        "0118 39296a78 9a3bc004 5c8a5fb4 2c7d1bd9 98f54449"
    "579b4468 17afbd17 273e662c 97ee7299 5ef42640 c550b901"
    "3fad0761 353c7086 a272c240 88be9476 9fd16650";

static char P521_n_string[] =
        "01ff ffffffff ffffffff ffffffff ffffffff ffffffff"
    "ffffffff ffffffff fffffffa 51868783 bf2f966b 7fcc0148"
    "f709a5d0 3bb5c9b8 899c47ae bb6fb71e 91386409";



static char *TEST_NAME_STRING[] =
{
    [TEST_NOP]                  = "TEST_NOP",
    [TEST_ADD]                  = "TEST_ADD",
    [TEST_SUBTRACT]             = "TEST_SUBTRACT",
    [TEST_MULTIPLY]             = "TEST_MULTIPLY",
    [TEST_DIVIDE]               = "TEST_DIVIDE",
    [TEST_DIV_MOD]              = "TEST_DIV_MOD",
    [TEST_MODULO]               = "TEST_MODULO",
    [TEST_SHIFT_LEFT]           = "TEST_SHIFT_LEFT",
    [TEST_SHIFT_RIGHT]          = "TEST_SHIFT_RIGHT",
    [TEST_MOD_INVERT]           = "TEST_MOD_INVERT",
    [TEST_MOD_EXP]              = "TEST_MOD_EXP",
    [TEST_RSA_MOD_EXP]          = "TEST_RSA_MOD_EXP",
    [TEST_RSA_VERIFY]           = "TEST_RSA_VERIFY",
    [TEST_RSA_MOD_EXP_WITH_CRT] = "TEST_RSA_MOD_EXP_WITH_CRT",
    [TEST_ECC_ADD]              = "TEST_ECC_ADD",
    [TEST_ECC_DOUBLE]           = "TEST_ECC_DOUBLE",
    [TEST_ECC_MULTIPLY]         = "TEST_ECC_MULTIPLY",
    [TEST_ECDSA_GEN]            = "TEST_ECDSA_GEN",
    [TEST_ECDSA_VERIFY]         = "TEST_ECDSA_VERIFY",
    [TEST_ECDSA_GEN_VERIFY]     = "TEST_ECDSA_GEN_VERIFY",
    [TEST_DSA_GEN]              = "TEST_DSA_GEN",
    [TEST_DSA_VERIFY]           = "TEST_DSA_VERIFY",
    [TEST_DSA_GEN_VERIFY]       = "TEST_DSA_GEN_VERIFY"
};

ecdsa_key_system_t P256_ecdsa;
ecdsa_key_system_t P384_ecdsa;
ecdsa_key_system_t P521_ecdsa;

static test_ecdsa_t P256_ecdsa_test;
static test_ecdsa_t P384_ecdsa_test;

static pka_operand_t *P256_kinv;
static pka_operand_t *P384_kinv;

static uint8_t TWO_BUFFER[1]  = { 0x02 };
static uint8_t ONE_BUFFER[1]  = { 0x01 };
static uint8_t ZERO_BUFFER[1] = { 0x00 };

static pka_operand_t TWO =
{
    .buf_len    = 1,
    .actual_len = 1,
    .buf_ptr    = &TWO_BUFFER[0]
};

static pka_operand_t ONE =
{
    .buf_len    = 1,
    .actual_len = 1,
    .buf_ptr    = &ONE_BUFFER[0]
};

static pka_operand_t ZERO =
{
    .buf_len    = 1,
    .actual_len = 1,
    .buf_ptr    = &ZERO_BUFFER[0]
};



uint32_t pka_get_msb_idx(pka_operand_t *operand, uint8_t big_endian)
{
    uint32_t byte_len, msb_idx;
    uint8_t *byte_ptr;

    if (big_endian)
    {
        byte_ptr = &operand->buf_ptr[0];
        if (byte_ptr[0] != 0)
            return 0;

        // Move forwards over all zero bytes.
        byte_len = operand->actual_len;
        msb_idx  = 0;
        while ((byte_ptr[0] == 0) && (1 <= byte_len))
        {
            msb_idx++;
            byte_ptr++;
            byte_len--;
        }

        return msb_idx;
    }
    else  // little-endian.
    {
        // First find the most significant byte based upon the actual_len, and
        // then move backwards over all zero bytes, in order to skip leading
        // zeros and find the real msb index.
        byte_len = operand->actual_len;
        byte_ptr = &operand->buf_ptr[byte_len - 1];
        if (byte_ptr[0] != 0)
            return byte_len - 1;

        msb_idx = byte_len - 1;
        while ((byte_ptr[0] == 0) && (1 <= byte_len))
        {
            msb_idx--;
            byte_ptr--;
            byte_len--;
        }

        return msb_idx;
    }
}

boolean_t pka_is_zero(pka_operand_t *operand)
{
    uint32_t len, idx;

    // Note that this algorithm does not depend on endianess.
    len = operand->actual_len;
    for (idx = 0;  idx < len;  idx++)
        if (operand->buf_ptr[idx] != 0)
            return FALSE;

    return TRUE;
}

boolean_t pka_is_one(pka_operand_t *operand)
{
    return pka_compare(operand, &ONE) == PKA_EQUAL;
}

static uint32_t count_leading_zeros(pka_operand_t *operand, uint8_t big_endian)
{
    uint32_t byte_len, leading_zeros;
    uint8_t  ms_byte;

    byte_len = operand->actual_len;
    if (pka_is_zero(operand))
        return byte_len - 1;
    else if (byte_len == 0)
        byte_len = operand->buf_len;

    for (leading_zeros = 0; leading_zeros <= byte_len; leading_zeros++)
    {
        if (big_endian)
            ms_byte = operand->buf_ptr[leading_zeros];
        else
            ms_byte = operand->buf_ptr[(byte_len - 1) - leading_zeros];

        if (ms_byte != 0)
            return leading_zeros;
    }

    Assert(FALSE);
    return byte_len - 1;
}

status_t to_uint32_integer(pka_operand_t *operand, uint32_t *value_ptr)
{
    uint32_t operand_len, msb_idx, value, idx;
    uint8_t *buf_ptr;

    operand_len = pka_operand_byte_len(operand);
    if (4 < operand_len)
    {
        *value_ptr = 0;
        return FAILURE;
    }

    msb_idx = pka_get_msb_idx(operand, operand->big_endian);
    buf_ptr = &operand->buf_ptr[msb_idx];
    value   = 0;

    // Work from the most significant end to the least significant end.
    for (idx = 0; idx < operand_len; idx++)
    {
        value = (value << 8) | buf_ptr[0];
        if (operand->big_endian)
            buf_ptr++;
        else
            buf_ptr--;
    }

    *value_ptr = value;
    return SUCCESS;
}

static pka_operand_t *uint32_to_operand(gxcr_pka_handle_t *handle,
                                        uint32_t           value,
                                        uint8_t            big_endian)
{
    pka_operand_t *operand;
    uint32_t       value_len, lsb_idx, idx;
    uint8_t       *buf_ptr;

    if (value < 0x100)
        value_len = 1;
    else if (value < 0x10000)
        value_len = 2;
    else if (value < 0x1000000)
        value_len = 3;
    else
        value_len = 4;

    operand             = malloc_operand(handle, value_len);
    operand->actual_len = value_len;
    operand->big_endian = big_endian;

    lsb_idx = big_endian ? (value_len - 1) : 0;
    buf_ptr = &operand->buf_ptr[lsb_idx];

    // Work from the least significant end to the most significant end.
    for (idx = 0;  idx < value_len;  idx++)
    {
        *buf_ptr = value & 0xFF;
        value    = value >> 8;
        if (big_endian)
            buf_ptr--;
        else
            buf_ptr++;
    }

    return operand;
}

void print_pka_operand (char          *pre_string,
                        pka_operand_t *big_num,
                        char          *post_string)
{
    char string_buffer[2050];

    if (pre_string == NULL)
        pre_string = "";

    if (post_string == NULL)
        post_string = "";

    if (big_num == NULL)
    {
        printf("%s0xNULL%s", pre_string, post_string);
        return;
    }

    gxcr_pka_to_hex_string (big_num, &string_buffer[0], 2050);
    if (strlen(&string_buffer[0]) == 0)
        string_buffer[0] = '0';

    if (big_num->big_endian)
        printf("%s0x%s%s", pre_string, &string_buffer[0], post_string);
    else
        printf("%s%s%s", pre_string, &string_buffer[0], post_string);
}



pka_operand_t *malloc_operand(gxcr_pka_handle_t *handle, uint32_t buf_len)
{
    pka_operand_t *operand;

    operand             = calloc(1, sizeof(pka_operand_t));
    operand->buf_ptr    = calloc(1, buf_len);
    operand->buf_len    = buf_len;
    operand->actual_len = 0;
    operand->big_endian = gxcr_pka_get_endian(handle);
    return operand;
}

pka_operand_t *hex_string_to_operand(gxcr_pka_handle_t *handle, char *string)
{
    pka_operand_t *operand;
    uint32_t       string_len, hex_digits, idx;

    string_len = strlen(string);
    hex_digits = 0;
    for (idx = 0;  idx < string_len;  idx++)
        if (isxdigit(string[idx]))
            hex_digits++;

    operand = malloc_operand(handle, (hex_digits + 1) / 2);
    gxcr_pka_from_hex_string(string, operand);
    return operand;
}

ecc_curve_t *create_ecc_curve(gxcr_pka_handle_t *handle,
                              pka_operand_t     *prime,
                              pka_operand_t     *a,
                              pka_operand_t     *b)
{
    ecc_curve_t *result_curve;

    result_curve    = calloc(1, sizeof(ecc_curve_t));
    result_curve->p = prime;
    result_curve->a = a;
    result_curve->b = b;
    return result_curve;
}

ecc_point_t *create_ecc_point(gxcr_pka_handle_t *handle,
                              pka_operand_t     *x,
                              pka_operand_t     *y)
{
    ecc_point_t *result_pt;

    result_pt    = calloc(1, sizeof(ecc_point_t));
    result_pt->x = x;
    result_pt->y = y;
    return result_pt;
}

ecc_point_t *malloc_ecc_point(gxcr_pka_handle_t *handle,
                              uint32_t           x_buf_len,
                              uint32_t           y_buf_len)
{
    ecc_point_t *result_pt;

    result_pt    = calloc(1, sizeof(ecc_point_t));
    result_pt->x = malloc_operand(handle, x_buf_len);
    result_pt->y = malloc_operand(handle, y_buf_len);
    return result_pt;
}

dsa_signature_t *malloc_dsa_signature(gxcr_pka_handle_t *handle,
                                      uint32_t           r_buf_len,
                                      uint32_t           s_buf_len)
{
    dsa_signature_t *signature;

    signature    = calloc(1, sizeof(dsa_signature_t));
    signature->r = malloc_operand(handle, r_buf_len);
    signature->s = malloc_operand(handle, s_buf_len);
    return signature;
}

pka_operand_t *dup_operand(gxcr_pka_handle_t *handle,
                           pka_operand_t     *src_operand)
{
    pka_operand_t *new_operand;
    uint32_t       leading_zeros, len;
    uint8_t       *src_buf_ptr;

    if (src_operand == NULL)
    {
        printf("dup_operand called with src_operand == NULL\n");
        return NULL;
    }

    leading_zeros = count_leading_zeros(src_operand, src_operand->big_endian);
    len           = src_operand->actual_len - leading_zeros;
    src_buf_ptr   = src_operand->buf_ptr;
    if (src_operand->big_endian)
        src_buf_ptr += leading_zeros;

    new_operand             = malloc_operand(handle, len);
    new_operand->actual_len = len;
    new_operand->big_endian = src_operand->big_endian;
    memcpy(new_operand->buf_ptr, src_buf_ptr, len);
    return new_operand;
}

ecc_point_t *dup_ecc_point(gxcr_pka_handle_t *handle, ecc_point_t *src_point)
{
    ecc_point_t *new_point;

    new_point    = calloc(1, sizeof(ecc_point_t));
    new_point->x = dup_operand(handle, src_point->x);
    new_point->y = dup_operand(handle, src_point->y);
    return new_point;
}

pka_results_t *malloc_results(uint32_t result_cnt, uint32_t buf_len)
{
    pka_results_t *results;

    results                        = calloc(1, sizeof(pka_results_t));
    results->results[0].buf_ptr    = calloc(1, buf_len);
    results->results[0].buf_len    = buf_len;
    results->results[0].actual_len = 0;
    results->result_cnt            = 1;
    if (2 <= result_cnt)
    {
        results->result_cnt            = 2;
        results->results[1].buf_ptr    = calloc(1, buf_len);
        results->results[1].buf_len    = buf_len;
        results->results[1].actual_len = 0;
    }

    return results;
}

void free_operand(pka_operand_t **operand_ptr)
{
    pka_operand_t *operand;
    uint8_t       *buf_ptr;

    if (operand_ptr == NULL)
    {
        printf("free_operand called with NULL operand_ptr\n");
        return;
    }

    operand = *operand_ptr;
    if (operand == NULL)
    {
        printf("free_operand called with NULL operand\n");
        return;
    }

    buf_ptr = operand->buf_ptr;
    if (buf_ptr == NULL)
        printf("free_operand called with NULL buf_ptr\n");
    else
        free(buf_ptr);

    operand->buf_ptr      = NULL;
    operand->buf_len      = 0;
    operand->actual_len   = 0;
    operand->internal_use = 0;
    operand->pad          = 0;
    free(operand);
    *operand_ptr = NULL;
}

void free_ecc_point(ecc_point_t *ecc_point)
{
    if (ecc_point == NULL)
    {
        printf("free_ecc_point called with NULL operand\n");
        return;
    }

    free_operand(&ecc_point->x);
    free_operand(&ecc_point->y);
    free(ecc_point);
}

void free_results(pka_results_t *results)
{
    if (results == NULL)
    {
        printf("free_results called with NULL operand\n");
        return;
    }

    free(results->results[0].buf_ptr);
    results->results[0].buf_ptr    = NULL;
    results->results[0].buf_len    = 0;
    results->results[0].actual_len = 0;
    if (2 <= results->result_cnt)
    {
        free(results->results[1].buf_ptr);
        results->results[1].buf_ptr    = NULL;
        results->results[1].buf_len    = 0;
        results->results[1].actual_len = 0;
    }

    free(results);
}



status_t get_results(gxcr_pka_handle_t *handle,
                     pka_operand_t     *result1,
                     pka_operand_t     *result2)
{
    pka_results_t *temp;
    uint32_t       result1_len, result2_len;

    temp = malloc_results(2, MAX_BYTE_LEN + 8);
    gxcr_pka_get_results(handle, temp);
    if (temp->status != 0)
    {
        printf("get_results status=0x%X\n", temp->status);
        free_results(temp);
        return FAILURE;
    }

    if (temp->result_cnt != 2)
        printf("get_results result_cnt != 2\n");

    if (temp->results[0].big_endian != temp->results[1].big_endian)
        printf("get_results mixed endianness\n");

    if (result1->buf_ptr != NULL)
        free(result1->buf_ptr);

    if (result2->buf_ptr != NULL)
        free(result2->buf_ptr);

    memset(result1, 0, sizeof(pka_operand_t));
    result1_len         = temp->results[0].actual_len;
    result1->actual_len = result1_len;
    result1->buf_len    = result1_len;
    result1->buf_ptr    = malloc(result1_len);
    result1->big_endian = temp->results[0].big_endian;
    memcpy(result1->buf_ptr, temp->results[0].buf_ptr, result1_len);

    memset(result2, 0, sizeof(pka_operand_t));
    result2_len         = temp->results[1].actual_len;
    result2->actual_len = result2_len;
    result2->buf_len    = result2_len;
    result2->buf_ptr    = malloc(result2_len);
    result2->big_endian = temp->results[1].big_endian;
    memcpy(result2->buf_ptr, temp->results[1].buf_ptr, result2_len);

    free_results(temp);
    return SUCCESS;
}

pka_comparison_t get_compare_result(gxcr_pka_handle_t *handle)
{
    pka_comparison_t compare_result;
    pka_results_t   *temp;

    temp = malloc_results(2, MAX_BYTE_LEN + 8);
    gxcr_pka_get_results(handle, temp);

    if (temp->status != 0)
    {
        printf("get_compare_result status=0x%X\n", temp->status);
        free_results(temp);
        return PKA_NO_COMPARE;
    }

    compare_result = temp->compare_result;
    if (temp->result_cnt != 0)
        printf("get_compare_result result_cnt should be zero was=%d\n",
               temp->result_cnt);

    free_results(temp);
    return compare_result;
}

pka_operand_t *results_to_operand(gxcr_pka_handle_t *handle)
{
    pka_results_t *temp;
    pka_operand_t *result;
    uint32_t       temp_len;

    temp = malloc_results(1, MAX_BYTE_LEN + 8);
    gxcr_pka_get_results(handle, temp);
    if (temp->status != 0)
    {
        printf("get_results status=0x%X\n", temp->status);
        free_results(temp);
        return NULL;
    }

    temp_len           = temp->results[0].actual_len;
    result             = malloc_operand(handle, temp_len);
    result->actual_len = temp_len;
    result->big_endian = temp->results[0].big_endian;
    memcpy(result->buf_ptr, temp->results[0].buf_ptr, temp_len);

    free_results(temp);
    return result;
}



pka_operand_t *rand_operand(gxcr_pka_handle_t *handle,
                            uint32_t           bit_len,
                            boolean_t          make_odd)
{
    pka_operand_t *result;
    uint32_t       byte_len, msb_idx, lsb_idx, num_msb_bits;
    uint8_t        msb_byte;

    byte_len           = (bit_len + 7) / 8;
    result             = malloc_operand(handle, byte_len);
    result->actual_len = byte_len;
    result->big_endian = gxcr_pka_get_endian(handle);

    gxcr_pka_get_rand_bytes(handle, &result->buf_ptr[0], byte_len);

    // Get the index of the most significant and least significant bytes.
    if (result->big_endian)
    {
        result->big_endian = 1;
        msb_idx            = 0;
        lsb_idx            = byte_len - 1;
    }
    else
    {
        result->big_endian = 0;
        msb_idx            = byte_len - 1;
        lsb_idx            = 0;
    }

    // Make sure the msb byte is non-zero and in fact is of the correct
    // bit len.
    msb_byte     = result->buf_ptr[msb_idx];
    num_msb_bits = bit_len - (8 * (byte_len - 1));
    Assert((1 <= num_msb_bits) && (num_msb_bits <= 8));

    msb_byte |=   1 << (num_msb_bits - 1);
    msb_byte &= ((1 << num_msb_bits) - 1);

    result->buf_ptr[msb_idx] = msb_byte;
    if (make_odd)
        result->buf_ptr[lsb_idx] |= 0x01;

    Assert(pka_operand_bit_len(result) == bit_len);
    return result;
}

pka_operand_t *rand_prime(gxcr_pka_handle_t *handle, uint32_t bit_len)
{
    pka_operand_t *operand;
    uint32_t       max_attempts, attempts;

    max_attempts  = 16 * bit_len;
    for (attempts = 1;  attempts <= max_attempts;  attempts++)
    {
        operand = rand_operand(handle, bit_len, TRUE);
        if (is_prime(handle, operand, 25, FALSE))
        {
            if ((max_attempts / 2) < attempts)
                printf("rand_prime required %d attempts for bit_len=%u\n",
                       attempts, bit_len);
            return operand;
        }

        free_operand(&operand);
    }

    printf("rand_prime failed to find a prime number after %u attempts\n",
           max_attempts);
    return NULL;
}

// Return a big number between 1 .. max_plus_1 - 1.

pka_operand_t *rand_non_zero_integer(gxcr_pka_handle_t *handle,
                                     pka_operand_t     *max_plus_1)
{
    pka_operand_t *result;
    uint32_t       byte_len, msb_idx, max_plus_msb, result_msb;

    byte_len           = pka_operand_byte_len(max_plus_1);
    result             = malloc_operand(handle, byte_len);
    result->big_endian = gxcr_pka_get_endian(handle);
    result->actual_len = byte_len;

    do
    {
        gxcr_pka_get_rand_bytes(handle, &result->buf_ptr[0], byte_len);
    } while (pka_is_zero(result));

    if (pka_compare(result, max_plus_1) == PKA_LESS_THAN)
        return result;

    // Need to reduce the most significant byte of the result to be less than
    // the most significant byte of max_plus_1.  First get msb of max_plus_1.
    msb_idx      = pka_get_msb_idx(max_plus_1, max_plus_1->big_endian);
    max_plus_msb = max_plus_1->buf_ptr[msb_idx];
    Assert(max_plus_msb != 0);

    // Next find msb of the result and adjust it.
    msb_idx                  = pka_get_msb_idx(result, result->big_endian);
    result_msb               = result->buf_ptr[msb_idx];
    result->buf_ptr[msb_idx] = result_msb % max_plus_msb;
    return result;
}



static pka_operand_t *power_of_two(gxcr_pka_handle_t *handle,
                                   uint32_t           bit_len,
                                   boolean_t          make_odd)
{
    pka_operand_t *result;
    uint32_t       byte_len, msb_idx, lsb_idx, num_msb_bits;

    byte_len           = (bit_len + 7) / 8;
    result             = malloc_operand(handle, byte_len);
    result->actual_len = byte_len;
    result->big_endian = gxcr_pka_get_endian(handle);

    // Get the index of the most significant and least significant bytes.
    if (result->big_endian)
    {
        result->big_endian = 1;
        msb_idx            = 0;
        lsb_idx            = byte_len - 1;
    }
    else
    {
        result->big_endian = 0;
        msb_idx            = byte_len - 1;
        lsb_idx            = 0;
    }

    // Make sure the msb byte is non-zero and in fact is of the correct
    // bit len.
    num_msb_bits = bit_len - (8 * (byte_len - 1));
    Assert((1 <= num_msb_bits) && (num_msb_bits <= 8));

    result->buf_ptr[msb_idx] = 1 << (num_msb_bits - 1);
    if (make_odd)
        result->buf_ptr[lsb_idx] = 0x01;

    Assert(pka_operand_bit_len(result) == bit_len);
    return result;
}



pka_operand_t *sync_add(gxcr_pka_handle_t *handle,
                        pka_operand_t     *value,
                        pka_operand_t     *addend)
{
    gxcr_pka_err_t rc;
    pka_operand_t *hw_result;
    uint32_t       sum_len;

    // See if the size of the result is too large for the HW, and if so use
    // the sw algorithm.
    sum_len = MAX(value->actual_len, addend->actual_len) + 1;
    if (MAX_BYTE_LEN <= sum_len)
        return sw_add(handle, value, addend);

    rc = gxcr_pka_add(handle, NULL, value, addend);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_add failed rc=%d\n", rc);
        return NULL;
    }

    hw_result = results_to_operand(handle);
    return hw_result;
}

pka_operand_t *sync_subtract(gxcr_pka_handle_t *handle,
                             pka_operand_t     *value,
                             pka_operand_t     *subtrahend)
{
    gxcr_pka_err_t rc;

    if ((value == NULL) || (subtrahend == NULL))
    {
        printf("sync_subtract called with NULL operand\n");
        return NULL;
    }

    rc = gxcr_pka_subtract(handle, NULL, value, subtrahend);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_subtract failed rc=%d\n", rc);
        return NULL;
    }

    return results_to_operand(handle);
}

pka_operand_t *sync_multiply(gxcr_pka_handle_t *handle,
                             pka_operand_t     *value,
                             pka_operand_t     *multipler)
{
    gxcr_pka_err_t rc;
    uint32_t       product_len;

    if ((value == NULL) || (multipler == NULL))
    {
        printf("sync_multiply called with NULL operand\n");
        return NULL;
    }

    // See if the size of the value + the size of the multipler is too large
    // for the HW, and if so use the sw algorithm.
    product_len = value->actual_len + multipler->actual_len;
    if (MAX_BYTE_LEN <= product_len)
        return sw_multiply(handle, value, multipler);

    rc = gxcr_pka_multiply(handle, NULL, value, multipler);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_multiply failed rc=%d\n", rc);
        print_pka_operand("  value    =", value,     "\n");
        print_pka_operand("  multipler=", multipler, "\n");
        return NULL;
    }

    return results_to_operand(handle);
}

pka_operand_t *sync_divide(gxcr_pka_handle_t *handle,
                           pka_operand_t     *dividend,
                           pka_operand_t     *divisor)
{
    gxcr_pka_err_t rc;
    pka_operand_t  *quotient, *remainder;

    if ((dividend == NULL) || (divisor == NULL))
    {
        printf("sync_divide called with NULL operand\n");
        return NULL;
    }

    // See if the size of the dividend or divisor is too large for the HW,
    // and if so use the sw algorithm.
    if ((MAX_BYTE_LEN <= dividend->actual_len) ||
        (MAX_BYTE_LEN <= divisor->actual_len))
        return sw_divide(handle, dividend, divisor);

    rc = gxcr_pka_divide(handle, NULL, dividend, divisor);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_divide failed rc=%d\n", rc);
        print_pka_operand("  dividend=", dividend, "\n");
        print_pka_operand("  divisor =", divisor,  "\n");
        return NULL;
    }

    remainder = malloc_operand(handle, MAX_BYTE_LEN);
    quotient  = malloc_operand(handle, MAX_BYTE_LEN);
    if (SUCCESS != get_results(handle, remainder, quotient))
        return NULL;

    free_operand(&remainder);
    return quotient;
}


pka_operand_t *sync_modulo(gxcr_pka_handle_t *handle,
                           pka_operand_t     *value,
                           pka_operand_t     *modulus)
{
    gxcr_pka_err_t rc;

    if ((value == NULL) || (modulus == NULL))
    {
        printf("sync_modulo called with NULL operand\n");
        return NULL;
    }

    if (pka_compare(value, modulus) == PKA_LESS_THAN)
        return dup_operand(handle, value);

    rc = gxcr_pka_modulo(handle, NULL, value, modulus);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_modulo failed rc=%d\n", rc);
        print_pka_operand("  value  =", value,   "\n");
        print_pka_operand("  modulus=", modulus, "\n");
        return NULL;
    }

    return results_to_operand(handle);
}

pka_operand_t *sync_shift_left(gxcr_pka_handle_t *handle,
                               pka_operand_t     *value,
                               uint32_t           shift_cnt)
{
    gxcr_pka_err_t rc;

    rc = gxcr_pka_shift_left(handle, NULL, value, shift_cnt);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_shift_left failed rc=%d\n", rc);
        return NULL;
    }

    return results_to_operand(handle);
}

pka_operand_t *sync_shift_right(gxcr_pka_handle_t *handle,
                                pka_operand_t     *value,
                                uint32_t           shift_cnt)
{
    gxcr_pka_err_t rc;

    rc = gxcr_pka_shift_right(handle, NULL, value, shift_cnt);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_shift_right failed rc=%d\n", rc);
        return NULL;
    }

    return results_to_operand(handle);
}

pka_operand_t *sync_mod_inverse(gxcr_pka_handle_t *handle,
                                pka_operand_t     *value,
                                pka_operand_t     *modulus)
{
    gxcr_pka_err_t rc;

    rc = gxcr_pka_mod_invert(handle, NULL, value, modulus);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_mod_inverse failed rc=%d\n", rc);
        return NULL;
    }

    return results_to_operand(handle);
}

pka_operand_t *sync_mod_add(gxcr_pka_handle_t *handle,
                            pka_operand_t     *value,
                            pka_operand_t     *addend,
                            pka_operand_t     *modulus)
{
    pka_operand_t *sum, *result;

    if ((value == NULL) || (addend == NULL) || (modulus == NULL))
    {
        printf("sync_mod_add called with NULL operand\n");
        return NULL;
    }

    sum = sync_add(handle, value, addend);
    if (pka_compare(sum, modulus) == PKA_LESS_THAN)
        return sum;

    result = sync_subtract(handle, sum, modulus);
    free_operand(&sum);
    return result;
}

pka_operand_t *sync_mod_subtract(gxcr_pka_handle_t *handle,
                                 pka_operand_t     *value,
                                 pka_operand_t     *subtrahend,
                                 pka_operand_t     *modulus)
{
    pka_comparison_t comparison;
    pka_operand_t   *reversed_diff, *result;

    if ((value == NULL) || (subtrahend == NULL) || (modulus == NULL))
    {
        printf("sync_mod_subtract called with NULL operand\n");
        return NULL;
    }

    comparison = pka_compare(value, subtrahend);
    if (comparison == PKA_EQUAL)
        return NULL;
    else if (comparison == PKA_GREATER_THAN)
        return sync_subtract(handle, value, subtrahend);

    // result is -diff mod modulus which is the same as modulus - diff;
    reversed_diff = sync_subtract(handle, subtrahend, value);
    result        = sync_subtract(handle, modulus, reversed_diff);

    free_operand(&reversed_diff);
    return result;
}

pka_operand_t *sync_mod_multiply(gxcr_pka_handle_t *handle,
                                 pka_operand_t     *value,
                                 pka_operand_t     *multiplier,
                                 pka_operand_t     *modulus)
{
    pka_operand_t *product, *result;

    if ((value == NULL) | (multiplier == NULL) || (modulus == NULL))
    {
        printf("sync_mod_multiply called with NULL operand\n");
        return NULL;
    }

    product = sync_multiply(handle, value, multiplier);
    result  = sync_modulo(handle, product, modulus);
    free_operand(&product);
    return result;
}

pka_operand_t *sync_mod_exp(gxcr_pka_handle_t *handle,
                            pka_operand_t     *exponent,
                            pka_operand_t     *modulus,
                            pka_operand_t     *msg)
{
    gxcr_pka_err_t rc;

    if ((msg == NULL) || (exponent == NULL) || (modulus == NULL))
    {
        printf("sync_mod_exp called with some NULL operands\n");
        return NULL;
    }

    rc = gxcr_pka_mod_exp(handle, NULL, exponent, modulus, msg);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_mod_exp failed rc=%d\n", rc);
        print_pka_operand("  exponent=", exponent, "\n");
        print_pka_operand("  modulus =", modulus,  "\n");
        print_pka_operand("  msg     =", msg,      "\n");
        return NULL;
    }

    return results_to_operand(handle);
}

pka_operand_t *sync_exp_with_crt(gxcr_pka_handle_t *handle,
                                 pka_operand_t     *p,
                                 pka_operand_t     *q,
                                 pka_operand_t     *msg,
                                 pka_operand_t     *d_p,
                                 pka_operand_t     *d_q,
                                 pka_operand_t     *qinv)
{
    gxcr_pka_err_t rc;

    rc = gxcr_pka_exp_with_crt(handle, NULL, p, q, msg, d_p, d_q, qinv);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_exp_with_crt failed rc=%d\n", rc);
        return NULL;
    }

    return results_to_operand(handle);
}



ecc_point_t *sync_ecc_add(gxcr_pka_handle_t *handle,
                          ecc_curve_t       *curve,
                          ecc_point_t       *pointA,
                          ecc_point_t       *pointB)
{
    gxcr_pka_err_t rc;
    ecc_point_t   *result_pt;
    uint32_t       buf_len;

    rc = gxcr_pka_ecc_add(handle, NULL, curve, pointA, pointB);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_ecc_add failed rc=%d\n", rc);
        return NULL;
    }

    buf_len   = curve->p->actual_len;
    result_pt = malloc_ecc_point(handle, buf_len, buf_len);
    if (SUCCESS != get_results(handle, result_pt->x, result_pt->y))
        return NULL;

    return result_pt;
}

ecc_point_t *sync_ecc_multiply(gxcr_pka_handle_t *handle,
                               ecc_curve_t       *curve,
                               ecc_point_t       *pointA,
                               pka_operand_t     *multiplier)
{
    gxcr_pka_err_t rc;
    ecc_point_t   *result_pt;
    uint32_t       buf_len;

    rc = gxcr_pka_ecc_multiply(handle, NULL, curve, pointA, multiplier);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_ecc_multiply failed rc=%d\n", rc);
        return NULL;
    }

    buf_len   = curve->p->actual_len;
    result_pt = malloc_ecc_point(handle, curve->p->actual_len,
                                 curve->p->actual_len);
    if (SUCCESS != get_results(handle, result_pt->x, result_pt->y))
        return NULL;

    return result_pt;
}



dsa_signature_t *sync_ecdsa_gen(gxcr_pka_handle_t *handle,
                                ecc_curve_t       *curve,
                                ecc_point_t       *base_pt,
                                pka_operand_t     *base_pt_order,
                                pka_operand_t     *private_key,
                                pka_operand_t     *hash,
                                pka_operand_t     *k)
{
    dsa_signature_t *signature;
    gxcr_pka_err_t   rc;
    uint32_t         buf_len;

    rc = gxcr_pka_ecdsa_gen(handle, NULL, curve, base_pt, base_pt_order,
                            private_key, hash, k);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_ecdsa_gen failed rc=%d\n", rc);
        return NULL;
    }

    buf_len   = curve->p->buf_len;
    signature = malloc_dsa_signature(handle, buf_len, buf_len);
    if (SUCCESS != get_results(handle, signature->r, signature->s))
        return NULL;

    return signature;
}

status_t sync_ecdsa_verify(gxcr_pka_handle_t *handle,
                           ecc_curve_t       *curve,
                           ecc_point_t       *base_pt,
                           pka_operand_t     *base_pt_order,
                           ecc_point_t       *public_key,
                           pka_operand_t     *hash,
                           dsa_signature_t   *signature)
{
    pka_comparison_t comparison;
    gxcr_pka_err_t   rc;

    rc = gxcr_pka_ecdsa_verify(handle, NULL, curve, base_pt, base_pt_order,
                               public_key, hash, signature);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_ecdsa_verify failed rc=%d\n", rc);
        return FAILURE;
    }

    comparison = get_compare_result(handle);
    if (comparison == PKA_EQUAL)
        return SUCCESS;

    if (comparison == PKA_NO_COMPARE)
        printf("sync_ecdsa_verify failed in get_compare_result\n");

    return FAILURE;
}

dsa_signature_t *sync_dsa_gen(gxcr_pka_handle_t *handle,
                              pka_operand_t     *p,
                              pka_operand_t     *q,
                              pka_operand_t     *g,
                              pka_operand_t     *private_key,
                              pka_operand_t     *hash,
                              pka_operand_t     *k)
{
    dsa_signature_t *signature;
    gxcr_pka_err_t   rc;
    uint32_t         buf_len;

    rc = gxcr_pka_dsa_gen(handle, NULL, p, q, g, private_key, hash, k);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_dsa_gen failed rc=%d\n", rc);
        return NULL;
    }

    buf_len   = p->buf_len;
    signature = malloc_dsa_signature(handle, buf_len, buf_len);
    if (SUCCESS != get_results(handle, signature->r, signature->s))
        return NULL;

    return signature;
}

status_t sync_dsa_verify(gxcr_pka_handle_t *handle,
                         pka_operand_t     *p,
                         pka_operand_t     *q,
                         pka_operand_t     *g,
                         pka_operand_t     *public_key,
                         pka_operand_t     *hash,
                         dsa_signature_t   *signature)
{
    pka_comparison_t comparison;
    gxcr_pka_err_t   rc;

    rc = gxcr_pka_dsa_verify(handle, NULL, p, q, g, public_key, hash,
                             signature);
    if (rc != PKA_NO_ERROR)
    {
        printf("sync_dsa_verify failed rc=%d\n", rc);
        return FAILURE;
    }

    comparison = get_compare_result(handle);
    if (comparison == PKA_EQUAL)
        return SUCCESS;

    if (comparison == PKA_NO_COMPARE)
        printf("sync_dsa_verify failed in sync_dsa_verify\n");

    return FAILURE;
}



uint32_t num_trailing_zeros(pka_operand_t *value)
{
    uint32_t byte_len, trailing_zeros, byte_idx, index, bit_idx;
    uint8_t  byte;

    // Find the number of trailing zero bits in value.
    byte_len       = value->actual_len;
    trailing_zeros = 0;
    for (byte_idx = 0;  byte_idx < byte_len;  byte_idx++)
    {
        if (value->big_endian)
            index = (byte_len - 1) - byte_idx;
        else
            index = byte_idx;

        byte = value->buf_ptr[index];
        if (byte != 0)
        {
            for (bit_idx = 0;  bit_idx < 8;  bit_idx++)
            {
                if ((byte & (1 << bit_idx)) != 0)
                    break;

                trailing_zeros++;
            }

            return trailing_zeros;
        }

        trailing_zeros += 8;
    }

    // If we reach here then value is all zeros.
    return 0;
}

boolean_t miller_rabin_test(gxcr_pka_handle_t *handle,
                            pka_operand_t     *witness,        // aka a
                            pka_operand_t     *prime,
                            pka_operand_t     *prime_minus_1,
                            pka_operand_t     *odd,            // aka d
                            uint32_t           trailing_zeros,
                            boolean_t          debug)
{
    pka_operand_t *mod_exp, *squared;
    boolean_t      is_one, is_minus_one;
    uint32_t       idx;

    // Assert (1 <= trailing_zeros);
    mod_exp = sync_mod_exp(handle, odd, prime, witness);

    // If mod_exp == 1 || mod_exp == prime_minus_1 then is a probable prime
    // so return TRUE.  Otherwise keep testing the modular squares of mod_exp.
    is_one       = pka_compare(mod_exp, &ONE)          == PKA_EQUAL;
    is_minus_one = pka_compare(mod_exp, prime_minus_1) == PKA_EQUAL;
    if (is_minus_one || is_one)
    {
        free_operand(&mod_exp);
        return TRUE;
    }

    squared = NULL;
    for (idx = 1;  idx < trailing_zeros;  idx++)
    {
        squared      = sync_mod_multiply(handle, mod_exp, mod_exp, prime);
        is_one       = pka_compare(squared, &ONE)          == PKA_EQUAL;
        is_minus_one = pka_compare(squared, prime_minus_1) == PKA_EQUAL;
        free_operand(&mod_exp);
        mod_exp = squared;

        if (is_minus_one || is_one)
        {
            free_operand(&mod_exp);
            return is_minus_one;
        }
    }

    free_operand(&mod_exp);
    return FALSE;
}

boolean_t is_prime(gxcr_pka_handle_t *handle,
                   pka_operand_t     *prime,    // aka possible_prime
                   uint32_t           iterations,
                   boolean_t          should_be_prime)
{
    pka_operand_t *prime_minus_1, *odd, *witness;
    boolean_t      result, prime_test;
    uint32_t       trailing_zeros, cnt;

    prime_minus_1 = sync_subtract(handle, prime, &ONE);

    // Find the number of trailing zero bits in prime_minus_1.
    trailing_zeros = num_trailing_zeros(prime_minus_1);

    // Divide prime_minus_1 by 2^trailing_zeros, which is equivalent to a
    // right shift by trailing_zeros.  This right shift is done first by
    // dropping the bottom "trailing_zeros/8" bytes and then right shifting
    // this value by "trailing_zeros & 7".
    odd    = sync_shift_right(handle, prime_minus_1, trailing_zeros);
    result = TRUE;

    for (cnt = 1;  cnt <= iterations;  cnt++)
    {
        witness = rand_non_zero_integer(handle, prime_minus_1);

        // g = gcd(b, prime);
        // if (! IsZero(g))
        //     return 0;
        prime_test = miller_rabin_test(handle, witness, prime, prime_minus_1,
                                       odd, trailing_zeros, FALSE);

        if ((prime_test == FALSE) && should_be_prime)
        {
            printf("Composite found cnt=%u\n", cnt);
            print_pka_operand("prime       =", prime,   "\n");
            print_pka_operand("witness     =", witness, "\n");
        }

        free_operand(&witness);
        if (prime_test == FALSE)
        {
            result = FALSE;
            break;
        }
    }

    free_operand(&odd);
    free_operand(&prime_minus_1);
    return result;
}



boolean_t signatures_are_equal(dsa_signature_t *signature1,
                               dsa_signature_t *signature2)
{
    pka_comparison_t compare1, compare2;

    compare1 = pka_compare(signature1->r, signature2->r);
    compare2 = pka_compare(signature1->s, signature2->s);
    return (compare1 == PKA_EQUAL) && (compare2 == PKA_EQUAL);
}

boolean_t ecc_points_are_equal(gxcr_pka_handle_t *handle,
                               ecc_point_t       *pointA,
                               ecc_point_t       *pointB)
{
    pka_comparison_t compare_x, compare_y;

    compare_x = pka_compare(pointA->x, pointB->x);
    compare_y = pka_compare(pointA->y, pointB->y);
    return (compare_x == PKA_EQUAL) && (compare_y == PKA_EQUAL);
}

boolean_t sw_is_valid_curve(gxcr_pka_handle_t *handle, ecc_curve_t *curve)
{
    pka_operand_t *const_4, *const_27, *a_squared, *a_cubed, *b_squared;
    pka_operand_t *a_cubed_by_4, *b_sqrd_by_27, *final_sum;
    boolean_t      result;

    // Use bignum arithmetic check the following:
    // verify that "(4*a^3 + 27*b^2) mod p" is not equal to zero.
    const_4              = malloc_operand(handle, 1);
    const_4->buf_ptr[0]  = 4;
    const_4->actual_len  = 1;
    const_27             = malloc_operand(handle, 1);
    const_27->buf_ptr[0] = 27;
    const_27->actual_len = 1;

    a_squared = sw_mod_multiply(handle, curve->a,  curve->a, curve->p);
    a_cubed   = sw_mod_multiply(handle, a_squared, curve->a, curve->p);
    b_squared = sw_mod_multiply(handle, curve->b,  curve->b, curve->p);

    a_cubed_by_4 = sw_mod_multiply(handle, a_cubed,   const_4,    curve->p);
    b_sqrd_by_27 = sw_mod_multiply(handle, b_squared, const_27,   curve->p);
    final_sum    = sw_mod_add(handle, a_cubed_by_4, b_sqrd_by_27, curve->p);

    if (final_sum != NULL)
        result = final_sum->buf_len != 0;
    else
        result = FALSE;

    free_operand(&const_4);
    free_operand(&const_27);
    free_operand(&a_squared);
    free_operand(&a_cubed);
    free_operand(&b_squared);
    free_operand(&a_cubed_by_4);
    free_operand(&b_sqrd_by_27);
    free_operand(&final_sum);

    return result;
}

boolean_t is_point_on_curve(gxcr_pka_handle_t *handle,
                            ecc_curve_t       *curve,
                            ecc_point_t       *point)
{
    pka_comparison_t comparison;
    pka_operand_t   *y_squared, *x_squared, *x_cubed, *x_times_a, *temp;
    pka_operand_t   *rhs;

    // Need to compare "y^2 mod p" with "x^3 + a*x + b mod p"
    y_squared = sw_mod_multiply(handle, point->y,  point->y, curve->p);
    x_squared = sw_mod_multiply(handle, point->x,  point->x, curve->p);
    x_cubed   = sw_mod_multiply(handle, x_squared, point->x, curve->p);
    x_times_a = sw_mod_multiply(handle, point->x,  curve->a, curve->p);

    temp       = sw_mod_add(handle, x_cubed, x_times_a, curve->p);
    rhs        = sw_mod_add(handle, temp,    curve->b,  curve->p);
    comparison = pka_compare(y_squared, rhs);

    if (comparison != PKA_EQUAL)
    {
        printf(" is_point_on_curve comparison=%u\n", comparison);
        print_pka_operand("x              =", point->x,  "\n");
        print_pka_operand("y              =", point->y,  "\n");
        print_pka_operand("y^2 mod p      =", y_squared, "\n");
        print_pka_operand("x^2 mod p      =", x_squared, "\n");
        print_pka_operand("x^3 mod p      =", x_cubed,   "\n");
        print_pka_operand("a*x mod p      =", x_times_a, "\n");
        print_pka_operand("x^3 + a*x mod p=", temp,      "\n");
        print_pka_operand("b              =", curve->b,  "\n");
        print_pka_operand("rhs            =", rhs,       "\n");
    }

    free_operand(&y_squared);
    free_operand(&x_squared);
    free_operand(&x_cubed);
    free_operand(&x_times_a);
    free_operand(&temp);
    free_operand(&rhs);

    return comparison == PKA_EQUAL;
}



pka_operand_t *sw_add(gxcr_pka_handle_t *handle,
                      pka_operand_t     *value,
                      pka_operand_t     *addend)
{
    pka_operand_t *result;
    uint32_t       value_byte_len, addend_byte_len, result_byte_len;
    uint32_t       value_byte, addend_byte, sum_byte, carry, idx, cnt;
    uint32_t       val_idx, add_idx, res_idx, final_len;
    uint8_t       *value_buf_ptr, *addend_buf_ptr, *result_buf_ptr;
    uint8_t        big_endian;

    big_endian = gxcr_pka_get_endian(handle);
    if ((value->big_endian != big_endian) || (addend->big_endian != big_endian))
    {
        printf("sw_add endian mismatch. handle endian=%u value=%u addend=%u\n",
               big_endian, value->big_endian, addend->big_endian);
        return NULL;
    }

    value_byte_len  = value->actual_len;
    value_buf_ptr   = value->buf_ptr;
    addend_byte_len = addend->actual_len;
    addend_buf_ptr  = addend->buf_ptr;
    result_byte_len = MAX(value_byte_len, addend_byte_len) + 1;
    result          = malloc_operand(handle, result_byte_len);
    result_buf_ptr  = result->buf_ptr;

    if (big_endian)
    {
        carry = 0;
        for (cnt = 0;  cnt < result_byte_len - 1;  cnt++)
        {
            val_idx     = (value_byte_len  - cnt) - 1;
            add_idx     = (addend_byte_len - cnt) - 1;
            res_idx     = (result_byte_len - cnt) - 1;
            value_byte  = (cnt < value_byte_len)  ? value_buf_ptr[val_idx]  : 0;
            addend_byte = (cnt < addend_byte_len) ? addend_buf_ptr[add_idx] : 0;
            sum_byte    = value_byte + addend_byte + carry;
            carry       = sum_byte >> 8;

            result_buf_ptr[res_idx] = (uint8_t) (sum_byte & 0xFF);
        }

        result_buf_ptr[0] = carry;
        final_len         = result_byte_len;
    }
    else
    {
        carry = 0;
        for (idx = 0;  idx < result_byte_len - 1;  idx++)
        {
            value_byte  = (idx < value_byte_len)  ? value_buf_ptr[idx]  : 0;
            addend_byte = (idx < addend_byte_len) ? addend_buf_ptr[idx] : 0;
            sum_byte    = value_byte + addend_byte + carry;
            carry       = sum_byte >> 8;
            result_buf_ptr[idx] = (uint8_t) (sum_byte & 0xFF);
        }

        result_buf_ptr[result_byte_len - 1] = carry;

        // Now determine result's actual_len by going backwards (i.e. from MSB
        // to LSB).
        for (idx = result_byte_len - 1;  idx != 0;  idx--)
            if (result_buf_ptr[idx] != 0)
                break;

        final_len = idx + 1;
    }

    result->actual_len = final_len;
    result->big_endian = big_endian;
    return result;
}



pka_operand_t *sw_subtract(gxcr_pka_handle_t *handle,
                           pka_operand_t     *value,
                           pka_operand_t     *subtrahend)
{
    pka_operand_t *diff, *result;
    uint32_t       minuend_byte_len, subtrahend_byte_len, diff_byte_len;
    uint32_t       borrow, minuend_byte, subtrahend_byte, diff_byte;
    uint32_t       byte_cnt;
    uint8_t       *minuend_ptr, *subtrahend_ptr, *diff_ptr, big_endian;

    big_endian = gxcr_pka_get_endian(handle);
    if ((value->big_endian      != big_endian) ||
        (subtrahend->big_endian != big_endian))
    {
        printf("sw_subtract endian mismatch. handle=%u value=%u subtra=%u\n",
               big_endian, value->big_endian, subtrahend->big_endian);
        return NULL;
    }

    Assert(pka_compare(value, subtrahend) != PKA_LESS_THAN);
    minuend_byte_len    = value->actual_len;
    subtrahend_byte_len = subtrahend->actual_len;
    diff_byte_len       = minuend_byte_len;
    diff                = malloc_operand(handle, diff_byte_len);
    diff->big_endian    = big_endian;
    diff->actual_len    = diff_byte_len;

    if (big_endian)
    {
        minuend_ptr    = &value->buf_ptr[minuend_byte_len  - 1];
        subtrahend_ptr = &subtrahend->buf_ptr[subtrahend_byte_len - 1];
        diff_ptr       = &diff->buf_ptr[diff_byte_len - 1];
    }
    else
    {
        minuend_ptr    = &value->buf_ptr[0];
        subtrahend_ptr = &subtrahend->buf_ptr[0];
        diff_ptr       = &diff->buf_ptr[0];
    }

    // Subtract subtrahend from minued by proceeding from the least significant
    // bytes to the most significant bytes.
    borrow = 0;
    for (byte_cnt = 0;  byte_cnt < minuend_byte_len;  byte_cnt++)
    {
        minuend_byte = *minuend_ptr;
        if (byte_cnt < subtrahend_byte_len)
            subtrahend_byte = (*subtrahend_ptr) + borrow;
        else
            subtrahend_byte = borrow;

        if (subtrahend_byte <= minuend_byte)
        {
            diff_byte = minuend_byte - subtrahend_byte;
            borrow    = 0;
        }
        else
        {
            diff_byte = (256 + minuend_byte) - subtrahend_byte;
            borrow    = 1;
        }

        *diff_ptr = diff_byte;
        if (big_endian)
        {
            minuend_ptr--;
            subtrahend_ptr--;
            diff_ptr--;
        }
        else
        {
            minuend_ptr++;
            subtrahend_ptr++;
            diff_ptr++;
        }
    }

    result = dup_operand(handle, diff);
    free_operand(&diff);
    return result;
}

pka_operand_t *sw_multiply(gxcr_pka_handle_t *handle,
                           pka_operand_t     *value,
                           pka_operand_t     *multiplier)
{
    pka_operand_t *product, *result;
    boolean_t      big_endian;
    uint32_t       val_byte_len, mul_byte_len, product_byte_len;
    uint32_t       val_cnt, val_idx, mul_cnt, mul_idx, product_idx;
    uint32_t       value_byte, mul_byte, product_byte, prod_byte, carry;
    uint8_t       *val_buf_ptr, *mul_buf_ptr, *product_buf_ptr;

    big_endian = gxcr_pka_get_endian(handle);
    if ((value->big_endian      != big_endian) ||
        (multiplier->big_endian != big_endian))
    {
        printf("sw_multiply endian mismatch. handle=%u value=%u mult=%u\n",
               big_endian, value->big_endian, multiplier->big_endian);
        return NULL;
    }

    val_byte_len        = value->actual_len;
    val_buf_ptr         = value->buf_ptr;
    mul_byte_len        = multiplier->actual_len;
    mul_buf_ptr         = multiplier->buf_ptr;
    product_byte_len    = val_byte_len + mul_byte_len;
    product             = malloc_operand(handle, product_byte_len);
    product->actual_len = product_byte_len;
    product->big_endian = big_endian;
    product_buf_ptr     = product->buf_ptr;

    for (val_cnt = 0; val_cnt < val_byte_len; val_cnt++)
    {
        val_idx    = big_endian ? ((val_byte_len - 1) - val_cnt) : val_cnt;
        value_byte = val_buf_ptr[val_idx];
        carry      = 0;

        // Use separate loops for big and little endian cases to speed the
        // sw_multiply up a bit (maybe 2x?)
        if (big_endian)
        {
            for (mul_cnt = 0; mul_cnt < mul_byte_len; mul_cnt++)
            {
                mul_idx      = (mul_byte_len - 1) - mul_cnt;
                mul_byte     = mul_buf_ptr[mul_idx];
                product_idx  = (product_byte_len - 1) - (val_cnt + mul_cnt);
                product_byte = product_buf_ptr[product_idx];
                prod_byte    = product_byte + (value_byte * mul_byte) + carry;
                carry        = prod_byte >> 8;

                product_buf_ptr[product_idx] = (uint8_t) (prod_byte & 0xFF);
            }

            product_idx = (product_byte_len - 1) - (val_cnt + mul_byte_len);
        }
        else
        {
            for (mul_cnt = 0; mul_cnt < mul_byte_len; mul_cnt++)
            {
                mul_idx      = mul_cnt;
                mul_byte     = mul_buf_ptr[mul_idx];
                product_idx  = val_cnt + mul_cnt;
                product_byte = product_buf_ptr[product_idx];
                prod_byte    = product_byte + (value_byte * mul_byte) + carry;
                carry        = prod_byte >> 8;

                product_buf_ptr[product_idx] = (uint8_t) (prod_byte & 0xFF);
            }

            product_idx = val_cnt + mul_byte_len;
        }

        product_buf_ptr[product_idx] = carry;
    }

    result = dup_operand(handle, product);
    free_operand(&product);
    return result;
}



static void set_actual_len(pka_operand_t *operand, uint8_t big_endian)
{
    uint32_t byte_len, leading_zeros;

    if (pka_is_zero(operand))
    {
        operand->actual_len = 1;
        return;
    }

    leading_zeros = count_leading_zeros(operand, big_endian);
    if (leading_zeros == 0)
        return;

    byte_len = operand->actual_len;
    if (byte_len == 0)
        byte_len = operand->buf_len;

    operand->actual_len = byte_len - leading_zeros;
    if (big_endian)
        operand->buf_ptr += leading_zeros;
}



// Update dividend in place with "dividend - (divisor * quotient)",
// where quotient is "quot_byte * 2^(8 * quot_byte_shift)".  Note that
// this routine requires "divisor * quotient <= dividend", i.e. the quotient
// MUST be such that the dividend does not go negative!

static void subtract_product(pka_operand_t *dividend,
                             pka_operand_t *divisor,
                             uint32_t       quot_byte,
                             uint32_t       quot_byte_shift,
                             uint8_t        big_endian)
{
    uint32_t divisor_byte_len, dividend_byte_len, carry, borrow;
    uint32_t divisor_byte, dividend_byte, product, prod_byte, result_byte;
    uint32_t byte_cnt;
    uint8_t *divisor_byte_ptr, *dividend_byte_ptr;

    divisor_byte_len  = divisor->actual_len;
    dividend_byte_len = dividend->actual_len;
    if ((dividend_byte_len == divisor_byte_len) && (quot_byte == 1))
    {
        // Optimize the case where dividend == divisor.
        if (pka_compare(dividend, divisor) == PKA_EQUAL)
        {
            memset(dividend->buf_ptr, 0, dividend->actual_len);
            dividend->actual_len = 1;
            return;
        }
    }

    if (big_endian)
    {
        divisor_byte_ptr  = &divisor->buf_ptr[divisor_byte_len  - 1];
        dividend_byte_ptr = &dividend->buf_ptr[(dividend_byte_len - 1) -
                                               quot_byte_shift];
    }
    else
    {
        divisor_byte_ptr  = &divisor->buf_ptr[0];
        dividend_byte_ptr = &dividend->buf_ptr[quot_byte_shift];
    }

    // Now multiply divisor by quot_byte and subtract it from dividend.
    // This code proceeds from least significant byte to most significant
    // byte.
    carry  = 0;
    borrow = 0;
    for (byte_cnt = 0;  byte_cnt < divisor_byte_len;  byte_cnt++)
    {
        divisor_byte  = *divisor_byte_ptr;
        dividend_byte = *dividend_byte_ptr;

        // Note that since quot_byte, divisor_byte and carry in are all <= 255,
        // this implies that product <= 0xFF00, so carry out <= 255.
        product   = (quot_byte * divisor_byte) + carry;
        prod_byte = (product & 0xFF) + borrow;
        carry     = product >> 8;
        if (prod_byte <= dividend_byte)
        {
            result_byte = dividend_byte - prod_byte;
            borrow      = 0;
        }
        else
        {
            result_byte = (256 + dividend_byte) - prod_byte;
            borrow      = 1;
        }

        *dividend_byte_ptr = result_byte;

        if (big_endian)
        {
            divisor_byte_ptr--;
            dividend_byte_ptr--;
        }
        else
        {
            divisor_byte_ptr++;
            dividend_byte_ptr++;
        }
    }

    if ((carry != 0) || (borrow != 0))
    {
        dividend_byte = *dividend_byte_ptr;
        prod_byte     = carry + borrow;
        Assert(prod_byte <= dividend_byte);
        *dividend_byte_ptr = dividend_byte - prod_byte;
    }

    // trim result of leading zeros to have the correct length.
    set_actual_len(dividend, big_endian);
}



// This function returns the most significant two bytes of the operand, as an
// integer in the range 0x100 .. 0xFFFF.  If the operand only has a length
// of 1, return this byte * 0x100 (i.e. act as if the length was 2, with the
// extra byte being zero).

static uint32_t get_two_ms_bytes(pka_operand_t *operand, uint8_t big_endian)
{
    uint32_t result, byte_len, msb_idx, next_idx;

    byte_len = pka_operand_byte_len(operand);
    msb_idx  = pka_get_msb_idx(operand, big_endian);

    Assert(byte_len != 0);
    result = operand->buf_ptr[msb_idx] << 8;
    if (byte_len <= 1)
        return result;

    if (big_endian)
        next_idx = msb_idx + 1;
    else
        next_idx = msb_idx - 1;

    result += operand->buf_ptr[next_idx];
    return result;
}

// This function returns the most significant three bytes of the operand, as an
// integer in the range 0x10000 .. 0xFFFFFF.  If the operand only has a length
// of 1, return this byte * 0x10000 (i.e. act as if the length was 3, with the
// extra bytes being zero).  Similarly, if the operand length is 2, then return
// the most significant two bytes * 0x100.

static uint32_t get_three_ms_bytes(pka_operand_t *operand,
                                   uint32_t       two_ms_bytes,
                                   uint8_t        big_endian)
{
  uint32_t result, byte_len, msb_idx, next_idx;

    byte_len = pka_operand_byte_len(operand);
    result   = two_ms_bytes << 8;
    if (byte_len < 3)
        return result;

    msb_idx = pka_get_msb_idx(operand, big_endian);
    if (big_endian)
        next_idx = msb_idx + 2;
    else
        next_idx = msb_idx - 2;

    result += operand->buf_ptr[next_idx];
    return result;
}



static pka_comparison_t cmp_ms_dividend_to_ms_divisor(pka_operand_t *dividend,
                                                      pka_operand_t *divisor,
                                                      uint8_t        big_endian)
{
    uint32_t divisor_byte_len, divisor_msb_idx, dividend_msb_idx, byte_cnt;
    uint8_t *divisor_ptr, *dividend_ptr, divisor_byte, dividend_byte;

    divisor_byte_len  = pka_operand_byte_len(divisor);
    divisor_msb_idx   = pka_get_msb_idx(divisor, big_endian);
    dividend_msb_idx  = pka_get_msb_idx(dividend, big_endian);

    divisor_ptr  = &divisor->buf_ptr[divisor_msb_idx];
    dividend_ptr = &dividend->buf_ptr[dividend_msb_idx];

    // Compare the most significant "divisor_byte_len" bytes of the dividend
    // to the divisor, starting at the most significant bytes.
    for (byte_cnt = 0;  byte_cnt < divisor_byte_len;  byte_cnt++)
    {
        divisor_byte  = *divisor_ptr;
        dividend_byte = *dividend_ptr;
        if (dividend_byte < divisor_byte)
            return PKA_LESS_THAN;
        else if (dividend_byte > divisor_byte)
            return PKA_GREATER_THAN;

        if (big_endian)
        {
            divisor_ptr++;
            dividend_ptr++;
        }
        else
        {
            divisor_ptr--;
            dividend_ptr--;
        }
    }

    return PKA_EQUAL;
}

static void add_quot_byte(pka_operand_t *quotient,
                          uint32_t       quot_byte,
                          uint32_t       quot_byte_shift,
                          uint8_t        big_endian)
{
    uint32_t quot_len, idx, old_quot_byte, new_quot_byte;

    quot_len = quotient->actual_len;
    Assert(quot_byte_shift < quot_len);

    if (big_endian)
        idx = (quot_len - 1) - quot_byte_shift;
    else
        idx = quot_byte_shift;

    // Add carry into subsequently more significant bytes?
    old_quot_byte = quotient->buf_ptr[idx];
    new_quot_byte = old_quot_byte + quot_byte;
    Assert(new_quot_byte <= 255);
    quotient->buf_ptr[idx] = new_quot_byte;
}



static void bignum_div_mod(pka_operand_t *dividend,
                           pka_operand_t *divisor,
                           pka_operand_t *quotient,
                           uint8_t        big_endian)
{
    pka_comparison_t cmp;
    uint32_t         divisor_len, ms_divisor, ms_dividend, quot_byte_shift;
    uint32_t         quot_byte;

    Assert(dividend->actual_len <= (pka_operand_byte_len(dividend) + 4));
    divisor_len = pka_operand_byte_len(divisor);
    ms_divisor  = get_two_ms_bytes(divisor, big_endian);
    Assert((0x100 <= ms_divisor) && (ms_divisor <= 0xFFFF));

    while (pka_compare(dividend, divisor) != PKA_LESS_THAN)
    {
        // Note that quot_byte_idx MUST be >= 0, as a consequence of the test
        // above showing that divisor <= dividend.
        ms_dividend     = get_two_ms_bytes(dividend, big_endian);
        quot_byte_shift = pka_operand_byte_len(dividend) - divisor_len;
        if (ms_dividend == ms_divisor)
            // Compare the "divisor_len" most significant bytes of dividend
            // to the divisor.
            cmp = cmp_ms_dividend_to_ms_divisor(dividend, divisor, big_endian);
        else if (ms_dividend < ms_divisor)
            cmp = PKA_LESS_THAN;
        else
            cmp = PKA_GREATER_THAN;

        if (cmp == PKA_LESS_THAN)
        {
            quot_byte_shift--;
            ms_dividend = get_three_ms_bytes(dividend, ms_dividend, big_endian);
            quot_byte   = ms_dividend / (ms_divisor + 1);
        }
        else if ((cmp == PKA_EQUAL) || (ms_dividend == ms_divisor))
            quot_byte = 1;
        else  // cmp == PKA_GREATER_THAN, which implies ms_divisor < 0xFFFF.
            quot_byte = ms_dividend / (ms_divisor + 1);

        // quot_byte here is guaranteed to be <= the "real" quotient byte,
        // and probably not more than 1 less than the "real" quotient byte.
        Assert((1 <= quot_byte) && (quot_byte <= 255));
        subtract_product(dividend, divisor, quot_byte, quot_byte_shift,
                         big_endian);

        add_quot_byte(quotient, quot_byte, quot_byte_shift, big_endian);
    }
}



void sw_divide_with_remainder(gxcr_pka_handle_t *handle,
                              pka_operand_t     *value,
                              pka_operand_t     *divisor,
                              quot_and_remain_t *quot_and_remain)
{
    pka_comparison_t comparison;
    pka_operand_t   *dividend, *quotient;
    uint32_t         quot_buf_len, value_byte_len, divisor_byte_len;
    uint32_t         value_uint32, divisor_uint32, quotient_uint32;
    uint32_t         remainder_uint32;
    uint8_t         *orig_div_buf_ptr, *orig_quot_buf_ptr, big_endian;

    comparison = pka_compare(value, divisor);
    if (comparison == PKA_LESS_THAN)
    {
        quot_and_remain->quotient  = dup_operand(handle, &ZERO);
        quot_and_remain->remainder = dup_operand(handle, value);
        return;
    }
    else if (comparison == PKA_EQUAL)
    {
        quot_and_remain->quotient  = dup_operand(handle, &ONE);
        quot_and_remain->remainder = dup_operand(handle, &ZERO);
        return;
    }
    else if (pka_is_one(divisor))
    {
        quot_and_remain->quotient  = dup_operand(handle, value);
        quot_and_remain->remainder = dup_operand(handle, &ZERO);
        return;
    }

    big_endian       = value->big_endian;
    value_byte_len   = pka_operand_byte_len(value);
    divisor_byte_len = pka_operand_byte_len(divisor);
    if ((value_byte_len < 4) && (divisor_byte_len < 4))
    {
        to_uint32_integer(value,   &value_uint32);
        to_uint32_integer(divisor, &divisor_uint32);
        quotient_uint32            = value_uint32 / divisor_uint32;
        remainder_uint32           = value_uint32 % divisor_uint32;
        quot_and_remain->quotient  = uint32_to_operand(handle, quotient_uint32,
                                                       big_endian);
        quot_and_remain->remainder = uint32_to_operand(handle, remainder_uint32,
                                                       big_endian);
        return;
    }

    dividend         = dup_operand(handle, value);
    orig_div_buf_ptr = dividend->buf_ptr;
    quot_buf_len     = (pka_operand_byte_len(dividend) + 1) -
                        pka_operand_byte_len(divisor);
    quotient         = malloc_operand(handle, quot_buf_len);

    quotient->actual_len = quot_buf_len;
    orig_quot_buf_ptr    = quotient->buf_ptr;

    bignum_div_mod(dividend, divisor, quotient, big_endian);

    // Allocate final results.
    quot_and_remain->quotient  = dup_operand(handle, quotient);
    quot_and_remain->remainder = dup_operand(handle, dividend);

    // Free temporaries.
    quotient->buf_ptr = orig_quot_buf_ptr;
    dividend->buf_ptr = orig_div_buf_ptr;
    free_operand(&quotient);
    free_operand(&dividend);
}

pka_operand_t *sw_divide(gxcr_pka_handle_t *handle,
                         pka_operand_t     *value,
                         pka_operand_t     *divisor)
{
    quot_and_remain_t quot_and_remain;

    sw_divide_with_remainder(handle, value, divisor, &quot_and_remain);
    free_operand(&quot_and_remain.remainder);
    return quot_and_remain.quotient;
}

pka_operand_t *sw_modulo(gxcr_pka_handle_t *handle,
                         pka_operand_t     *value,
                         pka_operand_t     *modulus)
{
    quot_and_remain_t quot_and_remain;

    sw_divide_with_remainder(handle, value, modulus, &quot_and_remain);
    free_operand(&quot_and_remain.quotient);
    return quot_and_remain.remainder;
}

pka_operand_t *sw_left_shift(gxcr_pka_handle_t *handle,
                             pka_operand_t     *value,
                             uint32_t           shift_cnt)
{
    pka_operand_t *result;
    uint32_t       value_bit_len, value_byte_len, result_bit_len, byte_shift;
    uint32_t       result_byte_len, bit_shift, cnt, value_idx, result_idx;
    uint32_t       value_byte, result_byte, shift_out, temp;
    uint8_t        big_endian;

    big_endian         = value->big_endian;
    value_bit_len      = pka_operand_bit_len(value);
    value_byte_len     = (value_bit_len + 7) / 8;
    result_bit_len     = value_bit_len + shift_cnt;
    result_byte_len    = (result_bit_len + 7) / 8;
    result             = malloc_operand(handle, result_byte_len);
    result->actual_len = result_byte_len;
    result->big_endian = big_endian;

    byte_shift = shift_cnt / 8;
    bit_shift  = shift_cnt & 0x7;
    if (bit_shift == 0)
    {
        if (big_endian)
            memcpy(&result->buf_ptr[0], value->buf_ptr, value_byte_len);
        else
            memcpy(&result->buf_ptr[byte_shift], value->buf_ptr,
                   value_byte_len);
        return result;
    }

    // Loop from LSB to MSB.
    shift_out = 0;
    Assert(value_byte_len != 0);
    for (cnt = 0;  cnt < value_byte_len;  cnt++)
    {
        if (big_endian)
        {
            value_idx  = (value_byte_len  - 1) - cnt;
            result_idx = (result_byte_len - 1) - (cnt + byte_shift);
        }
        else
        {
            value_idx  = cnt;
            result_idx = cnt + byte_shift;
        }

        value_byte  = value->buf_ptr[value_idx];
        temp        = (value_byte << bit_shift) | shift_out;
        result_byte = temp & 0xFF;
        shift_out   = temp >> 8;
        result->buf_ptr[result_idx] = result_byte;
    }

    if (shift_out != 0)
    {
        if (big_endian)
            result->buf_ptr[result_idx - 1] = shift_out;
        else
            result->buf_ptr[result_idx + 1] = shift_out;
    }

    return result;
}

pka_operand_t *sw_right_shift(gxcr_pka_handle_t *handle,
                              pka_operand_t     *value,
                              uint32_t           shift_cnt)
{
    pka_operand_t *result;
    uint32_t       value_bit_len, value_byte_len, result_bit_len, byte_shift;
    uint32_t       result_byte_len, bit_shift, copy_len, shift_out, cnt;
    uint32_t       value_idx, result_idx, value_byte, result_byte;
    uint8_t        big_endian;

    value_bit_len = pka_operand_bit_len(value);
    if (value_bit_len <= shift_cnt)
        return dup_operand(handle, &ZERO);

    big_endian         = value->big_endian;
    value_byte_len     = (value_bit_len + 7) / 8;
    result_bit_len     = value_bit_len - shift_cnt;
    result_byte_len    = (result_bit_len + 7) / 8;
    result             = malloc_operand(handle, result_byte_len);
    result->actual_len = result_byte_len;
    result->big_endian = big_endian;

    byte_shift = shift_cnt / 8;
    bit_shift  = shift_cnt & 0x7;
    if (bit_shift == 0)
    {
        copy_len = value_byte_len - byte_shift;
        if (big_endian)
            memcpy(result->buf_ptr, value->buf_ptr, copy_len);
        else
            memcpy(result->buf_ptr, &value->buf_ptr[byte_shift], copy_len);
        return result;
    }

    // Loop from MSB to LSB.
    shift_out = 0;
    for (cnt = 0;  cnt < result_byte_len;  cnt++)
    {
        if (big_endian)
        {
            value_idx  = cnt;
            result_idx = cnt;
        }
        else
        {
            value_idx  = (value_byte_len  - 1) - cnt;
            result_idx = (result_byte_len - 1) - cnt;
        }

        value_byte  = value->buf_ptr[value_idx];
        result_byte = shift_out | (value_byte >> bit_shift);
        shift_out   = value_byte << (8 - bit_shift);
        result->buf_ptr[result_idx] = result_byte;
    }

    return result;
}



uint8_t get_bit(pka_operand_t *operand, uint32_t bit_idx)
{
    uint32_t byte_idx, bit_in_byte_idx;
    uint8_t  byte, bit;

    Assert((bit_idx / 8) <= (operand->actual_len - 1));
    bit_in_byte_idx = bit_idx & 7;
    if (operand->big_endian)
        byte_idx = (operand->actual_len - 1) - (bit_idx / 8);
    else
        byte_idx = bit_idx / 8;

    byte = operand->buf_ptr[byte_idx];
    bit  = (byte >> bit_in_byte_idx) & 0x1;
    return bit;
}



pka_operand_t *sw_mod_exp(gxcr_pka_handle_t *handle,
                          pka_operand_t     *exponent,
                          pka_operand_t     *modulus,
                          pka_operand_t     *msg)
{
    pka_operand_t *base, *result, *new_base, *new_result;
    boolean_t      result_is_1;
    uint32_t       exp_bit_len, exp_bit_idx;
    uint8_t        big_endian;

    big_endian = msg->big_endian;
    Assert(exponent->big_endian == big_endian);
    Assert(modulus->big_endian  == big_endian);

    // Copy msg into base and get the bit_len of the exponent.
    base        = dup_operand(handle, msg);
    exp_bit_len = pka_operand_bit_len(exponent);

    // Initialize result to be 1.
    result_is_1 = TRUE;
    result      = NULL;

    for (exp_bit_idx = 0;  exp_bit_idx < exp_bit_len;  exp_bit_idx++)
    {
        if (get_bit(exponent, exp_bit_idx) != 0)
        {
            // Multiply by result by base.  Special case for the first
            // multiplication, where result is one.
            if (result_is_1)
            {
                result      = dup_operand(handle, base);
                result_is_1 = FALSE;
            }
            else
            {
                new_result = sw_mod_multiply(handle, result, base, modulus);
                free_operand(&result);
                result = new_result;
            }
        }

        // Square base.
        new_base = sw_mod_multiply(handle, base, base, modulus);
        free_operand(&base);
        base = new_base;
    }

    free_operand(&base);
    return result;
}

pka_operand_t *sw_exp_with_crt(gxcr_pka_handle_t *handle,
                               pka_operand_t     *p,
                               pka_operand_t     *q,
                               pka_operand_t     *msg,
                               pka_operand_t     *d_p,
                               pka_operand_t     *d_q,
                               pka_operand_t     *qinv)
{
    pka_operand_t *m1, *m2, *abs_diff, *mdiff, *h, *h_times_q, *result;
    boolean_t      m1_ge_m2;

    // Assert(d_q < q < p);  Assert(d_p < p);  Assert(qinv < p);
    // Assert(c < p*q);
    Assert(pka_compare(q, p) == PKA_LESS_THAN);

    // Calculate "m1 = c^d_p mod p", "m2 = c^d_q mod q",
    // "h = (q_inv * (m1 - m2)) mod p" and finally the result is:
    // "result = m2 + h * q".  Note that m1, m2 and abs(m1-m2) are all < p.
    m1       = sw_mod_exp(handle, d_p, p, msg);
    m2       = sw_mod_exp(handle, d_q, q, msg);
    m1_ge_m2 = pka_compare(m1, m2) != PKA_LESS_THAN;

    if (m1_ge_m2)
        mdiff = sw_subtract(handle, m1, m2);
    else
    {
        abs_diff = sw_subtract(handle, m2, m1);
        mdiff    = sw_subtract(handle, p, abs_diff);
        free_operand(&abs_diff);
    }

    h         = sw_mod_multiply(handle, mdiff, qinv, p);
    h_times_q = sw_multiply(handle, h, q);
    result    = sw_add(handle, m2, h_times_q);

    free_operand(&m1);
    free_operand(&m2);
    free_operand(&mdiff);
    free_operand(&h);
    free_operand(&h_times_q);
    return result;
}



pka_operand_t *sw_mod_inverse(gxcr_pka_handle_t *handle,
                              pka_operand_t     *value,
                              pka_operand_t     *modulus)
{
    quot_and_remain_t quot_and_remain;
    pka_comparison_t  comparison;
    pka_operand_t    *a, *b, *quot, *abs_x, *last_abs_x, *temp_abs_x;
    pka_operand_t    *abs_prod, *temp, *inverse;
    int32_t           x_sign, last_x_sign, temp_x_sign;

    comparison = pka_compare(value, modulus);
    if (comparison != PKA_LESS_THAN)
        a = sw_modulo(handle, value, modulus);
    else
        a = dup_operand(handle, value);

    b = dup_operand(handle, modulus);
    if (pka_is_zero(a) || pka_is_zero(b))
    {
        printf("sw_mod_inverse called with zero operands\n");
        return NULL;
    }

    // Find x such that a*x - q*b = 1 using the extended euclidean algorithm.
    last_abs_x  = dup_operand(handle, &ONE);
    last_x_sign = 1;
    abs_x       = dup_operand(handle, &ZERO);
    x_sign      = 0;

    while (! pka_is_zero(b))
    {
        sw_divide_with_remainder(handle, a, b, &quot_and_remain);
        free_operand(&a);
        a    = b;
        quot = quot_and_remain.quotient;
        b    = quot_and_remain.remainder;

        temp_abs_x  = abs_x;
        temp_x_sign = x_sign;

        // Calculate the next x as "x = last_x - quot * x".  The complications
        // arise, since the bignum arithmetic does not support negative values
        // and we are also careful to prevent multiplication or addition by 0.

        if ((x_sign == 0) || pka_is_zero(quot))
        {
            abs_x  = dup_operand(handle, last_abs_x);
            x_sign = last_x_sign;
        }
        else
        {
            abs_prod = sw_multiply(handle, quot, abs_x);
            if (last_x_sign == 0)
            {
                abs_x  = dup_operand(handle, abs_prod);
                x_sign = -1 * x_sign;
            }
            else if (x_sign != last_x_sign)
            {
                abs_x  = sw_add(handle, last_abs_x, abs_prod);
                x_sign = last_x_sign;
            }
            else
            {
                comparison = pka_compare(abs_prod, last_abs_x);
                if (comparison == PKA_EQUAL)
                {
                    abs_x  = dup_operand(handle, &ZERO);
                    x_sign = 0;
                }
                else if (comparison == PKA_LESS_THAN)
                {
                    abs_x  = sw_subtract(handle, last_abs_x, abs_prod);
                    // x_sign doesn't change in this case/
                }
                else
                {
                    abs_x  = sw_subtract(handle, abs_prod, last_abs_x);
                    x_sign = -1 * x_sign;
                }
            }

            free_operand(&abs_prod);
        }

        free_operand(&quot);
        free_operand(&last_abs_x);
        last_abs_x  = temp_abs_x;
        last_x_sign = temp_x_sign;
    }

    if (last_x_sign == -1)
    {
        temp    = sw_subtract(handle, modulus, last_abs_x);
        inverse = dup_operand(handle, temp);
        free_operand(&temp);
    }
    else
        inverse = dup_operand(handle, last_abs_x);

    // Check that inverse is in fact correct:
    abs_prod = sw_mod_multiply(handle, value, inverse, modulus);
    Assert(pka_compare(abs_prod, &ONE) == PKA_EQUAL);

    free_operand(&last_abs_x);
    free_operand(&abs_prod);
    return inverse;
}



ecc_point_t *sw_ecc_double(gxcr_pka_handle_t *handle,
                           ecc_curve_t       *curve,
                           ecc_point_t       *pointA)
{
    pka_operand_t *x_squared, *temp2, *temp3, *dbl_y, *dbl_y_inv, *slope;
    pka_operand_t *plus_a, *s_squared, *dbl_x, *ac_xdiff, *result_x, *result_y;
    pka_operand_t *product;

    x_squared = sw_mod_multiply(handle, pointA->x, pointA->x, curve->p);
    temp2     = sw_mod_add(handle, x_squared, x_squared, curve->p);
    temp3     = sw_mod_add(handle, temp2,     x_squared, curve->p);
    plus_a    = sw_mod_add(handle, temp3,     curve->a,  curve->p);
    dbl_y     = sw_mod_add(handle, pointA->y, pointA->y, curve->p);
    dbl_y_inv = sw_mod_inverse(handle, dbl_y, curve->p);
    slope     = sw_mod_multiply(handle, plus_a, dbl_y_inv, curve->p);
    s_squared = sw_mod_multiply(handle, slope, slope, curve->p);
    dbl_x     = sw_mod_add(handle, pointA->x, pointA->x, curve->p);
    result_x  = sw_mod_subtract(handle, s_squared, dbl_x, curve->p);

    ac_xdiff = sw_mod_subtract(handle, pointA->x, result_x,  curve->p);
    product  = sw_mod_multiply(handle, slope,     ac_xdiff,  curve->p);
    result_y = sw_mod_subtract(handle, product,   pointA->y, curve->p);

    free_operand(&x_squared);
    free_operand(&temp2);
    free_operand(&temp3);
    free_operand(&dbl_y);
    free_operand(&dbl_y_inv);
    free_operand(&slope);
    free_operand(&plus_a);
    free_operand(&s_squared);
    free_operand(&dbl_x);
    free_operand(&ac_xdiff);
    free_operand(&product);

    return create_ecc_point(handle, result_x, result_y);
}

ecc_point_t *sw_ecc_add(gxcr_pka_handle_t *handle,
                        ecc_curve_t       *curve,
                        ecc_point_t       *pointA,
                        ecc_point_t       *pointB)
{
    pka_operand_t *ab_xdiff, *ab_ydiff, *ab_xdiff_inv, *slope, *s_squared;
    pka_operand_t *ab_xsum, *ac_xdiff, *product, *result_x, *result_y;

    ab_xdiff = sw_mod_subtract(handle, pointA->x, pointB->x, curve->p);
    ab_ydiff = sw_mod_subtract(handle, pointA->y, pointB->y, curve->p);
    if ((ab_xdiff == NULL) && (ab_ydiff == NULL))
        return sw_ecc_double(handle, curve, pointA);
    else if ((ab_xdiff == NULL) || (ab_ydiff == NULL))
        return NULL;  // *TBD* return 0?

    ab_xdiff_inv = sw_mod_inverse(handle, ab_xdiff, curve->p);
    slope        = sw_mod_multiply(handle, ab_ydiff, ab_xdiff_inv, curve->p);
    s_squared    = sw_mod_multiply(handle, slope, slope, curve->p);
    ab_xsum      = sw_mod_add(handle, pointA->x, pointB->x, curve->p);
    result_x     = sw_mod_subtract(handle, s_squared, ab_xsum, curve->p);

    ac_xdiff = sw_mod_subtract(handle, pointA->x, result_x,  curve->p);
    product  = sw_mod_multiply(handle, slope,     ac_xdiff,  curve->p);
    result_y = sw_mod_subtract(handle, product,   pointA->y, curve->p);

    free_operand(&ab_xdiff);
    free_operand(&ab_ydiff);
    free_operand(&ab_xdiff_inv);
    free_operand(&slope);
    free_operand(&s_squared);
    free_operand(&ab_xsum);
    free_operand(&ac_xdiff);
    free_operand(&product);

    return create_ecc_point(handle, result_x, result_y);
}

ecc_point_t *sw_ecc_multiply(gxcr_pka_handle_t *handle,
                             ecc_curve_t       *curve,
                             ecc_point_t       *pointA,
                             pka_operand_t     *multiplier)
{
    ecc_point_t *result_pt, *new_result_pt, *base_pt, *new_base_pt;
    boolean_t    result_is_0;
    uint32_t     mult_bit_len, mult_bit_idx;
    uint8_t      big_endian;

    big_endian = multiplier->big_endian;
    Assert(curve->p->big_endian  == big_endian);
    Assert(curve->a->big_endian  == big_endian);
    Assert(pointA->x->big_endian == big_endian);
    Assert(pointA->y->big_endian == big_endian);

    // Copy pointA into base_pt and multiplier into mult.
    base_pt      = dup_ecc_point(handle, pointA);
    mult_bit_len = pka_operand_bit_len(multiplier);

    // Initialize result to be 0.
    result_is_0 = TRUE;
    result_pt   = NULL;

    for (mult_bit_idx = 0;  mult_bit_idx < mult_bit_len;  mult_bit_idx++)
    {
        if (get_bit(multiplier, mult_bit_idx) != 0)
        {
            // Add base_pt to result.  Special case for the first addition,
            // where result is zero.
            if (result_is_0)
            {
                result_pt   = dup_ecc_point(handle, base_pt);
                result_is_0 = FALSE;
            }
            else
            {
                new_result_pt = sw_ecc_add(handle, curve, result_pt, base_pt);
                free_ecc_point(result_pt);
                result_pt = new_result_pt;
            }
        }

        // Double the base_pt.
        new_base_pt = sw_ecc_double(handle, curve, base_pt);
        free_ecc_point(base_pt);
        base_pt = new_base_pt;
    }

    return result_pt;
}



dsa_signature_t *sw_ecdsa_gen(gxcr_pka_handle_t *handle,
                              ecc_curve_t       *curve,
                              ecc_point_t       *base_pt,
                              pka_operand_t     *base_pt_order,
                              pka_operand_t     *private_key,
                              pka_operand_t     *hash,
                              pka_operand_t     *k)
{
    dsa_signature_t *signature;
    pka_operand_t   *k_inv, *product, *sum, *r, *s;
    ecc_point_t     *kG;

    // kG = k * base_pt;             // This is an "ECC" point multiplication.
    // r  = kG.x mod base_pt_order;
    // s  = (k_inv * (hash + private_key * r) mod base_pt_order;
    k_inv   = sw_mod_inverse (handle, k,                    base_pt_order);
    kG      = sw_ecc_multiply(handle, curve, base_pt, k);
    r       = sw_modulo      (handle, kG->x,                base_pt_order);
    product = sw_mod_multiply(handle, private_key, r,       base_pt_order);
    sum     = sw_mod_add     (handle, hash,        product, base_pt_order);
    s       = sw_mod_multiply(handle, k_inv,       sum,     base_pt_order);

    free_operand(&k_inv);
    free_ecc_point(kG);
    free_operand(&product);
    free_operand(&sum);

    signature    = calloc(1, sizeof(dsa_signature_t));
    signature->r = r;
    signature->s = s;
    return signature;
}

status_t sw_ecdsa_verify(gxcr_pka_handle_t *handle,
                         ecc_curve_t       *curve,
                         ecc_point_t       *base_pt,
                         pka_operand_t     *base_pt_order,
                         ecc_point_t       *public_key,
                         pka_operand_t     *hash,
                         dsa_signature_t   *signature)
{
    pka_comparison_t comparison;
    pka_operand_t   *s_inv, *u1, *u2, *v;
    ecc_point_t     *u1_times_base_pt, *u2_time_public_key, *sum_pt;

    // Note that public_key = g^private_key mod p
    // Chk that 0 < r < base_pt_order and 0 < s < base_pt_order
    // s_inv  = s^(-1)         mod base_pt_order;
    // u1     = (hash * s_inv) mod base_pt_order;
    // u2     = (r    * s_inv) mod base_pt_order;
    // sum_pt = (u1 * base_pt) + (u2 * public_key);   // ECC adds and mults.
    // Chk that r == sum_pt.x mod base_pt_order;

    s_inv = sw_mod_inverse (handle, signature->s,        base_pt_order);
    u1    = sw_mod_multiply(handle, hash,         s_inv, base_pt_order);
    u2    = sw_mod_multiply(handle, signature->r, s_inv, base_pt_order);

    u1_times_base_pt   = sw_ecc_multiply(handle, curve, base_pt, u1);
    u2_time_public_key = sw_ecc_multiply(handle, curve, public_key, u2);
    sum_pt             = sw_ecc_add(handle, curve, u1_times_base_pt,
                                    u2_time_public_key);

    v          = sw_modulo(handle, sum_pt->x, base_pt_order);
    comparison = pka_compare(signature->r, v);

    free_operand(&s_inv);
    free_operand(&u1);
    free_operand(&u2);
    free_operand(&v);
    free_ecc_point(u1_times_base_pt);
    free_ecc_point(u2_time_public_key);
    free_ecc_point(sum_pt);

    if (comparison != PKA_EQUAL)
        // print some of the values here?
        return FAILURE;
    else
        return SUCCESS;
}



dsa_signature_t *sw_dsa_gen(gxcr_pka_handle_t *handle,
                            pka_operand_t     *p,
                            pka_operand_t     *q,
                            pka_operand_t     *g,
                            pka_operand_t     *private_key,
                            pka_operand_t     *hash,
                            pka_operand_t     *k)
{
    dsa_signature_t *signature;
    pka_operand_t   *k_inv, *mod_exp, *product, *sum, *r, *s;

    // r = (g^k mod p) mod q;
    // s = (k_inv * (hash + private_key * r) mod q.
    mod_exp = sw_mod_exp     (handle, k,       p, g);
    k_inv   = sw_mod_inverse (handle, k,       q);
    r       = sw_modulo      (handle, mod_exp, q);
    product = sw_mod_multiply(handle, private_key, r, q);
    sum     = sw_mod_add     (handle, hash,   product, q);
    s       = sw_mod_multiply(handle, k_inv,  sum,     q);

    free_operand(&mod_exp);
    free_operand(&k_inv);
    free_operand(&product);
    free_operand(&sum);

    signature    = calloc(1, sizeof(dsa_signature_t));
    signature->r = r;
    signature->s = s;
    return signature;
}

status_t sw_dsa_verify(gxcr_pka_handle_t *handle,
                       pka_operand_t     *p,
                       pka_operand_t     *q,
                       pka_operand_t     *g,
                       pka_operand_t     *public_key,
                       pka_operand_t     *hash,
                       dsa_signature_t   *signature)
{
    pka_comparison_t comparison;
    pka_operand_t   *s_inv, *u1, *u2, *mod_exp1, *mod_exp2, *product, *v;

    // Note that public_key = g^private_key mod p
    // Chk that 0 < r < q and 0 < s < q
    // s_inv  = s^(-1) mod q
    // u1     = (hash * s_inv) mod q
    // u2     = (r    * s_inv) mod q
    // v      = (((g^u1) * (public_key^u2)) mod p) mod q
    // Chk that v == r

    s_inv = sw_mod_inverse (handle, signature->s,        q);
    u1    = sw_mod_multiply(handle, hash,         s_inv, q);
    u2    = sw_mod_multiply(handle, signature->r, s_inv, q);

    mod_exp1 = sw_mod_exp(handle, u1, p, g);
    mod_exp2 = sw_mod_exp(handle, u2, p, public_key);

    product    = sw_mod_multiply(handle, mod_exp1, mod_exp2, p);
    v          = sw_modulo      (handle, product,            q);
    comparison = pka_compare(signature->r, v);

    free_operand(&s_inv);
    free_operand(&u1);
    free_operand(&u2);
    free_operand(&mod_exp1);
    free_operand(&mod_exp2);
    free_operand(&product);
    free_operand(&v);

    if (comparison != PKA_EQUAL)
        // print some of the values here?
        return FAILURE;
    else
        return SUCCESS;
}



pka_operand_t *sw_mod_add(gxcr_pka_handle_t *handle,
                          pka_operand_t     *value,
                          pka_operand_t     *addend,
                          pka_operand_t     *modulus)
{
    pka_operand_t *sum, *result;

    if ((value == NULL) || (addend == NULL) || (modulus == NULL))
    {
        printf("sw_mod_add called with NULL operand\n");
        return NULL;
    }

    sum = sw_add(handle, value, addend);
    if (pka_compare(sum, modulus) == PKA_LESS_THAN)
        return sum;

    result = sw_subtract(handle, sum, modulus);
    free_operand(&sum);
    return result;
}

pka_operand_t *sw_mod_subtract(gxcr_pka_handle_t *handle,
                               pka_operand_t     *value,
                               pka_operand_t     *subtrahend,
                               pka_operand_t     *modulus)
{
    pka_comparison_t comparison;
    pka_operand_t   *reversed_diff, *result;

    if ((value == NULL) || (subtrahend == NULL) || (modulus == NULL))
    {
        printf("sw_mod_subtract called with NULL operand\n");
        return NULL;
    }

    comparison = pka_compare(value, subtrahend);
    if (comparison == PKA_EQUAL)
        return NULL;
    else if (comparison == PKA_GREATER_THAN)
        return sw_subtract(handle, value, subtrahend);

    // result is -diff mod modulus which is the same as modulus - diff;
    reversed_diff = sw_subtract(handle, subtrahend, value);
    result        = sw_subtract(handle, modulus, reversed_diff);

    free_operand(&reversed_diff);
    return result;
}

pka_operand_t *sw_mod_multiply(gxcr_pka_handle_t *handle,
                               pka_operand_t     *value,
                               pka_operand_t     *multiplier,
                               pka_operand_t     *modulus)
{
    pka_operand_t *product, *result;

    if ((value == NULL) | (multiplier == NULL) || (modulus == NULL))
    {
        printf("sw_mod_multiply called with NULL operand\n");
        return NULL;
    }

    product = sw_multiply(handle, value, multiplier);
    result  = sw_modulo(handle, product, modulus);
    free_operand(&product);
    return result;
}



static pka_operand_t *get_basic_answer(gxcr_pka_handle_t *handle,
                                       pka_test_name_t   test_name,
                                       test_basic_t      *basic)
{
    switch (test_name)
    {
    case TEST_ADD:
        return sw_add(handle, basic->first, basic->second);

    case TEST_SUBTRACT:
        return sw_subtract(handle, basic->first, basic->second);

    case TEST_MULTIPLY:
        return sw_multiply(handle, basic->first, basic->second);

    case TEST_DIVIDE:
        return sw_divide(handle, basic->first, basic->second);

    case TEST_MODULO:
    case TEST_DIV_MOD:
        return sw_modulo(handle, basic->first, basic->second);

    case TEST_MOD_INVERT:
        return sw_mod_inverse(handle, basic->first, basic->second);

    case TEST_SHIFT_LEFT:
        return sw_left_shift(handle, basic->first, basic->shift_cnt);

    case TEST_SHIFT_RIGHT:
        return sw_right_shift(handle, basic->first, basic->shift_cnt);

    default:
        Assert(FALSE);
        return NULL;
    }
}



static mod_exp_key_system_t *create_mod_exp_keys(gxcr_pka_handle_t *handle,
                                                 uint32_t           bit_len,
                                                 uint32_t           verbosity)
{
    mod_exp_key_system_t *mod_exp_keys;

    LOG(2, "  find one large random prime to be the modulus.\n");
    mod_exp_keys          = calloc(1, sizeof(mod_exp_key_system_t));
    mod_exp_keys->modulus = rand_prime(handle, bit_len);
    if (mod_exp_keys->modulus == NULL)
        return NULL;

    return mod_exp_keys;
}

static test_mod_exp_t *create_mod_exp_test(gxcr_pka_handle_t    *handle,
                                           mod_exp_key_system_t *mod_exp_keys,
                                           boolean_t             make_answers,
                                           uint32_t              verbosity)
{
    test_mod_exp_t *mod_exp_test;
    pka_operand_t  *modulus;

    LOG(2, "  Find two large random integers to be the base and exponent.\n");
    modulus                = mod_exp_keys->modulus;
    mod_exp_test           = calloc(1, sizeof(test_mod_exp_t));
    mod_exp_test->base     = rand_non_zero_integer(handle, modulus);
    mod_exp_test->exponent = rand_non_zero_integer(handle, modulus);

    if (make_answers)
    {
        LOG(2, "  calculate the mod exp answer using sofware algorithms.\n");
        LOG(2, "    (note that this can take awhile).\n");
        mod_exp_test->answer = sw_mod_exp(handle, mod_exp_test->exponent,
                                          modulus, mod_exp_test->base);
        LOG(2, "  done calculating the mod exp answer.\n");
    }

    return mod_exp_test;
}



static rsa_key_system_t *create_rsa_keys(gxcr_pka_handle_t *handle,
                                         pka_test_kind_t   *test_kind,
                                         uint32_t           verbosity)
{
    pka_comparison_t  a_b_order;
    rsa_key_system_t *keys;
    pka_operand_t    *a, *b, *p, *q, *d, *e, *q_qinv, *q_qinv_mod_p, *p_minus_1;
    pka_operand_t    *q_minus_1, *product, *prod_inv, *diff, *temp, *plus_one;
    boolean_t         is_one;
    uint32_t          bit_len;

    LOG(2, "  find two large random primes to be p and q.\n");
    bit_len   = test_kind->bit_len;
    a         = rand_prime(handle, bit_len / 2);
    b         = rand_prime(handle, bit_len / 2);
    a_b_order = pka_compare(a, b);
    if (a_b_order == PKA_LESS_THAN)
    {
        p = b;
        q = a;
    }
    else if (a_b_order == PKA_GREATER_THAN)
    {
        p = a;
        q = b;
    }
    else
        Assert(FALSE);

    // Make sure e is coprime to (p - 1) * (q - 1).  This is done by making
    // e a big prime number (i.e. a prime just a little bigger than (p - 1)
    // and (q - 1)).
    LOG(2, "  pick a random prime to be the public key.\n");
    if (test_kind->test_name == TEST_RSA_VERIFY)
    {
        if (test_kind->second_bit_len == 0)
        {
            printf("TEST_RSA_VERIFY requires secondary bit length to be set\n");
            return NULL;
        }

        e = power_of_two(handle, test_kind->second_bit_len, TRUE);
        while (! is_prime(handle, e, 25, FALSE))
        {
            // Increment e by 2 and try again
            temp = e;
            e    = sync_add(handle, temp, &TWO);
            free_operand(&temp);
        }
    }
    else
        e = rand_prime(handle, (bit_len / 2) + 3);

    LOG(2, "  calculate (p - 1) * (q - 1).\n");
    p_minus_1 = sync_subtract(handle, p, &ONE);
    q_minus_1 = sync_subtract(handle, q, &ONE);
    product   = sync_multiply(handle, p_minus_1, q_minus_1);

    // Note that we can't use sync_mod_inverse directly to get e's inverse,
    // since the modulus (product) is even.  So instead we use the following
    // formula to handle cases where the modulus is even but the value to
    // invert is odd:
    //   d = (1 + (product * (e - mod_inverse(product, e)))) / e;
    LOG(2, "  calculate the private key using modular inverse.\n");
    prod_inv  = sync_mod_inverse(handle, product, e);
    diff      = sync_subtract(handle, e, prod_inv);
    temp      = sync_multiply(handle, product, diff);
    plus_one  = sync_add(handle, temp, &ONE);
    d         = sync_divide(handle, plus_one, e);

    // Derived values
    LOG(2, "  calculate q inverse and modulus (p * q).\n");
    keys              = calloc(1, sizeof(rsa_key_system_t));
    keys->p           = p;
    keys->q           = q;
    keys->d_p         = sync_modulo(handle, d, p_minus_1);
    keys->d_q         = sync_modulo(handle, d, q_minus_1);
    keys->qinv        = sync_mod_inverse(handle, q, p);
    keys->n           = sync_multiply(handle, p, q);
    keys->private_key = d;
    keys->public_key  = e;

    // Test qinv.  qinv * q mod p should be 1.
    LOG(2, "  test that qinv * q mod p == 1.\n");
    q_qinv       = sync_multiply(handle, keys->qinv, q);
    q_qinv_mod_p = sync_modulo(handle, q_qinv, p);
    is_one       = pka_is_one(q_qinv_mod_p);

    if (! is_one)
    {
        printf("ERROR qinv * q mod p should be 1, but is instead\n");
        print_pka_operand("", q_qinv_mod_p, "\n\n");
    }

    free_operand(&q_qinv_mod_p);
    free_operand(&p_minus_1);
    free_operand(&q_minus_1);
    free_operand(&product);
    free_operand(&prod_inv);
    free_operand(&diff);
    free_operand(&temp);
    free_operand(&plus_one);
    return keys;
}

static test_rsa_t *create_rsa_test(gxcr_pka_handle_t *handle,
                                   pka_test_kind_t   *test_kind,
                                   rsa_key_system_t  *rsa_keys,
                                   boolean_t          make_answers,
                                   uint32_t           verbosity)
{
    pka_operand_t *modulus, *exponent;
    test_rsa_t    *rsa_test;

    // Use the special (simpler) public key for RSA_VERIFY.
    modulus = rsa_keys->n;
    if (test_kind->test_name == TEST_RSA_VERIFY)
        exponent = rsa_keys->public_key;
    else
        exponent = rsa_keys->private_key;

    rsa_test = calloc(1, sizeof(test_rsa_t));

    LOG(2, "  create a random test msg to be encrypted by RSA.\n");
    rsa_test->msg = rand_non_zero_integer(handle, modulus);

    if (make_answers)
    {
        LOG(2, "  calculate the RSA answer using sofware algorithms.\n");
        LOG(2, "    (note that this can take awhile).\n");
        rsa_test->answer = sw_mod_exp(handle, exponent, modulus, rsa_test->msg);
        LOG(2, "  done calculating the RSA answer.\n");
    }

    return rsa_test;
}



static ecc_key_system_t *create_ecc_keys(gxcr_pka_handle_t *handle,
                                         uint32_t           bit_len,
                                         uint32_t           verbosity)
{
    ecc_key_system_t *ecc_keys;
    pka_operand_t    *p, *a, *b, *x, *y, *y_squared, *x_squared, *x_cubed;
    pka_operand_t    *x_times_a, *temp;

    LOG(2, "  find a large random prime to be the ECC curve modulus.\n");
    LOG(2, "  find three random integers to be the ECC curve a, x and y.\n");
    p = rand_prime(handle, bit_len);
    a = rand_non_zero_integer(handle, p);
    x = rand_non_zero_integer(handle, p);
    y = rand_non_zero_integer(handle, p);

    // Now determine curve->b as = y^2 - (x^3 + a*x) mod p.
    LOG(2, "  calculate the ECC curve param b based on chosen p,a,x and y.\n");
    y_squared = sync_mod_multiply(handle, y, y, p);
    x_squared = sync_mod_multiply(handle, x, x, p);
    x_cubed   = sync_mod_multiply(handle, x_squared, x, p);
    x_times_a = sync_mod_multiply(handle, x, a, p);
    temp      = sync_mod_add(handle, x_cubed, x_times_a, p);
    b         = sync_mod_subtract(handle, y_squared, temp, p);

    ecc_keys             = calloc(1, sizeof(ecc_key_system_t));
    ecc_keys->curve      = create_ecc_curve(handle, p, a, b);
    ecc_keys->base_pt    = create_ecc_point(handle, x, y);

    Assert(is_point_on_curve(handle, ecc_keys->curve, ecc_keys->base_pt));
    free_operand(&y_squared);
    free_operand(&x_squared);
    free_operand(&x_cubed);
    free_operand(&x_times_a);
    free_operand(&temp);
    return ecc_keys;
}

static test_ecc_t *create_ecc_test(gxcr_pka_handle_t *handle,
                                   pka_test_name_t    test_name,
                                   ecc_key_system_t  *ecc_key,
                                   uint32_t           bit_len,
                                   boolean_t          make_answers,
                                   uint32_t           verbosity)
{
    pka_operand_t *random_mult1, *random_mult2;
    ecc_curve_t   *curve;
    ecc_point_t   *base_pt;
    test_ecc_t    *ecc_test;
    uint32_t       rand_len;

    curve    = ecc_key->curve;
    base_pt  = ecc_key->base_pt;
    ecc_test = calloc(1, sizeof(test_ecc_t));
    rand_len = bit_len - 4;

    LOG(2, "  find some large random integers to use as ECC test operands.\n");
    random_mult1     = rand_operand(handle, rand_len, FALSE);
    random_mult2     = rand_operand(handle, rand_len, FALSE);
    ecc_test->pointA = sw_ecc_multiply(handle, curve, base_pt, random_mult1);
    ecc_test->pointB = sw_ecc_multiply(handle, curve, base_pt, random_mult2);
    ecc_test->multiplier = random_mult2;

    if ((! is_point_on_curve(handle, curve, ecc_test->pointA)) ||
        (! is_point_on_curve(handle, curve, ecc_test->pointB)))
    {
        printf("create_ecc_add_params point A and/or B not on curve\n");
        return NULL;
    }

    if (make_answers == FALSE)
        return ecc_test;

    LOG(2, "  calculate the ECC answer using sofware algorithms.\n");
    LOG(2, "    (note that this can take awhile).\n");
    if (test_name == TEST_ECC_ADD)
        ecc_test->answer = sw_ecc_add(handle, curve, ecc_test->pointA,
                                      ecc_test->pointB);
    else if (test_name == TEST_ECC_DOUBLE)
        ecc_test->answer = sw_ecc_add(handle, curve, ecc_test->pointA,
                                      ecc_test->pointA);
    else if (test_name == TEST_ECC_MULTIPLY)
        ecc_test->answer = sw_ecc_multiply(handle, curve, ecc_test->pointA,
                                           ecc_test->multiplier);
    else
        Assert(FALSE);

    LOG(2, "  done calculating the ECC answer.\n");
    if (! is_point_on_curve(handle, curve, ecc_test->answer))
    {
        printf("create_ecc_add_params result point not on curve\n");
        return NULL;
    }

    return ecc_test;
}



static ecc_curve_t *select_ecc_curve(gxcr_pka_handle_t *handle,
                                     uint32_t           bit_len)
{
    if (bit_len == 256)
        return P256_ecdsa.curve;
    else if (bit_len == 384)
        return P384_ecdsa.curve;
    else if (bit_len == 521)
        return P521_ecdsa.curve;
    else
        return NULL;
}

static ecc_point_t *select_base_pt(gxcr_pka_handle_t *handle,
                                   ecc_curve_t       *curve,
                                   uint32_t           bit_len,
                                   pka_operand_t    **base_pt_order)
{
    if (bit_len == 256)
    {
        *base_pt_order = P256_ecdsa.base_pt_order;
        return P256_ecdsa.base_pt;
    }
    else if (bit_len == 384)
    {
        *base_pt_order = P384_ecdsa.base_pt_order;
        return P384_ecdsa.base_pt;
    }
    else if (bit_len == 521)
    {
        *base_pt_order = P521_ecdsa.base_pt_order;
        return P521_ecdsa.base_pt;
    }
    else
        return NULL;
}

static ecdsa_key_system_t *create_ecdsa_key_system(gxcr_pka_handle_t *handle,
                                                   uint32_t           p_bit_len,
                                                   uint32_t           q_bit_len,
                                                   uint32_t           verbosity)
{
    ecdsa_key_system_t *ecdsa_keys;
    pka_operand_t      *base_pt_order, *d;
    ecc_curve_t        *curve;
    ecc_point_t        *base_pt, *public_pt;

    // Select curve.
    LOG(2, "  select a standard ECC curve for this ECDSA key system\n");
    curve = select_ecc_curve(handle, p_bit_len);
    if (curve == NULL)
    {
        printf("create_ecdsa_key_system failed to select an ecc curve\n");
        return NULL;
    }

    // Pick base_pt with appropriate base_pt_order.
    LOG(2, "  select a base_pt with the appropriate base_pt_order\n");
    base_pt = select_base_pt(handle, curve, p_bit_len, &base_pt_order);
    if (base_pt == NULL)
    {
        printf("create_ecdsa_key_system failed to find a suitable base_pt\n");
        return NULL;
    }

    // Finally create a private/public key pair.
    LOG(2, "  create a ECDSA private/public key pair.\n");
    d         = rand_non_zero_integer(handle, base_pt_order);
    public_pt = sync_ecc_multiply(handle, curve, base_pt, d);

    ecdsa_keys                = calloc(1, sizeof(ecdsa_key_system_t));
    ecdsa_keys->curve         = curve;
    ecdsa_keys->base_pt       = base_pt;
    ecdsa_keys->base_pt_order = base_pt_order;
    ecdsa_keys->private_key   = d;
    ecdsa_keys->public_key    = public_pt;
    return ecdsa_keys;
}

static test_ecdsa_t *create_ecdsa_test(gxcr_pka_handle_t  *handle,
                                       ecdsa_key_system_t *ecdsa_keys,
                                       uint32_t            n_bit_len,
                                       boolean_t           make_answers,
                                       uint32_t            verbosity)
{
    dsa_signature_t *signature;
    pka_operand_t   *c, *n_minus_1, *temp, *k, *hash;
    test_ecdsa_t    *ecdsa_test;
    uint32_t         hash_len;

    LOG(2, "  create a random ECDSA operands like the hash and secret k.\n");
    c         = rand_operand(handle, n_bit_len + 64, FALSE);
    n_minus_1 = sync_subtract(handle, ecdsa_keys->base_pt_order, &ONE);
    temp      = sync_modulo(handle, c, n_minus_1);
    k         = sync_add(handle, temp, &ONE);

    hash_len  = n_bit_len;  // *TBD*
    hash      = rand_operand(handle, hash_len, FALSE);
    signature = NULL;

    if (make_answers)
    {
        LOG(2, "  calculate the ECDSA signature using sofware algorithms.\n");
        signature = sw_ecdsa_gen(handle, ecdsa_keys->curve, ecdsa_keys->base_pt,
                                 ecdsa_keys->base_pt_order,
                                 ecdsa_keys->private_key, hash, k);
        LOG(2, "  done calculating the ECDSA signature.\n");
    }

    free_operand(&c);
    free_operand(&n_minus_1);
    free_operand(&temp);

    ecdsa_test            = calloc(1, sizeof(test_ecdsa_t));
    ecdsa_test->k         = k;
    ecdsa_test->hash      = hash;
    ecdsa_test->signature = signature;
    return ecdsa_test;
}



static status_t create_dsa_p_and_q(gxcr_pka_handle_t *handle,
                                   pka_operand_t    **p_ptr,
                                   pka_operand_t    **q_ptr,
                                   uint32_t           p_bit_len,
                                   uint32_t           q_bit_len)
{
    pka_operand_t *p, *q, *X, *c, *c_minus_1, *q_times_2;
    uint32_t       counter;

    // Generate prime numbers p and q.
    p = NULL;
    while (TRUE)
    {
        q = rand_prime(handle, q_bit_len);
        if (q == NULL)
            return FAILURE;

        q_times_2 = sync_add(handle, q, q);
        for (counter = 0;  counter < (4 * p_bit_len) - 1;  counter++)
        {
            X         = rand_operand(handle, p_bit_len, TRUE);
            c         = sync_modulo(handle, X, q_times_2);
            c_minus_1 = sync_subtract(handle, c, &ONE);
            p         = sync_subtract(handle, X, c_minus_1);

            free_operand(&X);
            free_operand(&c);
            free_operand(&c_minus_1);
            if ((pka_operand_bit_len(p) == p_bit_len) &&
                is_prime(handle, p, 25, FALSE))
            {
                // *TBD* Check that sync_modulus(handle, p, q_times_2) == 1.
                *p_ptr = p;
                *q_ptr = q;
                // free_operand(&q_times_2);
                return SUCCESS;
            }
        }

        free_operand(&p);
        free_operand(&q);
        free_operand(&q_times_2);
    }

    return FAILURE;
}

static pka_operand_t *create_dsa_g(gxcr_pka_handle_t *handle,
                                   pka_operand_t     *p,
                                   pka_operand_t     *q,
                                   uint32_t           p_bit_len)
{
    pka_operand_t *p_minus_1, *exponent, *temp, *h, *g, *next_h, *mod_exp;
    uint32_t       counter;

    // Generate generator g.
    p_minus_1 = sync_subtract(handle, p, &ONE);
    exponent  = sync_divide(handle, p_minus_1, q);
    temp      = rand_operand(handle, p_bit_len - 2, TRUE);
    h         = sync_add(handle, temp, &ONE);
    // The above construction of h should ensure that 1 < h < p_minus_1!

    for (counter = 1;  counter < 100;  counter++)
    {
        g = sync_mod_exp(handle, exponent, p, h);
        if (pka_compare(g, &ONE) == PKA_GREATER_THAN)
        {
            // Note that 2<= g <= p - 1 && g^q mod p == 1.
            Assert(pka_compare(g, p) == PKA_LESS_THAN);
            mod_exp = sync_mod_exp(handle, q, p, g);
            Assert(pka_compare(mod_exp, &ONE) == PKA_EQUAL);
            free_operand(&h);
            free_operand(&mod_exp);
            return g;
        }

        next_h = sync_add(handle, h, &ONE);
        free_operand(&h);
        h = next_h;
    }

    return NULL;
}

static dsa_key_system_t *create_dsa_key_system(gxcr_pka_handle_t *handle,
                                               uint32_t           p_bit_len,
                                               uint32_t           q_bit_len,
                                               uint32_t           verbosity)
{
    dsa_key_system_t *dsa_keys;
    pka_operand_t    *c, *q_minus_1, *temp, *x, *y;
    status_t          status;

    // Generate prime numbers p and q.
    LOG(2, "  find two large random primes to be p and q for DSA.\n");
    dsa_keys = calloc(1, sizeof(dsa_key_system_t));
    status   = create_dsa_p_and_q(handle, &dsa_keys->p, &dsa_keys->q,
                                  p_bit_len, q_bit_len);
    if (status != SUCCESS)
    {
        printf("create_dsa_key_system failed to create p and q\n");
        return NULL;
    }

    // Generate generator g.
    LOG(2, "  generate the DSA generator g.\n");
    dsa_keys->g = create_dsa_g(handle, dsa_keys->p, dsa_keys->q, p_bit_len);
    if (dsa_keys->g == NULL)
    {
        printf("create_dsa_key_system failed to generate a generator\n");
        return NULL;
    }

    // Finally create a private/public key pair.
    LOG(2, "  create a DSA private/public key pair.\n");
    c         = rand_operand(handle, q_bit_len + 64, FALSE);
    q_minus_1 = sync_subtract(handle, dsa_keys->q, &ONE);
    temp      = sync_modulo(handle, c, q_minus_1);
    x         = sync_add(handle, temp, &ONE);
    y         = sync_mod_exp(handle, x, dsa_keys->p, dsa_keys->g);

    free_operand(&c);
    free_operand(&q_minus_1);
    free_operand(&temp);

    // Note that private_key is in the range 1 .. q-1 and the public_key is
    // in the range 1..prime-1.
    dsa_keys->private_key = x;
    dsa_keys->public_key  = y;
    return dsa_keys;
}

static test_dsa_t *create_dsa_test(gxcr_pka_handle_t *handle,
                                   dsa_key_system_t  *dsa_keys,
                                   uint32_t           q_bit_len,
                                   boolean_t          make_answers,
                                   uint32_t           verbosity)
{
    dsa_signature_t *signature;
    pka_operand_t   *c, *q_minus_1, *temp, *k, *hash;
    test_dsa_t      *dsa_test;
    uint32_t         hash_len;

    LOG(2, "  create a random DSA operands like the hash and secret k.\n");
    c         = rand_operand(handle, q_bit_len + 64, FALSE);
    q_minus_1 = sync_subtract(handle, dsa_keys->q, &ONE);
    temp      = sync_modulo(handle, c, q_minus_1);
    k         = sync_add(handle, temp, &ONE);

    hash_len  = q_bit_len;  // *TBD*
    hash      = rand_operand(handle, hash_len, FALSE);
    signature = NULL;

    if (make_answers)
    {
        LOG(2, "  calculate the DSA signature using sofware algorithms.\n");
        signature = sw_dsa_gen(handle, dsa_keys->p, dsa_keys->q, dsa_keys->g,
                               dsa_keys->private_key, hash, k);
        LOG(2, "  done calculating the DSA signature.\n");
    }

    free_operand(&c);
    free_operand(&q_minus_1);
    free_operand(&temp);

    dsa_test            = calloc(1, sizeof(test_dsa_t));
    dsa_test->k         = k;
    dsa_test->hash      = hash;
    dsa_test->signature = signature;
    return dsa_test;
}



static status_t create_basic_test_descs(gxcr_pka_handle_t *handle,
                                        pka_test_kind_t   *test_kind,
                                        test_desc_t       *test_descs[],
                                        boolean_t          make_answers,
                                        uint32_t           verbosity)
{
    pka_test_name_t test_name;
    test_basic_t   *basic;
    test_desc_t    *test_desc;
    uint32_t        num_key_systems, tests_per_key_system, test_desc_idx;
    uint32_t        key_cnt, test_cnt, bit_len, second_bit_len;

    test_name            = test_kind->test_name;
    num_key_systems      = test_kind->num_key_systems;
    tests_per_key_system = test_kind->tests_per_key_system;
    bit_len              = test_kind->bit_len;
    second_bit_len       = test_kind->second_bit_len;
    LOG(1, "Create %u random basic test systems:\n", num_key_systems);

    // First loop over the num_key_systems and then loop to create the
    // required number of basic tests per key_system.
    test_desc_idx = 0;
    for (key_cnt = 1;  key_cnt <= num_key_systems;  key_cnt++)
    {
        LOG(1, "Create %u basic tests (random operands and possible answer).\n",
            tests_per_key_system);

        for (test_cnt = 1;  test_cnt <= tests_per_key_system;  test_cnt++)
        {
            test_desc = calloc(1, sizeof(test_desc_t));
            basic     = calloc(1, sizeof(test_basic_t));
            if (test_name == TEST_MOD_INVERT)
            {
                basic->second = rand_prime(handle, bit_len);
                basic->first  = rand_non_zero_integer(handle, basic->second);
            }
            else if ((test_name == TEST_DIVIDE)  ||
                     (test_name == TEST_DIV_MOD) ||
                     (test_name == TEST_MODULO))
            {
                basic->first  = rand_operand(handle, bit_len,        FALSE);
                basic->second = rand_operand(handle, second_bit_len, TRUE);
            }
            else if (test_name == TEST_SUBTRACT)
            {
                basic->first  = rand_operand(handle, bit_len, FALSE);
                basic->second = rand_non_zero_integer(handle, basic->first);
            }
            else
            {
                basic->first  = rand_operand(handle, bit_len, FALSE);
                basic->second = rand_operand(handle, bit_len, FALSE);
            }

            basic->shift_cnt = rand() & 0x1F;
            if (make_answers)
            {
                LOG(2, "  calculate the answer using sofware algorithms.\n");
                basic->answer = get_basic_answer(handle, test_name, basic);
                if (test_name == TEST_DIV_MOD)
                    basic->answer2 = sw_divide(handle, basic->first,
                                               basic->second);
            }

            test_desc->test_kind        = test_kind;
            test_desc->key_system       = NULL;
            test_desc->test_operands    = basic;
            test_descs[test_desc_idx++] = test_desc;
        }
    }

    LOG(1, "Done creating the %u random basic test systems.\n",
        num_key_systems);
    return SUCCESS;
}



static status_t create_mod_exp_test_descs(gxcr_pka_handle_t *handle,
                                          pka_test_kind_t   *test_kind,
                                          test_desc_t       *test_descs[],
                                          boolean_t          make_answers,
                                          uint32_t           verbosity)
{
    mod_exp_key_system_t *mod_exp_keys;
    pka_test_name_t       test_name;
    test_mod_exp_t       *test_mod_exp;
    test_desc_t          *test_desc;
    uint32_t              num_key_systems, tests_per_key_system, test_desc_idx;
    uint32_t              key_cnt, test_cnt, bit_len;

    test_name            = test_kind->test_name;
    num_key_systems      = test_kind->num_key_systems;
    tests_per_key_system = test_kind->tests_per_key_system;
    bit_len              = test_kind->bit_len;
    LOG(1, "Create %u modular exponentiation key systems:\n", num_key_systems);
    Assert(test_name == TEST_MOD_EXP);

    // First loop over the creation of the mod_exp_keys objects.  Then for each
    // mod_exp_key object create the required number of different mod-_xp tests.
    test_desc_idx = 0;
    for (key_cnt = 1;  key_cnt <= num_key_systems;  key_cnt++)
    {
        mod_exp_keys = create_mod_exp_keys(handle, bit_len, verbosity);
        if (mod_exp_keys == NULL)
            return FAILURE;

        LOG(1, "Create %u mod exp tests (random msg and possible answer).\n",
            tests_per_key_system);

        for (test_cnt = 1;  test_cnt <= tests_per_key_system;  test_cnt++)
        {
            test_desc    = calloc(1, sizeof(test_desc_t));
            test_mod_exp = create_mod_exp_test(handle, mod_exp_keys,
                                               make_answers, verbosity);

            test_desc->test_kind        = test_kind;
            test_desc->key_system       = mod_exp_keys;
            test_desc->test_operands    = test_mod_exp;
            test_descs[test_desc_idx++] = test_desc;
        }

        LOG(1, "Done creating the %u mod exp tests.\n", tests_per_key_system);
    }

    LOG(1, "Done creating the %u modular exponentiation key systems.\n",
        num_key_systems);
    return SUCCESS;
}



static status_t create_rsa_test_descs(gxcr_pka_handle_t *handle,
                                      pka_test_kind_t   *test_kind,
                                      test_desc_t       *test_descs[],
                                      boolean_t          make_answers,
                                      uint32_t           verbosity)
{
    rsa_key_system_t *rsa_keys;
    pka_test_name_t   test_name;
    test_desc_t      *test_desc;
    test_rsa_t       *test_rsa;
    uint32_t          num_key_systems, tests_per_key_system, test_desc_idx;
    uint32_t          key_cnt, test_cnt;

    test_name            = test_kind->test_name;
    num_key_systems      = test_kind->num_key_systems;
    tests_per_key_system = test_kind->tests_per_key_system;
    LOG(1, "Create %u RSA key systems:\n", num_key_systems);

    Assert((test_name == TEST_RSA_MOD_EXP)          ||
           (test_name == TEST_RSA_VERIFY)           ||
           (test_name == TEST_RSA_MOD_EXP_WITH_CRT));

    // First loop over the creation of the rsa_keys objects.  Then for each
    // rsa_key object create the required number of different rsa tests.
    test_desc_idx = 0;
    for (key_cnt = 1; key_cnt <= num_key_systems; key_cnt++)
    {
        rsa_keys = create_rsa_keys(handle, test_kind, verbosity);

        LOG(1, "Create %u RSA tests (random msg and possible answer).\n",
            tests_per_key_system);

        for (test_cnt = 1; test_cnt <= tests_per_key_system; test_cnt++)
        {
            test_desc = calloc(1, sizeof(test_desc_t));
            test_rsa  = create_rsa_test(handle, test_kind, rsa_keys,
                                        make_answers, verbosity);

            test_desc->test_kind        = test_kind;
            test_desc->key_system       = rsa_keys;
            test_desc->test_operands    = test_rsa;
            test_descs[test_desc_idx++] = test_desc;
        }

        LOG(1, "Done creating the %u RSA tests.\n", tests_per_key_system);
    }

    LOG(1, "Done creating the %u RSA key systems.\n", num_key_systems);
    return SUCCESS;
}



static status_t create_ecc_test_descs(gxcr_pka_handle_t *handle,
                                      pka_test_kind_t   *test_kind,
                                      test_desc_t       *test_descs[],
                                      boolean_t          make_answers,
                                      uint32_t           verbosity)
{
    ecc_key_system_t *ecc_key;
    pka_test_name_t   test_name;
    test_desc_t      *test_desc;
    test_ecc_t       *ecc_test;
    uint32_t          num_key_systems, tests_per_key_system, test_desc_idx;
    uint32_t          key_cnt, test_cnt, bit_len;

    test_name            = test_kind->test_name;
    num_key_systems      = test_kind->num_key_systems;
    tests_per_key_system = test_kind->tests_per_key_system;
    bit_len              = test_kind->bit_len;
    LOG(1, "Create %u ECC key systems:\n", num_key_systems);

    Assert((test_name == TEST_ECC_ADD) || (test_name == TEST_ECC_DOUBLE) ||
           (test_name == TEST_ECC_MULTIPLY));

    // First loop over the creation of the ecc_keys objects.  Then for each
    // ecc_key object create the required number of different ecc tests.
    test_desc_idx = 0;
    for (key_cnt = 1;  key_cnt <= num_key_systems;  key_cnt++)
    {
        ecc_key = create_ecc_keys(handle, bit_len, verbosity);

        LOG(1, "Create %u ECC tests (random msg and possible answer).\n",
            tests_per_key_system);

        for (test_cnt = 1;  test_cnt <= tests_per_key_system;  test_cnt++)
        {
            ecc_test  = create_ecc_test(handle, test_name, ecc_key, bit_len,
                                        make_answers, verbosity);
            test_desc = calloc(1, sizeof(test_desc_t));

            test_desc->test_kind        = test_kind;
            test_desc->key_system       = ecc_key;
            test_desc->test_operands    = ecc_test;
            test_descs[test_desc_idx++] = test_desc;
        }

        LOG(1, "Done creating the %u ECC tests.\n", tests_per_key_system);
    }

    LOG(1, "Done creating the %u ECC key systems.\n", num_key_systems);
    return SUCCESS;
}



static status_t create_ecdsa_test_descs(gxcr_pka_handle_t *handle,
                                        pka_test_kind_t   *test_kind,
                                        test_desc_t       *test_descs[],
                                        boolean_t          make_answers,
                                        uint32_t           verbosity)
{
    ecdsa_key_system_t *ecdsa_keys;
    pka_test_name_t     test_name;
    test_desc_t        *test_desc;
    test_ecdsa_t       *ecdsa_test;
    uint32_t            num_key_systems, tests_per_key_system, test_desc_idx;
    uint32_t            key_cnt, test_cnt, p_bit_len, q_bit_len;

    test_name            = test_kind->test_name;
    p_bit_len            = test_kind->bit_len;
    q_bit_len            = test_kind->second_bit_len;
    num_key_systems      = test_kind->num_key_systems;
    tests_per_key_system = test_kind->tests_per_key_system;
    LOG(1, "Create %u ECDSA key systems:\n", num_key_systems);

    Assert((test_name == TEST_ECDSA_GEN)    ||
           (test_name == TEST_ECDSA_VERIFY) ||
           (test_name == TEST_ECDSA_GEN_VERIFY));

    // First loop over the creation of the ecdsa_keys objects.  Then for each
    // ecdsa_key object create the required number of different ecdsa tests.
    test_desc_idx = 0;
    for (key_cnt = 1;  key_cnt <= num_key_systems;  key_cnt++)
    {

        ecdsa_keys = create_ecdsa_key_system(handle, p_bit_len, q_bit_len,
                                             verbosity);

        LOG(1, "Create %u ECDSA tests (random msg and possible answer).\n",
            tests_per_key_system);

        for (test_cnt = 1;  test_cnt <= tests_per_key_system;  test_cnt++)
        {
            ecdsa_test = create_ecdsa_test(handle, ecdsa_keys, q_bit_len,
                                           make_answers, verbosity);
            test_desc  = calloc(1, sizeof(test_desc_t));

            test_desc->test_kind        = test_kind;
            test_desc->key_system       = ecdsa_keys;
            test_desc->test_operands    = ecdsa_test;
            test_descs[test_desc_idx++] = test_desc;
        }

        LOG(1, "Done creating the %u ECDSA tests.\n", tests_per_key_system);
    }

    LOG(1, "Done creating the %u ECDSA key systems.\n", num_key_systems);
    return SUCCESS;
}



static status_t create_dsa_test_descs(gxcr_pka_handle_t *handle,
                                      pka_test_kind_t   *test_kind,
                                      test_desc_t       *test_descs[],
                                      boolean_t          make_answers,
                                      uint32_t           verbosity)
{
    dsa_key_system_t *dsa_keys;
    pka_test_name_t   test_name;
    test_desc_t      *test_desc;
    test_dsa_t       *dsa_test;
    uint32_t          num_key_systems, tests_per_key_system, test_desc_idx;
    uint32_t          key_cnt, test_cnt, p_bit_len, q_bit_len;

    test_name            = test_kind->test_name;
    p_bit_len            = test_kind->bit_len;
    q_bit_len            = test_kind->second_bit_len;
    num_key_systems      = test_kind->num_key_systems;
    tests_per_key_system = test_kind->tests_per_key_system;
    LOG(1, "Create %u DSA key systems:\n", num_key_systems);

    Assert((test_name == TEST_DSA_GEN)    ||
           (test_name == TEST_DSA_VERIFY) ||
           (test_name == TEST_DSA_GEN_VERIFY));

    if (q_bit_len == 0)
    {
        printf("create_dsa_test_descs error. Need to set the "
               "secondary bit_len\n");
        return FAILURE;
    }

    // First loop over the creation of the dsa_keys objects.  Then for each
    // dsa_key object create the required number of different dsa tests.
    test_desc_idx = 0;
    for (key_cnt = 1;  key_cnt <= num_key_systems;  key_cnt++)
    {
        dsa_keys = create_dsa_key_system(handle, p_bit_len, q_bit_len,
                                         verbosity);

        LOG(1, "Create %u DSA tests (random msg and possible answer).\n",
            tests_per_key_system);

        for (test_cnt = 1;  test_cnt <= tests_per_key_system;  test_cnt++)
        {
            dsa_test  = create_dsa_test(handle, dsa_keys, q_bit_len,
                                        make_answers, verbosity);
            test_desc = calloc(1, sizeof(test_desc_t));

            test_desc->test_kind        = test_kind;
            test_desc->key_system       = dsa_keys;
            test_desc->test_operands    = dsa_test;
            test_descs[test_desc_idx++] = test_desc;
        }

        LOG(1, "Done creating the %u DSA tests.\n", tests_per_key_system);
    }

    LOG(1, "Done creating the %u DSA key systems.\n", num_key_systems);
    return SUCCESS;
}



status_t chk_bit_lens(pka_test_kind_t *test_kind)
{
    switch (test_kind->test_name)
    {
    case TEST_ADD:
    case TEST_SUBTRACT:
    case TEST_MULTIPLY:
    case TEST_SHIFT_LEFT:
    case TEST_SHIFT_RIGHT:
    case TEST_MOD_INVERT:
        // test_kind->bit_len must be in range 33 .. 4096
        if ((33 <= test_kind->bit_len) && (test_kind->bit_len <= 4096))
            return SUCCESS;

        printf("Basic tests require bit_len to be in the range 33..4096\n");
        return FAILURE;

    case TEST_DIVIDE:
    case TEST_DIV_MOD:
    case TEST_MODULO:
        // test_kind->bit_len must be in range 33 .. 4096
        // test_kind->second_bit_len must be in the range 33..4095 AND
        // test_kind->second_bit_len must be <= test_kind->bit_len
        if ((33 <= test_kind->bit_len) && (test_kind->bit_len <= 4096) &&
            (33 <= test_kind->second_bit_len) &&
            (test_kind->second_bit_len <= test_kind->bit_len))
            return SUCCESS;

        printf("Divide/modulo tests require bit_len to be in the range "
               "33..4096 and 33 <= second_bit <= bit_len\n");
        return FAILURE;

    case TEST_MOD_EXP:
        // test_kind->bit_len must be in range 33 .. 4096
        if ((33 <= test_kind->bit_len) && (test_kind->bit_len <= 4096))
            return SUCCESS;

        printf("ModExp tests require bit_len to be in the range 33..4096\n");
        return FAILURE;

    case TEST_RSA_VERIFY:
        // test_kind->bit_len must be in range 66 .. 4096 and
        // test_kind->second_bit_len must be in the range 9..4095 AND
        // test_kind->second_bit_len must be < test_kind->bit_len
        if ((66 <= test_kind->bit_len) && (test_kind->bit_len <= 4096) &&
            (9 <= test_kind->second_bit_len) &&
            (test_kind->second_bit_len < test_kind->bit_len))
            return SUCCESS;

        printf("RSA Verify tests require bit_len to be in the range 66..4096 "
               "and 9 <= second_bit < bit_len\n");
        return FAILURE;

    case TEST_RSA_MOD_EXP:
    case TEST_RSA_MOD_EXP_WITH_CRT:
        // test_kind->bit_len must be in range 66 .. 4096
        if ((66 <= test_kind->bit_len) && (test_kind->bit_len <= 4096))
            return SUCCESS;

        printf("RsaModExp tests (with or without CRT) require bit_len "
               "to be in the range 66..4096\n");
        return FAILURE;

    case TEST_ECC_ADD:
    case TEST_ECC_DOUBLE:
    case TEST_ECC_MULTIPLY:
        // test_kind->bit_len must be in range 33 .. 4096
        if ((33 <= test_kind->bit_len) && (test_kind->bit_len <= 521))
            return SUCCESS;

        if ((522 <= test_kind->bit_len) && (test_kind->bit_len <= 4096))
        {
            printf("WARNING bit lengths > 521 for ECC tests are valid, but\n");
            printf("are not recommended since they can take a very long\n");
            printf("time and also do not reflect real ECC systems\n");
            return SUCCESS;
        }

        printf("ECC tests require bit_len to be in the range 33..4096\n");
        return FAILURE;

    case TEST_ECDSA_GEN:
    case TEST_ECDSA_VERIFY:
    case TEST_ECDSA_GEN_VERIFY:
        // test_kind->bit_len MUST equal 256, 384 or 521
        // test_kind->second_bit_len must be in the range 33..520 AND
        // test_kind->second_bit_len must be < test_kind->bit_len
        if (((test_kind->bit_len == 256) || (test_kind->bit_len == 384) ||
            (test_kind->bit_len == 521)) && (33 <= test_kind->second_bit_len) &&
            (test_kind->second_bit_len < test_kind->bit_len))
            return SUCCESS;

        printf("ECDSA tests require bit_len to be either 256, 384 or 521 "
               "and 33 <= second_bit < bit_len\n");
        return FAILURE;

    case TEST_DSA_GEN:
    case TEST_DSA_VERIFY:
    case TEST_DSA_GEN_VERIFY:
        // test_kind->bit_len must be in range 33 .. 4096 and
        // test_kind->second_bit_len must be in the range 9..4095 AND
        // test_kind->second_bit_len must be < test_kind->bit_len
        if ((33 <= test_kind->bit_len) && (test_kind->bit_len <= 4096) &&
            (9 <= test_kind->second_bit_len) &&
            (test_kind->second_bit_len < test_kind->bit_len))
            return SUCCESS;

        printf("DSA tests require bit_len to be in the range 33..4096 "
               "and 9 <= second_bit < bit_len\n");
        return FAILURE;

    default:
        Assert(FALSE);
        return FAILURE;
    }
}

status_t create_pka_test_descs(gxcr_pka_handle_t *handle,
                               pka_test_kind_t   *test_kind,
                               test_desc_t       *test_descs[],
                               boolean_t          make_answers,
                               uint32_t           verbosity)
{
    // Assert(test_desc->test_category == PKA_TEST);

    switch (test_kind->test_name)
    {
    case TEST_ADD:
    case TEST_SUBTRACT:
    case TEST_MULTIPLY:
    case TEST_DIVIDE:
    case TEST_DIV_MOD:
    case TEST_MODULO:
    case TEST_SHIFT_LEFT:
    case TEST_SHIFT_RIGHT:
    case TEST_MOD_INVERT:
        return create_basic_test_descs(handle, test_kind, test_descs,
                                       make_answers, verbosity);

    case TEST_MOD_EXP:
        return create_mod_exp_test_descs(handle, test_kind, test_descs,
                                         make_answers, verbosity);

    case TEST_RSA_MOD_EXP:
    case TEST_RSA_VERIFY:
    case TEST_RSA_MOD_EXP_WITH_CRT:
        return create_rsa_test_descs(handle, test_kind, test_descs,
                                     make_answers, verbosity);

    case TEST_ECC_ADD:
    case TEST_ECC_DOUBLE:
    case TEST_ECC_MULTIPLY:
        return create_ecc_test_descs(handle, test_kind, test_descs,
                                     make_answers, verbosity);
    case TEST_ECDSA_GEN:
    case TEST_ECDSA_VERIFY:
    case TEST_ECDSA_GEN_VERIFY:
        return create_ecdsa_test_descs(handle, test_kind, test_descs,
                                       make_answers, verbosity);

    case TEST_DSA_GEN:
    case TEST_DSA_VERIFY:
    case TEST_DSA_GEN_VERIFY:
        return create_dsa_test_descs(handle, test_kind, test_descs,
                                     make_answers, verbosity);

    default:
        Assert(FALSE);
    }
}



pka_test_name_t lookup_test_name(char *string)
{
    pka_test_name_t test_name;
    uint32_t        len;
    char            new_string[64];

    // First try straight name lookup
    for (test_name = TEST_ADD;  test_name <= TEST_DSA_GEN_VERIFY;  test_name++)
        if (strcasecmp(TEST_NAME_STRING[test_name], string) == 0)
            return test_name;

    // Now try name lookup with additional "TEST_" prefix added.
    strcpy(&new_string[0], "TEST_");
    len = strlen(new_string);
    strcpy(&new_string[len], string);

    for (test_name = TEST_ADD;  test_name <= TEST_DSA_GEN_VERIFY;  test_name++)
        if (strcasecmp(TEST_NAME_STRING[test_name], new_string) == 0)
            return test_name;

    return TEST_NOP;
}

char *test_name_to_string(pka_test_name_t test_name)
{
    if (test_name <= TEST_DSA_GEN_VERIFY)
        return TEST_NAME_STRING[test_name];

    return "TEST_NOP";
}

void free_test_descs(test_desc_t *test_descs[])
{
}



void check_ecc_dsa(gxcr_pka_handle_t  *handle,
                   ecdsa_key_system_t *ecdsa_keys,
                   test_ecdsa_t       *ecdsa_test,
                   pka_operand_t      *kinv)
{
    pka_operand_t *k_times_kinv;

    if (! sw_is_valid_curve(handle, ecdsa_keys->curve))
        printf("check_ecc_dsa bad curve\n");

    if (! is_point_on_curve(handle, ecdsa_keys->curve, ecdsa_keys->base_pt))
        printf("check_ecc_dsa base_pt NOT on curve\n");

    if (! is_point_on_curve(handle, ecdsa_keys->curve, ecdsa_keys->public_key))
        printf("check_ecc_dsa public key NOT on curve\n");

    k_times_kinv = sync_mod_multiply(handle, ecdsa_test->k, kinv,
                                     ecdsa_keys->base_pt_order);

    if (! pka_is_one(k_times_kinv))
    {
        printf("init_ecc_values (k * k_inv) mod n doesn't equal 1\n");
        print_pka_operand("k           =", ecdsa_test->k, "\n");
        print_pka_operand("kinv        =", kinv,          "\n");
        print_pka_operand("k_times_kinv=", k_times_kinv,  "\n");
    }
}

static void init_ecc_values(gxcr_pka_handle_t *handle)
{
    P256_ecdsa.curve          = calloc(1, sizeof(ecc_curve_t));
    P256_ecdsa.base_pt        = calloc(1, sizeof(ecc_point_t));
    P256_ecdsa.public_key     = calloc(1, sizeof(ecc_point_t));
    P256_ecdsa_test.signature = calloc(1, sizeof(dsa_signature_t));

    P256_ecdsa.curve->p      = hex_string_to_operand(handle, P256_p_string);
    P256_ecdsa.curve->a      = hex_string_to_operand(handle, P256_a_string);
    P256_ecdsa.curve->b      = hex_string_to_operand(handle, P256_b_string);
    P256_ecdsa.base_pt->x    = hex_string_to_operand(handle, P256_xg_string);
    P256_ecdsa.base_pt->y    = hex_string_to_operand(handle, P256_yg_string);
    P256_ecdsa.base_pt_order = hex_string_to_operand(handle, P256_n_string);
    P256_ecdsa.private_key   = hex_string_to_operand(handle, P256_d_string);
    P256_ecdsa.public_key->x = hex_string_to_operand(handle, P256_xq_string);
    P256_ecdsa.public_key->y = hex_string_to_operand(handle, P256_yq_string);

    P256_kinv                = hex_string_to_operand(handle, P256_kinv_string);
    P256_ecdsa_test.k        = hex_string_to_operand(handle, P256_k_string);
    P256_ecdsa_test.hash     = hex_string_to_operand(handle, P256_hash_string);
//  P256_ecdsa_test.signature->r =hex_string_to_operand(handle, P256_yq_string);
    P256_ecdsa_test.signature->s = hex_string_to_operand(handle, P256_s_string);

    // check_ecc_dsa(handle, &P256_ecdsa, &P256_ecdsa_test, P256_kinv);

    P384_ecdsa.curve          = calloc(1, sizeof(ecc_curve_t));
    P384_ecdsa.base_pt        = calloc(1, sizeof(ecc_point_t));
    P384_ecdsa.public_key     = calloc(1, sizeof(ecc_point_t));
    P384_ecdsa_test.signature = calloc(1, sizeof(dsa_signature_t));

    P384_ecdsa.curve->p      = hex_string_to_operand(handle, P384_p_string);
    P384_ecdsa.curve->a      = hex_string_to_operand(handle, P384_a_string);
    P384_ecdsa.curve->b      = hex_string_to_operand(handle, P384_b_string);
    P384_ecdsa.base_pt->x    = hex_string_to_operand(handle, P384_xg_string);
    P384_ecdsa.base_pt->y    = hex_string_to_operand(handle, P384_yg_string);
    P384_ecdsa.base_pt_order = hex_string_to_operand(handle, P384_n_string);
    P384_ecdsa.private_key   = hex_string_to_operand(handle, P384_d_string);
    P384_ecdsa.public_key->x = hex_string_to_operand(handle, P384_xq_string);
    P384_ecdsa.public_key->y = hex_string_to_operand(handle, P384_yq_string);

    P384_kinv                = hex_string_to_operand(handle, P384_kinv_string);
    P384_ecdsa_test.k        = hex_string_to_operand(handle, P384_k_string);
    P384_ecdsa_test.hash     = hex_string_to_operand(handle, P384_hash_string);
//  P384_ecdsa_test.signature->r =hex_string_to_operand(handle, P384_yq_string);
    P384_ecdsa_test.signature->s = hex_string_to_operand(handle, P384_s_string);

    // check_ecc_dsa(handle, &P384_ecdsa, &P384_ecdsa_test, P384_kinv);

    P521_ecdsa.curve          = calloc(1, sizeof(ecc_curve_t));
    P521_ecdsa.base_pt        = calloc(1, sizeof(ecc_point_t));
    // P521_ecdsa.public_key     = calloc(1, sizeof(ecc_point_t));
    // P521_ecdsa_test.signature = calloc(1, sizeof(dsa_signature_t));

    P521_ecdsa.curve->p      = hex_string_to_operand(handle, P521_p_string);
    P521_ecdsa.curve->a      = hex_string_to_operand(handle, P521_a_string);
    P521_ecdsa.curve->b      = hex_string_to_operand(handle, P521_b_string);
    P521_ecdsa.base_pt->x    = hex_string_to_operand(handle, P521_xg_string);
    P521_ecdsa.base_pt->y    = hex_string_to_operand(handle, P521_yg_string);
    P521_ecdsa.base_pt_order = hex_string_to_operand(handle, P521_n_string);
    return;

    if (! sw_is_valid_curve(handle, P521_ecdsa.curve))
        printf("init_ecc_values P521 bad curve\n");

    if (! is_point_on_curve(handle, P521_ecdsa.curve, P521_ecdsa.base_pt))
        printf("init_ecc_values P521 base_pt NOT on curve\n");
}



void init_test_utils(gxcr_pka_handle_t *handle)
{
    ZERO.big_endian = gxcr_pka_get_endian(handle);
    ONE.big_endian  = gxcr_pka_get_endian(handle);
    TWO.big_endian  = gxcr_pka_get_endian(handle);
    init_ecc_values(handle);
}

static void sw_byte_swap_cpy(uint8_t *dst_ptr,
                             uint8_t *src_ptr,
                             uint32_t byte_len)
{
    src_ptr += byte_len;
    while (byte_len--)
        *dst_ptr++ = *(--src_ptr);
}

boolean_t test_byte_swap_cpy(uint32_t len)
{
    uint32_t idx, test_size, dst_align, src_align, max_align, test_len;
    uint8_t  src_buffer[len], dst_buffer[len], dst_good[len];
    uint8_t *dst, *src;

    for (idx = 0;  idx < len;  idx++)
        src_buffer[idx] = idx + 3;

    for (dst_align = 0; dst_align < 7; dst_align++)
    {
        dst = dst_buffer + dst_align;
        for (src_align = 0; src_align < 7; src_align++)
        {
            src = src_buffer + src_align;
            // Make up for starting further into src or dst by shortening
            // the test length.
            max_align = (dst_align > src_align) ? dst_align : src_align;
            test_len  = len - max_align;
            for (test_size = 0;  test_size <= test_len;  test_size++)
            {
                memset(dst, 0, len);
                byte_swap_cpy(dst, src, test_size);
                sw_byte_swap_cpy(dst_good, src, test_size);
                if (memcmp(dst, dst_good, test_size) != 0)
                    return FALSE;
            }
        }
    }
    return TRUE;
}
