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


#ifndef _PKA_TEST_UTILS_H_
#define _PKA_TEST_UTILS_H_

#include <stdint.h>
#include <gxcr/pka.h>


#define ENABLE_ASSERT 1

#ifdef ENABLE_ASSERT
#define Assert(cond)                                                   \
    ({                                                                 \
        if (! (cond))                                                  \
        {                                                              \
            printf("Assertion failed @ %s:%u\n", __FILE__, __LINE__);  \
            abort();                                                   \
        }                                                              \
    })
#else
#define Assert(cond)
#endif


typedef enum { FALSE = 0, TRUE = 1 } boolean_t;
typedef enum { SUCCESS, FAILURE } status_t;

typedef struct
{
    pka_operand_t *quotient;
    pka_operand_t *remainder;
} quot_and_remain_t;




boolean_t pka_is_zero(pka_operand_t *operand);
boolean_t pka_is_one (pka_operand_t *operand);

uint32_t pka_get_msb_idx(pka_operand_t *operand, uint8_t big_endian);

status_t to_uint32_integer(pka_operand_t *operand, uint32_t *value_ptr);


void print_pka_operand(char          *pre_string,
                       pka_operand_t *big_num,
                       char          *post_string);

pka_operand_t *hex_string_to_operand(gxcr_pka_handle_t *handle, char *string);

pka_operand_t *malloc_operand(gxcr_pka_handle_t *handle, uint32_t buf_len);

pka_operand_t *dup_operand(gxcr_pka_handle_t *handle,
                           pka_operand_t     *src_operand);

ecc_point_t *dup_ecc_point(gxcr_pka_handle_t *handle, ecc_point_t *src_point);
ecc_point_t *malloc_ecc_point(gxcr_pka_handle_t *handle,
                              uint32_t           x_buf_len,
                              uint32_t           y_buf_len);

dsa_signature_t *malloc_dsa_signature(gxcr_pka_handle_t *handle,
                                      uint32_t           r_buf_len,
                                      uint32_t           s_buf_len);

pka_results_t *malloc_results(uint32_t result_cnt, uint32_t buf_len);

void free_operand(pka_operand_t **operand_ptr);
void free_ecc_point(ecc_point_t *ecc_point);
void free_results(pka_results_t *results);

pka_operand_t *results_to_operand(gxcr_pka_handle_t *handle);

status_t get_results(gxcr_pka_handle_t *handle,
                     pka_operand_t     *result1,
                     pka_operand_t     *result2);

pka_comparison_t get_compare_result(gxcr_pka_handle_t *handle);

pka_operand_t *rand_operand(gxcr_pka_handle_t *handle,
                            uint32_t           bit_len,
                            boolean_t          make_odd);

pka_operand_t *rand_prime(gxcr_pka_handle_t *handle, uint32_t bit_len);

// Return a big number between 1 .. max_plus_1 - 1.
pka_operand_t *rand_non_zero_integer(gxcr_pka_handle_t *handle,
                                     pka_operand_t     *max_plus_1);

boolean_t is_prime(gxcr_pka_handle_t *handle,
                   pka_operand_t     *prime,    // aka possible_prime
                   uint32_t           iterations,
                   boolean_t          should_be_prime);


// synchronous versions of the gxpka asynchronous commands.

pka_operand_t *sync_add(gxcr_pka_handle_t *handle,
                        pka_operand_t     *value,
                        pka_operand_t     *addend);

pka_operand_t *sync_subtract(gxcr_pka_handle_t *handle,
                             pka_operand_t     *value,
                             pka_operand_t     *subtrahend);

pka_operand_t *sync_multiply(gxcr_pka_handle_t *handle,
                             pka_operand_t     *value,
                             pka_operand_t     *multipler);

pka_operand_t *sync_divide(gxcr_pka_handle_t *handle,
                           pka_operand_t     *dividend,
                           pka_operand_t     *divisor);

pka_operand_t *sync_modulo(gxcr_pka_handle_t *handle,
                           pka_operand_t     *value,
                           pka_operand_t     *modulus);

pka_operand_t *sync_shift_left(gxcr_pka_handle_t *handle,
                               pka_operand_t     *value,
                               uint32_t           shift_cnt);

pka_operand_t *sync_shift_right(gxcr_pka_handle_t *handle,
                                pka_operand_t     *value,
                                uint32_t           shift_cnt);

pka_operand_t *sync_mod_inverse(gxcr_pka_handle_t *handle,
                                pka_operand_t     *value,
                                pka_operand_t     *modulus);

pka_operand_t *sync_mod_exp(gxcr_pka_handle_t *handle,
                            pka_operand_t     *exponent,
                            pka_operand_t     *modulus,
                            pka_operand_t     *msg);

pka_operand_t *sync_exp_with_crt(gxcr_pka_handle_t *handle,
                                 pka_operand_t     *p,
                                 pka_operand_t     *q,
                                 pka_operand_t     *msg,
                                 pka_operand_t     *d_p,
                                 pka_operand_t     *d_q,
                                 pka_operand_t     *qinv);

ecc_point_t *sync_ecc_add(gxcr_pka_handle_t *handle,
                          ecc_curve_t       *curve,
                          ecc_point_t       *pointA,
                          ecc_point_t       *pointB);

ecc_point_t *sync_ecc_multiply(gxcr_pka_handle_t *handle,
                               ecc_curve_t       *curve,
                               ecc_point_t       *pointA,
                               pka_operand_t     *multiplier);

dsa_signature_t *sync_ecdsa_gen(gxcr_pka_handle_t *handle,
                                ecc_curve_t       *curve,
                                ecc_point_t       *base_pt,
                                pka_operand_t     *base_pt_order,
                                pka_operand_t     *private_key,
                                pka_operand_t     *hash,
                                pka_operand_t     *k);

status_t sync_ecdsa_verify(gxcr_pka_handle_t *handle,
                           ecc_curve_t       *curve,
                           ecc_point_t       *base_pt,
                           pka_operand_t     *base_pt_order,
                           ecc_point_t       *public_key,
                           pka_operand_t     *hash,
                           dsa_signature_t   *signature);

dsa_signature_t *sync_dsa_gen(gxcr_pka_handle_t *handle,
                              pka_operand_t     *p,
                              pka_operand_t     *q,
                              pka_operand_t     *g,
                              pka_operand_t     *private_key,
                              pka_operand_t     *hash,
                              pka_operand_t     *k);

status_t sync_dsa_verify(gxcr_pka_handle_t *handle,
                         pka_operand_t     *p,
                         pka_operand_t     *q,
                         pka_operand_t     *g,
                         pka_operand_t     *public_key,
                         pka_operand_t     *hash,
                         dsa_signature_t   *signature);


// Combo ops.  These consist of some sync_* command, followed by a sync_modulus
// command.

pka_operand_t *sync_mod_add(gxcr_pka_handle_t *handle,
                            pka_operand_t     *value,
                            pka_operand_t     *addend,
                            pka_operand_t     *modulus);

pka_operand_t *sync_mod_subtract(gxcr_pka_handle_t *handle,
                                 pka_operand_t     *value,
                                 pka_operand_t     *subtrahend,
                                 pka_operand_t     *modulus);

pka_operand_t *sync_mod_multiply(gxcr_pka_handle_t *handle,
                                 pka_operand_t     *value,
                                 pka_operand_t     *multiplier,
                                 pka_operand_t     *modulus);

boolean_t signatures_are_equal(dsa_signature_t *signature1,
                               dsa_signature_t *signature2);

boolean_t ecc_points_are_equal(gxcr_pka_handle_t *handle,
                               ecc_point_t       *pointA,
                               ecc_point_t       *pointB);

boolean_t is_valid_curve(gxcr_pka_handle_t *handle, ecc_curve_t *curve);

boolean_t is_point_on_curve(gxcr_pka_handle_t *handle,
                            ecc_curve_t       *curve,
                            ecc_point_t       *point);


// Software versions of the HW ops (for checking results):

pka_operand_t *sw_add(gxcr_pka_handle_t *handle,
                      pka_operand_t     *value,
                      pka_operand_t     *addend);

pka_operand_t *sw_subtract(gxcr_pka_handle_t *handle,
                           pka_operand_t     *value,
                           pka_operand_t     *subtrahend);

pka_operand_t *sw_multiply(gxcr_pka_handle_t *handle,
                           pka_operand_t     *value,
                           pka_operand_t     *multiplier);

void sw_divide_with_remainder(gxcr_pka_handle_t *handle,
                              pka_operand_t     *value,
                              pka_operand_t     *divisor,
                              quot_and_remain_t *quot_and_remain);

pka_operand_t *sw_divide(gxcr_pka_handle_t *handle,
                         pka_operand_t     *value,
                         pka_operand_t     *divisor);

pka_operand_t *sw_modulo(gxcr_pka_handle_t *handle,
                         pka_operand_t     *value,
                         pka_operand_t     *modulus);

pka_operand_t *sw_left_shift(gxcr_pka_handle_t *handle,
                             pka_operand_t     *value,
                             uint32_t           shift_cnt);

pka_operand_t *sw_right_shift(gxcr_pka_handle_t *handle,
                              pka_operand_t     *value,
                              uint32_t           shift_cnt);

pka_operand_t *sw_mod_inverse(gxcr_pka_handle_t *handle,
                              pka_operand_t     *value,
                              pka_operand_t     *modulus);

pka_operand_t *sw_mod_exp(gxcr_pka_handle_t *handle,
                          pka_operand_t     *exponent,
                          pka_operand_t     *modulus,
                          pka_operand_t     *msg);

pka_operand_t *sw_exp_with_crt(gxcr_pka_handle_t *handle,
                               pka_operand_t     *p,
                               pka_operand_t     *q,
                               pka_operand_t     *msg,
                               pka_operand_t     *d_p,
                               pka_operand_t     *d_q,
                               pka_operand_t     *qinv);

ecc_point_t *sw_ecc_double(gxcr_pka_handle_t *handle,
                           ecc_curve_t       *curve,
                           ecc_point_t       *pointA);

ecc_point_t *sw_ecc_add(gxcr_pka_handle_t *handle,
                        ecc_curve_t       *curve,
                        ecc_point_t       *pointA,
                        ecc_point_t       *pointB);

ecc_point_t *sw_ecc_multiply(gxcr_pka_handle_t *handle,
                             ecc_curve_t       *curve,
                             ecc_point_t       *pointA,
                             pka_operand_t     *multiplier);

dsa_signature_t *sw_ecdsa_gen(gxcr_pka_handle_t *handle,
                              ecc_curve_t       *curve,
                              ecc_point_t       *base_pt,
                              pka_operand_t     *base_pt_order,
                              pka_operand_t     *private_key,
                              pka_operand_t     *hash,
                              pka_operand_t     *k);

status_t sw_ecdsa_verify(gxcr_pka_handle_t *handle,
                         ecc_curve_t       *curve,
                         ecc_point_t       *base_pt,
                         pka_operand_t     *base_pt_order,
                         ecc_point_t       *public_key,
                         pka_operand_t     *hash,
                         dsa_signature_t   *signature);


dsa_signature_t *sw_dsa_gen(gxcr_pka_handle_t *handle,
                            pka_operand_t     *p,
                            pka_operand_t     *q,
                            pka_operand_t     *g,
                            pka_operand_t     *private_key,
                            pka_operand_t     *hash,
                            pka_operand_t     *k);

status_t sw_dsa_verify(gxcr_pka_handle_t *handle,
                       pka_operand_t     *p,
                       pka_operand_t     *q,
                       pka_operand_t     *g,
                       pka_operand_t     *public_key,
                       pka_operand_t     *hash,
                       dsa_signature_t   *signature);


// SW combo ops.  These consist of some sw_* command, followed by a sw_modulus
// command.

pka_operand_t *sw_mod_add(gxcr_pka_handle_t *handle,
                          pka_operand_t     *value,
                          pka_operand_t     *addend,
                          pka_operand_t     *modulus);

pka_operand_t *sw_mod_subtract(gxcr_pka_handle_t *handle,
                               pka_operand_t     *value,
                               pka_operand_t     *subtrahend,
                               pka_operand_t     *modulus);

pka_operand_t *sw_mod_multiply(gxcr_pka_handle_t *handle,
                               pka_operand_t     *value,
                               pka_operand_t     *multiplier,
                               pka_operand_t     *modulus);



// Types and Fcns to help with test creation:

typedef enum
{
    PKA_TEST, BULK_CRYPTO_TEST   // Others???
} test_category_t;

typedef enum
{
    TEST_NOP,  // Represents no valid test.

    // Basic_pka_tests.  These use NO associated PKA key_system_t object.
    TEST_ADD, TEST_SUBTRACT, TEST_MULTIPLY, TEST_DIVIDE, TEST_DIV_MOD,
    TEST_MODULO, TEST_SHIFT_LEFT, TEST_SHIFT_RIGHT, TEST_MOD_INVERT,

    // Modular exponentiation tests.  These use the mod_exp_key_system_t.
    TEST_MOD_EXP,

    // RSA tests.  These use the rsa_key_system_t.
    TEST_RSA_MOD_EXP, TEST_RSA_VERIFY, TEST_RSA_MOD_EXP_WITH_CRT,

    // Ecc tests.  These use the ecc_key_system_t.
    TEST_ECC_ADD, TEST_ECC_DOUBLE, TEST_ECC_MULTIPLY,

    // Ecdsa tests.  These use the ecdsa_key_system_t.
    TEST_ECDSA_GEN, TEST_ECDSA_VERIFY, TEST_ECDSA_GEN_VERIFY,

    // Dsa tests.  These use the dsa_key_system_t.
    TEST_DSA_GEN, TEST_DSA_VERIFY, TEST_DSA_GEN_VERIFY
} pka_test_name_t;


// Definition of PKA key "systems"
typedef struct
{
    pka_operand_t *modulus;
} mod_exp_key_system_t;

typedef struct
{
    // Note that the modulus, n,  MUST equal p * q.  The bit_len of the system
    // is the bit_len of n.  p and q are prime numbers where p is always
    // larger than q, and have similar bit_len's.  (private_key * public_key)
    // mod (p-1)*(q-1) must equal 1.  Note that in an RSA "verify" key
    // system, public_key, e, has a secondary len < 33 bits long and has
    // exactly 2 bits set (the MSB and the LSB).
    pka_operand_t *p;
    pka_operand_t *q;
    pka_operand_t *private_key;  // aka d.
    pka_operand_t *public_key;   // aka e.

    // Derived values:
    pka_operand_t *n;            // I.e. modulus = product of p * q.
    pka_operand_t *d_p;          // d mod (p-1)
    pka_operand_t *d_q;          // d mod (q-1)
    pka_operand_t *qinv;         // q * qinv mod p = 1
} rsa_key_system_t;

typedef struct
{
    ecc_curve_t *curve;
    ecc_point_t *base_pt;
} ecc_key_system_t;

typedef struct
{
    ecc_curve_t   *curve;
    ecc_point_t   *base_pt;        // aka point G
    pka_operand_t *base_pt_order;  // aka n - order of point G ("n*G = 1 mod p")
    pka_operand_t *private_key;    // aka d.
    ecc_point_t   *public_key;     // aka point Q
} ecdsa_key_system_t;

typedef struct
{
    pka_operand_t *p;            // Must be prime.
    pka_operand_t *q;            // aka n, must be prime. Must divide p-1.
    pka_operand_t *g;            // g^q mod prime = 1?.  1 < g < p.
    pka_operand_t *private_key;  // aka x.  0 < private_key < q
    pka_operand_t *public_key;   // aka y.  y= g^private_key mod prime
} dsa_key_system_t;


// Definition of the "test_*_t types which point to all of the operands
// and answers of a test_desc_t.
typedef struct
{
    pka_operand_t *first;
    pka_operand_t *second;
    uint32_t       shift_cnt;
    pka_operand_t *answer;
    pka_operand_t *answer2;  // Modulus result for TEST_DIV_MOD
} test_basic_t;

typedef struct
{
    pka_operand_t *base;
    pka_operand_t *exponent;
    pka_operand_t *answer;
} test_mod_exp_t;

typedef struct
{
    pka_operand_t *msg;
    pka_operand_t *answer;
} test_rsa_t;

typedef struct
{
    pka_operand_t *base;    // aka msg, c, or plaintext.
    pka_operand_t *answer;
} test_crt_t;

typedef struct
{
    ecc_point_t   *pointA;
    ecc_point_t   *pointB;
    pka_operand_t *multiplier;
    ecc_point_t   *answer;
} test_ecc_t;

typedef struct
{
    pka_operand_t   *k;          // Secret num unique to each msg.  0 < k < q.
    pka_operand_t   *hash;       // Message hash
    dsa_signature_t *signature;
} test_ecdsa_t;

typedef struct
{
    pka_operand_t   *k;          // Secret num unique to each msg.  0 < k < q.
    pka_operand_t   *hash;       // Message hash
    dsa_signature_t *signature;
} test_dsa_t;


typedef struct
{
    uint32_t num_requested;
    uint32_t num_submitted;
    uint32_t num_good_replies;
    uint32_t num_bad_replies;
    uint32_t num_correct_answers;
    uint32_t num_wrong_answers;
    uint32_t errors;
    uint64_t total_latency;          // units = some multiple of clock cycles.
    uint64_t latency_squared;        // needed to calculate std deviation.

    uint64_t min_latency;            // units of microsecs.
    uint64_t avg_latency;
    uint64_t max_latency;
    uint32_t latency_std_dev;
} test_stats_t;

// There is one *_test_kind_t for kind of test currently being run.  Typically
// there is only one pka_test_kind_t object, but this system allows the
// capability of having mixed tests running siumultaneously (subject to
// certain constraints) using multiple test_kind_t objects.
typedef struct
{
    pka_test_name_t test_name;
    uint32_t        bit_len;          // primary bit_len
    uint32_t        second_bit_len;   // secondary bit_len
    uint32_t        num_key_systems;
    uint32_t        tests_per_key_system;
    boolean_t       create_key_verbosity;
    boolean_t       create_test_verbosity;
    test_stats_t   *test_kind_stats;
} pka_test_kind_t;


// There is a test_desc_t for each unique set of test_operands.
// There can be many test_desc_t objects for each test_kind.  The test_category
// determines the type of the test_kind.  The test_kind, in turn determines
// the numthe type of the test_operands and answer.
typedef struct
{
    test_category_t test_category;  // Determines the test_kind type.
    void           *test_kind;      // points to a *_test_kind_t object above.
    void           *key_system;     // points to a *_key_system_t object above.
    void           *test_operands;  // points to a test_*_t object above
    uint32_t        test_idx;

    // If per test_desc_t stats are not required, then this field is NULL.
    // In all cases however, test_kind_stats are defined and counted.
    test_stats_t *test_desc_stats;
} test_desc_t;



pka_test_name_t lookup_test_name(char *string);
char *test_name_to_string(pka_test_name_t test_name);

status_t chk_bit_lens(pka_test_kind_t *test_kind);

status_t create_pka_test_descs(gxcr_pka_handle_t *handle,
                               pka_test_kind_t   *test_kind,
                               test_desc_t       *test_descs[],
                               boolean_t          make_answers,
                               uint32_t           verbosity);

void free_test_descs(test_desc_t *test_descs[]);

void init_test_utils(gxcr_pka_handle_t *handle);

boolean_t test_byte_swap_cpy(uint32_t len);


extern ecdsa_key_system_t P256_ecdsa;
extern ecdsa_key_system_t P384_ecdsa;
extern ecdsa_key_system_t P521_ecdsa;

#endif


