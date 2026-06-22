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


#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arch/cycle.h>
#include <tmc/cpus.h>
#include <tmc/perf.h>
#include <tmc/spin.h>

#include <gxcr/pka.h>
#include "pka_test_utils.h"


#define MAX_NUM_TESTS   1000
#define MAX_SERVER_TILES  63

#define MIN(a, b)  (((a) <= (b)) ? (a) : (b))
#define MAX(a, b)  (((a) <= (b)) ? (b) : (a))

#define NOINLINE __attribute__((noinline))

typedef struct
{
    uint32_t thread_idx;
    uint32_t cpu_number;
} thread_arg_t;

typedef struct
{
    gxcr_pka_handle_t *handle;
    uint16_t          *randomize_tests;
    test_stats_t      *test_desc_stats;

    test_stats_t thread_stats;
    uint64_t     thread_cycles;
} thread_state_t;

typedef struct
{
    uint32_t      user_data_idx;
    uint32_t      thread_idx;

    test_desc_t  *test_desc;
    test_stats_t *test_desc_stats;
    uint64_t      start_time;  // time is measured in current clock cycles
    uint32_t      test_idx;
    boolean_t     multi_submit;
    boolean_t     first_submit;
} user_data_t;

static const uint32_t DEFAULT_BIT_LEN[] =
{
    [TEST_NOP] = 0,

    // Basic_pka_tests.  These use NO associated PKA key_system_t object.
    [TEST_ADD ... TEST_MOD_INVERT] = 1024,

    // Modular exponentiation tests.  These use the mod_exp_key_system_t.
    [TEST_MOD_EXP] = 1024,

    // RSA tests.  These use the rsa_key_system_t.
    [TEST_RSA_MOD_EXP ... TEST_RSA_MOD_EXP_WITH_CRT] = 1024,

    // Ecc tests.  These use the ecc_key_system_t.
    [TEST_ECC_ADD ... TEST_ECC_MULTIPLY] = 256,

    // Ecdsa tests.  These use the ecdsa_key_system_t.
    [TEST_ECDSA_GEN ... TEST_ECDSA_GEN_VERIFY] = 256,

    // Dsa tests.  These use the dsa_key_system_t.
    [TEST_DSA_GEN ... TEST_DSA_GEN_VERIFY] = 1024
};


static uint32_t        num_of_threads;
static uint32_t        cmds_outstanding;
static uint32_t        submits_per_test;
static boolean_t       check_results;
static boolean_t       report_thread_stats;
static boolean_t       big_endian;
static boolean_t       help;
static pka_test_kind_t test_kind;

static test_desc_t *test_descs[MAX_NUM_TESTS];
static uint32_t     num_tests;

static thread_state_t thread_states[MAX_SERVER_TILES];
static pthread_t      threads[MAX_SERVER_TILES];
static thread_arg_t   thread_args[MAX_SERVER_TILES];

static uint32_t overall_cmds_done   = 0;
static uint32_t overall_bad_results = 0;
static uint64_t overall_start_time;
static uint64_t overall_end_time;
static uint64_t cpu_freq;   // cycles per second.

static test_stats_t overall_test_stats;

static uint32_t verbosity = 0;

static tmc_spin_barrier_t thread_start_barrier;



uint64_t isqrt(uint64_t num)
{
    uint64_t result, bit;
    uint32_t bitNum;

    bitNum = 63 - __insn_clz(num);
    if (num < 4)
        return 1;

    // "bit" starts at the highest power of four <= the argument.
    bit = 1UL << (bitNum & 0x3E);
    result = 0;

    while (bit != 0)
    {
        if (num >= (result + bit))
        {
            num   -= result + bit;
            result = (result >> 1) + bit;
        }
        else
            result >>= 1;

        bit >>= 2;
    }

    return result;
}

static NOINLINE void busy_delay(void)
{
    uint32_t cnt;

    // Wait for ~300 cycles.
    for (cnt = 1;  cnt < 50;  cnt++)
        cycle_relax();
}

static status_t submit_basic_test(gxcr_pka_handle_t *handle,
                                  user_data_t       *user_data_ptr,
                                  test_desc_t       *test_desc,
                                  uint32_t           test_idx)
{
    pka_test_kind_t *test_kind;
    pka_test_name_t  test_name;
    gxcr_pka_err_t   rc;
    test_basic_t    *basic;

    test_kind = (pka_test_kind_t *) test_desc->test_kind;
    basic     = (test_basic_t    *) test_desc->test_operands;
    test_name = test_kind->test_name;

    if (3 <= verbosity)
    {
        printf("\nRunning test_idx=%u test_name=%s\n", test_idx,
               test_name_to_string(test_name));
        if ((test_name == TEST_SHIFT_LEFT) ||
            (test_name == TEST_SHIFT_RIGHT))
        {
            print_pka_operand("first  = ", basic->first,  "\n");
            printf           ("shift  = %u\n", basic->shift_cnt);
        }
        else
        {
            print_pka_operand("first  = ", basic->first,  "\n");
            print_pka_operand("second = ", basic->second, "\n");
        }
    }

    switch (test_name)
    {
    case TEST_ADD:
        rc = gxcr_pka_add(handle, user_data_ptr, basic->first, basic->second);
        break;

    case TEST_SUBTRACT:
        rc = gxcr_pka_subtract(handle, user_data_ptr, basic->first,
                               basic->second);
        break;

    case TEST_MULTIPLY:
        rc = gxcr_pka_multiply(handle, user_data_ptr, basic->first,
                               basic->second);
        break;

    case TEST_DIVIDE:
    case TEST_DIV_MOD:
        rc = gxcr_pka_divide(handle, user_data_ptr, basic->first,
                             basic->second);
        break;

    case TEST_MODULO:
        rc = gxcr_pka_modulo(handle, user_data_ptr, basic->first,
                             basic->second);
        break;

    case TEST_MOD_INVERT:
        rc = gxcr_pka_mod_invert(handle, user_data_ptr, basic->first,
                                 basic->second);
        break;

    case TEST_SHIFT_LEFT:
        rc = gxcr_pka_shift_left(handle, user_data_ptr, basic->first,
                                 basic->shift_cnt);
        break;

    case TEST_SHIFT_RIGHT:
        rc = gxcr_pka_shift_right(handle, user_data_ptr, basic->first,
                                  basic->shift_cnt);
        break;

    default:
        Assert(FALSE);
    }

    if (rc != PKA_NO_ERROR)
    {
        printf("Bad submit test_name=%s rc=%d\n",
               test_name_to_string(test_name), rc);
        return FAILURE;
    }

    return SUCCESS;
}



static status_t submit_mod_exp_test(gxcr_pka_handle_t *handle,
                                    user_data_t       *user_data_ptr,
                                    test_desc_t       *test_desc,
                                    uint32_t           test_idx)
{
    mod_exp_key_system_t *mod_exp_keys;
    pka_test_kind_t      *test_kind;
    pka_test_name_t       test_name;
    test_mod_exp_t       *test_mod_exp;
    gxcr_pka_err_t        rc;

    test_kind     = (pka_test_kind_t      *) test_desc->test_kind;
    mod_exp_keys  = (mod_exp_key_system_t *) test_desc->key_system;
    test_mod_exp  = (test_mod_exp_t       *) test_desc->test_operands;
    test_name     = test_kind->test_name;
    Assert (test_name == TEST_MOD_EXP);

    if (3 <= verbosity)
    {
        printf("\nRunning test_idx=%u test_name=%s\n", test_idx,
               test_name_to_string(test_name));
        print_pka_operand("base     = ", test_mod_exp->base,     "\n");
        print_pka_operand("exponent = ", test_mod_exp->exponent, "\n");
        print_pka_operand("modulus  = ", mod_exp_keys->modulus,  "\n");
    }

    // Modular exponentiation tests.  These use the mod_exp_key_system_t.
    rc = gxcr_pka_mod_exp(handle, user_data_ptr, test_mod_exp->exponent,
                          mod_exp_keys->modulus, test_mod_exp->base);
    if (rc != PKA_NO_ERROR)
    {
        printf("Bad submit test_name=%s rc=%d\n",
               test_name_to_string(test_name), rc);
        return FAILURE;
    }

    return SUCCESS;
}



static status_t submit_rsa_test(gxcr_pka_handle_t *handle,
                                user_data_t       *user_data_ptr,
                                test_desc_t       *test_desc,
                                uint32_t           test_idx)
{
    rsa_key_system_t *rsa_keys;
    pka_test_kind_t  *test_kind;
    pka_test_name_t   test_name;
    gxcr_pka_err_t    rc;
    test_rsa_t       *test_rsa;

    test_kind = (pka_test_kind_t  *) test_desc->test_kind;
    rsa_keys  = (rsa_key_system_t *) test_desc->key_system;
    test_rsa  = (test_rsa_t       *) test_desc->test_operands;
    test_name = test_kind->test_name;

    if (3 <= verbosity)
        printf("\nRunning test_idx=%u test_name=%s\n", test_idx,
               test_name_to_string(test_name));

    switch (test_name)
    {
    case TEST_RSA_MOD_EXP:
        if (3 <= verbosity)
        {
            print_pka_operand("base     = ", test_rsa->msg,         "\n");
            print_pka_operand("exponent = ", rsa_keys->private_key, "\n");
            print_pka_operand("modulus  = ", rsa_keys->n,           "\n");
        }

        rc = gxcr_pka_mod_exp(handle, user_data_ptr, rsa_keys->private_key,
                              rsa_keys->n, test_rsa->msg);
        break;

    case TEST_RSA_VERIFY:
        if (3 <= verbosity)
        {
            print_pka_operand("base     = ", test_rsa->msg,        "\n");
            print_pka_operand("exponent = ", rsa_keys->public_key, "\n");
            print_pka_operand("modulus  = ", rsa_keys->n,          "\n");
        }

        rc = gxcr_pka_mod_exp(handle, user_data_ptr, rsa_keys->public_key,
                              rsa_keys->n, test_rsa->msg);
        break;

    case TEST_RSA_MOD_EXP_WITH_CRT:
        if (3 <= verbosity)
        {
            print_pka_operand("msg      = ", test_rsa->msg,  "\n");
            print_pka_operand("d_p      = ", rsa_keys->d_p,  "\n");
            print_pka_operand("d_q      = ", rsa_keys->d_q,  "\n");
            print_pka_operand("p        = ", rsa_keys->p,    "\n");
            print_pka_operand("q        = ", rsa_keys->q,    "\n");
            print_pka_operand("qinv     = ", rsa_keys->qinv, "\n");
            print_pka_operand("p * q    = ", rsa_keys->n,    "\n");
        }

        rc = gxcr_pka_exp_with_crt(handle, user_data_ptr, rsa_keys->p,
                                   rsa_keys->q, test_rsa->msg, rsa_keys->d_p,
                                   rsa_keys->d_q, rsa_keys->qinv);
        break;

    default:
        Assert(FALSE);
    }

    if (rc != PKA_NO_ERROR)
    {
        printf("Bad submit test_name=%s rc=%d\n",
               test_name_to_string(test_name), rc);
        return FAILURE;
    }

    return SUCCESS;
}



static status_t submit_ecc_test(gxcr_pka_handle_t *handle,
                                user_data_t       *user_data_ptr,
                                test_desc_t       *test_desc,
                                uint32_t           test_idx)
{
    ecc_key_system_t *ecc_keys;
    pka_test_kind_t  *test_kind;
    pka_test_name_t   test_name;
    gxcr_pka_err_t    rc;
    ecc_curve_t      *curve;
    ecc_point_t      *pointA;
    test_ecc_t       *ecc_test;

    test_kind = (pka_test_kind_t  *) test_desc->test_kind;
    ecc_keys  = (ecc_key_system_t *) test_desc->key_system;
    ecc_test  = (test_ecc_t       *) test_desc->test_operands;
    curve     = ecc_keys->curve;
    pointA    = ecc_test->pointA;
    test_name = test_kind->test_name;

    if (3 <= verbosity)
    {
        printf("\nRunning test_idx=%u test_name=%s\n", test_idx,
               test_name_to_string(test_name));
        print_pka_operand("curve->p     = ", curve->p,  "\n");
        print_pka_operand("curve->a     = ", curve->a,  "\n");
        print_pka_operand("curve->b     = ", curve->b,  "\n");
        print_pka_operand("pointA->x    = ", pointA->x, "\n");
        print_pka_operand("pointA->y    = ", pointA->y, "\n");
    }

    switch (test_name)
    {
    case TEST_ECC_ADD:
        if (3 <= verbosity)
        {
            print_pka_operand("pointB->x    = ", ecc_test->pointB->x, "\n");
            print_pka_operand("pointB->y    = ", ecc_test->pointB->y, "\n");
        }

        rc = gxcr_pka_ecc_add(handle, user_data_ptr, curve, pointA,
                              ecc_test->pointB);
        break;

    case TEST_ECC_DOUBLE:
        rc = gxcr_pka_ecc_add(handle, user_data_ptr, curve, pointA, pointA);
        break;

    case TEST_ECC_MULTIPLY:
        if (3 <= verbosity)
            print_pka_operand("multiplier   = ", ecc_test->multiplier, "\n");

        rc = gxcr_pka_ecc_multiply(handle, user_data_ptr, curve, pointA,
                                   ecc_test->multiplier);
        break;

    default:
        Assert(FALSE);
    }

    if (rc != PKA_NO_ERROR)
    {
        printf("Bad submit test_name=%s rc=%d\n",
               test_name_to_string(test_name), rc);
        return FAILURE;
    }

    return SUCCESS;
}



static status_t submit_ecdsa_test(gxcr_pka_handle_t *handle,
                                  user_data_t       *user_data_ptr,
                                  test_desc_t       *test_desc,
                                  uint32_t           test_idx,
                                  boolean_t          first_submit)
{
    ecdsa_key_system_t *keys;
    dsa_signature_t    *signature;
    pka_test_kind_t    *test_kind;
    pka_test_name_t     test_name;
    gxcr_pka_err_t      rc;
    pka_operand_t      *base_pt_order, *hash;
    test_ecdsa_t       *ecdsa_test;
    ecc_curve_t        *curve;
    ecc_point_t        *base_pt;

    test_kind     = (pka_test_kind_t    *) test_desc->test_kind;
    keys          = (ecdsa_key_system_t *) test_desc->key_system;
    ecdsa_test    = (test_ecdsa_t       *) test_desc->test_operands;
    curve         = keys->curve;
    base_pt       = keys->base_pt;
    base_pt_order = keys->base_pt_order;
    hash          = ecdsa_test->hash;
    signature     = ecdsa_test->signature;
    test_name     = test_kind->test_name;

    if (3 <= verbosity)
    {
        printf("\nRunning test_idx=%u test_name=%s\n", test_idx,
               test_name_to_string(test_name));
        print_pka_operand("curve->p      = ", curve->p,      "\n");
        print_pka_operand("curve->a      = ", curve->a,      "\n");
        print_pka_operand("curve->b      = ", curve->b,      "\n");
        print_pka_operand("base_pt->x    = ", base_pt->x,    "\n");
        print_pka_operand("base_pt->y    = ", base_pt->y,    "\n");
        print_pka_operand("base_pt_order = ", base_pt_order, "\n");
        print_pka_operand("hash          = ", hash,          "\n");
    }

    if (test_name == TEST_ECDSA_GEN_VERIFY)
    {
        if (first_submit)
            test_name = TEST_ECDSA_GEN;
        else
            test_name = TEST_ECDSA_VERIFY;
    }

    switch (test_name)
    {
    case TEST_ECDSA_GEN:
        if (3 <= verbosity)
        {
            print_pka_operand("private_key   = ", keys->private_key, "\n");
            print_pka_operand("k             = ", ecdsa_test->k,     "\n");
        }

        rc = gxcr_pka_ecdsa_gen(handle, user_data_ptr, curve, base_pt,
                                base_pt_order, keys->private_key,
                                ecdsa_test->hash, ecdsa_test->k);
        break;

    case TEST_ECDSA_VERIFY:
        if (3 <= verbosity)
        {
            print_pka_operand("public_key->x = ", keys->public_key->x, "\n");
            print_pka_operand("public_key->y = ", keys->public_key->y, "\n");
            print_pka_operand("signature->r  = ", signature->r,        "\n");
            print_pka_operand("signature->s  = ", signature->s,        "\n");
        }

        rc = gxcr_pka_ecdsa_verify(handle, user_data_ptr, curve, base_pt,
                                   base_pt_order, keys->public_key, hash,
                                   ecdsa_test->signature);
        break;

    default:
        Assert(FALSE);
    }

    if (rc != PKA_NO_ERROR)
    {
        printf("Bad submit test_name=%s rc=%d\n",
               test_name_to_string(test_name), rc);
        return FAILURE;
    }

    return SUCCESS;
}



static status_t submit_dsa_test(gxcr_pka_handle_t *handle,
                                user_data_t       *user_data_ptr,
                                test_desc_t       *test_desc,
                                uint32_t           test_idx,
                                boolean_t          first_submit)
{
    dsa_key_system_t *keys;
    pka_test_kind_t  *test_kind;
    pka_test_name_t   test_name;
    dsa_signature_t  *signature;
    gxcr_pka_err_t    rc;
    test_dsa_t       *dsa_test;

    test_kind = (pka_test_kind_t  *) test_desc->test_kind;
    keys      = (dsa_key_system_t *) test_desc->key_system;
    dsa_test  = (test_dsa_t       *) test_desc->test_operands;
    signature = dsa_test->signature;
    test_name = test_kind->test_name;

    if (3 <= verbosity)
    {
        printf("\nRunning test_idx=%u test_name=%s\n", test_idx,
               test_name_to_string(test_name));
        print_pka_operand("p            = ", keys->p,        "\n");
        print_pka_operand("q            = ", keys->q,        "\n");
        print_pka_operand("g            = ", keys->g,        "\n");
        print_pka_operand("hash         = ", dsa_test->hash, "\n");
    }

    if (test_name == TEST_DSA_GEN_VERIFY)
    {
        if (first_submit)
            test_name = TEST_DSA_GEN;
        else
            test_name = TEST_DSA_VERIFY;
    }

    switch (test_name)
    {
    case TEST_DSA_GEN:
        if (3 <= verbosity)
        {
            print_pka_operand("private_key  = ", keys->private_key, "\n");
            print_pka_operand("k            = ", dsa_test->k,       "\n");
        }

        rc = gxcr_pka_dsa_gen(handle, user_data_ptr, keys->p, keys->q, keys->g,
                              keys->private_key, dsa_test->hash, dsa_test->k);
        break;

    case TEST_DSA_VERIFY:
        if (3 <= verbosity)
        {
            print_pka_operand("public_key   = ", keys->private_key, "\n");
            print_pka_operand("signature->r = ", signature->r,      "\n");
            print_pka_operand("signature->s = ", signature->s,      "\n");
        }

        rc = gxcr_pka_dsa_verify(handle, user_data_ptr, keys->p, keys->q,
                                 keys->g, keys->public_key, dsa_test->hash,
                                 dsa_test->signature);
        break;

    default:
        Assert(FALSE);
    }

    if (rc != PKA_NO_ERROR)
    {
        printf("Bad submit test_name=%s rc=%d\n",
               test_name_to_string(test_name), rc);
        return FAILURE;
    }

    return SUCCESS;
}



static NOINLINE status_t submit_pka_test(gxcr_pka_handle_t *handle,
                                         user_data_t       *user_data_ptr,
                                         boolean_t          first_submit)
{
    pka_test_name_t  test_name;
    pka_test_kind_t *test_kind;
    test_stats_t    *test_stats;
    test_desc_t     *test_desc;
    uint32_t         test_idx;
    status_t         status;

    test_idx   = user_data_ptr->test_idx;
    test_desc  = user_data_ptr->test_desc;
    test_kind  = (pka_test_kind_t *) test_desc->test_kind;
    test_stats = user_data_ptr->test_desc_stats;
    test_name  = test_kind->test_name;
    Assert(test_desc->test_category == PKA_TEST);

    if (first_submit)
        user_data_ptr->start_time = get_cycle_count();

    switch (test_name)
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
        status = submit_basic_test(handle, user_data_ptr, test_desc, test_idx);
        break;

    case TEST_MOD_EXP:
        status = submit_mod_exp_test(handle, user_data_ptr, test_desc,
                                     test_idx);
        break;

    case TEST_RSA_MOD_EXP:
    case TEST_RSA_VERIFY:
    case TEST_RSA_MOD_EXP_WITH_CRT:
        status = submit_rsa_test(handle, user_data_ptr, test_desc, test_idx);
        break;

    case TEST_ECC_ADD:
    case TEST_ECC_DOUBLE:
    case TEST_ECC_MULTIPLY:
        status = submit_ecc_test(handle, user_data_ptr, test_desc, test_idx);
        break;

    case TEST_ECDSA_GEN:
    case TEST_ECDSA_VERIFY:
    case TEST_ECDSA_GEN_VERIFY:
        if (test_name  == TEST_ECDSA_GEN_VERIFY)
            user_data_ptr->multi_submit = TRUE;

        status = submit_ecdsa_test(handle, user_data_ptr, test_desc, test_idx,
                                   first_submit);
        break;

    case TEST_DSA_GEN:
    case TEST_DSA_VERIFY:
    case TEST_DSA_GEN_VERIFY:
        if (test_name  == TEST_DSA_GEN_VERIFY)
            user_data_ptr->multi_submit = TRUE;

        status = submit_dsa_test(handle, user_data_ptr, test_desc, test_idx,
                                 first_submit);
        break;

    default:
        Assert(FALSE);
        status = FAILURE;
    }

    test_stats->num_submitted++;
    if (status == SUCCESS)
        test_stats->num_good_replies++;
    else
    {
        test_stats->num_bad_replies++;
        test_stats->errors++;
    }

    return status;
}



static status_t chk_basic_test_results(test_desc_t   *test_desc,
                                       pka_results_t *results)
{
    pka_operand_t *result, *answer;
    test_basic_t  *basic;

    // *TBD* TEST_MOD_DIV needs two answers!
    result = &results->results[0];
    basic  = (test_basic_t *) test_desc->test_operands;
    answer = basic->answer;

    if (3 <= verbosity)
    {
        print_pka_operand("result = ", result, "\n");
        print_pka_operand("answer = ", answer, "\n");
    }

    // Compare result and answer to make sure they agree.
    if (pka_compare(result, answer) == PKA_EQUAL)
        return SUCCESS;
    else
        return FAILURE;
}

static status_t chk_mod_exp_test_results(test_desc_t   *test_desc,
                                         pka_results_t *results)
{
    test_mod_exp_t *test_mod_exp;
    pka_operand_t  *result, *answer;

    result       = &results->results[0];
    test_mod_exp = (test_mod_exp_t *) test_desc->test_operands;
    answer       = test_mod_exp->answer;

    if (3 <= verbosity)
    {
        print_pka_operand("result   = ", result, "\n");
        print_pka_operand("answer   = ", answer, "\n");
    }

    // Compare result and answer to make sure they agree.
    if (pka_compare(result, answer) == PKA_EQUAL)
        return SUCCESS;
    else
        return FAILURE;
}

static status_t chk_rsa_test_results(test_desc_t   *test_desc,
                                     pka_results_t *results)
{
    pka_operand_t *result, *answer;
    test_rsa_t    *test_rsa;

    result   = &results->results[0];
    test_rsa = (test_rsa_t *) test_desc->test_operands;
    answer   = test_rsa->answer;

    if (3 <= verbosity)
    {
        print_pka_operand("result   = ", result, "\n");
        print_pka_operand("answer   = ", answer, "\n");
    }

    // Compare result and answer to make sure they agree.
    if (pka_compare(result, answer) == PKA_EQUAL)
        return SUCCESS;
    else
        return FAILURE;
}

static status_t chk_ecc_test_results(gxcr_pka_handle_t *handle,
                                     test_desc_t       *test_desc,
                                     pka_results_t     *results)
{
    ecc_point_t *answer_pt, *result_pt;
    test_ecc_t  *ecc_test;

    Assert(results->result_cnt == 2);
    result_pt    = calloc(1, sizeof(ecc_point_t));
    result_pt->x = dup_operand(handle, &results->results[0]);
    result_pt->x = dup_operand(handle, &results->results[1]);
    ecc_test     = (test_ecc_t *) test_desc->test_operands;
    answer_pt    = ecc_test->answer;

    if (3 <= verbosity)
    {
        print_pka_operand("result_pt->x = ", result_pt->x, "\n");
        print_pka_operand("result_pt->y = ", result_pt->y, "\n");
        print_pka_operand("answer_pt->x = ", answer_pt->x, "\n");
        print_pka_operand("answer_pt->y = ", answer_pt->y, "\n");
    }

    if (ecc_points_are_equal(handle, result_pt, answer_pt))
        return SUCCESS;
    else
        return FAILURE;
}

static status_t chk_ecdsa_test_results(test_desc_t   *test_desc,
                                       pka_results_t *results)
{
    pka_test_kind_t  *test_kind;
    pka_test_name_t   test_name;
    dsa_signature_t  *signature;
    test_ecdsa_t     *ecdsa_test;
    status_t          status;

    test_kind  = (pka_test_kind_t *) test_desc->test_kind;
    ecdsa_test = (test_ecdsa_t    *) test_desc->test_operands;
    signature  = ecdsa_test->signature;
    test_name  = test_kind->test_name;

    switch (test_name)
    {
    case TEST_ECDSA_GEN:
        if (3 <= verbosity)
        {
            print_pka_operand("signature->r  = ", signature->r, "\n");
            print_pka_operand("signature->s  = ", signature->s, "\n");
        }

        if (signatures_are_equal(signature, ecdsa_test->signature))
            status = SUCCESS;
        else
            status = FAILURE;

        break;

    case TEST_ECDSA_VERIFY:
    case TEST_ECDSA_GEN_VERIFY:
        status = (results->compare_result == PKA_EQUAL) ? SUCCESS : FAILURE;
        break;

    default:
        Assert(FALSE);
    }

    if (3 <= verbosity)
    {
        if (status == SUCCESS)
            printf("run_ecdsa_test SUCCESS\n");
        else
            printf("run_ecdsa_test FAILURE\n");
    }

    return status;
}

static status_t chk_dsa_test_results(test_desc_t   *test_desc,
                                     pka_results_t *results)
{
    pka_test_kind_t  *test_kind;
    pka_test_name_t   test_name;
    dsa_signature_t  *signature;
    test_dsa_t       *dsa_test;
    status_t          status;

    test_kind = (pka_test_kind_t *) test_desc->test_kind;
    dsa_test  = (test_dsa_t      *) test_desc->test_operands;
    signature = dsa_test->signature;
    test_name = test_kind->test_name;

    switch (test_kind->test_name)
    {
    case TEST_DSA_GEN:
        if (3 <= verbosity)
        {
            print_pka_operand("signature->r  = ", signature->r, "\n");
            print_pka_operand("signature->s  = ", signature->s, "\n");
        }

        if (signatures_are_equal(signature, dsa_test->signature))
            status = SUCCESS;
        else
            status = FAILURE;

        break;

    case TEST_DSA_VERIFY:
    case TEST_DSA_GEN_VERIFY:
        status = (results->compare_result == PKA_EQUAL) ? SUCCESS : FAILURE;
        break;

    default:
        Assert(FALSE);
    }

    if (3 <= verbosity)
    {
        if (status == SUCCESS)
            printf("run_dsa_test SUCCESS\n");
        else
            printf("run_dsa_test FAILURE\n");
    }

    return status;
}



static NOINLINE status_t chk_test_results(gxcr_pka_handle_t *handle,
                                          pka_test_name_t    test_name,
                                          test_desc_t       *test_desc,
                                          pka_results_t     *results)
{
    switch (test_name)
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
        return chk_basic_test_results(test_desc, results);

    case TEST_MOD_EXP:
        return chk_mod_exp_test_results(test_desc, results);

    case TEST_RSA_MOD_EXP:
    case TEST_RSA_VERIFY:
    case TEST_RSA_MOD_EXP_WITH_CRT:
        return chk_rsa_test_results(test_desc, results);

    case TEST_ECC_ADD:
    case TEST_ECC_DOUBLE:
    case TEST_ECC_MULTIPLY:
        return chk_ecc_test_results(handle, test_desc, results);

    case TEST_ECDSA_GEN:
    case TEST_ECDSA_VERIFY:
    case TEST_ECDSA_GEN_VERIFY:
        return chk_ecdsa_test_results(test_desc, results);

    case TEST_DSA_GEN:
    case TEST_DSA_VERIFY:
    case TEST_DSA_GEN_VERIFY:
        return chk_dsa_test_results(test_desc, results);

    default:
        Assert(FALSE);
        return FAILURE;
    }
}

static boolean_t process_pka_test_results(gxcr_pka_handle_t *handle,
                                          pka_results_t     *results,
                                          uint64_t           test_end_time)
{
    pka_test_kind_t *test_kind;
    test_stats_t    *test_stats;
    test_desc_t     *test_desc;
    user_data_t     *user_data_ptr;
    uint64_t         latency;
    status_t         status;

    user_data_ptr = (user_data_t     *) results->user_data;
    test_desc     = (test_desc_t     *) user_data_ptr->test_desc;
    test_kind     = (pka_test_kind_t *) test_desc->test_kind;
    test_stats    = user_data_ptr->test_desc_stats;

    if ((user_data_ptr->multi_submit) && (user_data_ptr->first_submit))
    {
        submit_pka_test(handle, user_data_ptr, FALSE);
        return FALSE;
    }

    if (check_results)
        status = chk_test_results(handle, test_kind->test_name, test_desc,
                                  results);
    else
        status = SUCCESS;

    if (status == SUCCESS)
        test_stats->num_correct_answers++;
    else
    {
        test_stats->num_wrong_answers++;
        test_stats->errors++;
    }

    latency = (test_end_time - user_data_ptr->start_time) / 256;
    test_stats->total_latency   += latency;
    test_stats->latency_squared += latency * latency;
    test_stats->min_latency      = MIN(test_stats->min_latency, latency);
    test_stats->max_latency      = MAX(test_stats->max_latency, latency);
    return TRUE;
}



static void join_threads(uint32_t num_threads)
{
    uint32_t thread_idx;

    for (thread_idx = 0; thread_idx < num_threads; thread_idx++)
        pthread_join(threads[thread_idx], NULL);
}



static void execute_tests_by_thread(uint32_t        thread_idx,
                                    thread_state_t *thread_state)
{
    gxcr_pka_handle_t *handle;
    pka_results_t     *results;
    user_data_t       *user_data_ptr, user_data[256];
    uint64_t           thread_start_time, thread_end_time, test_end_time;
    uint32_t           outstanding_cmds, failure_cnt;
    uint32_t           total_cmds_done, num_cmds, test_idx, test_desc_idx;
    uint32_t           total_cmds_submitted, user_data_idx, cmds_left_to_submit;

    handle = gxcr_pka_open(big_endian);
    if (handle == NULL)
    {
        printf("Failed to open gxcr/pka handle on thread_idx=%u\n",
               thread_idx);
        return;
    }

    memset(user_data, 0, sizeof (user_data));
    for (user_data_idx = 0;  user_data_idx < 256;  user_data_idx++)
    {
        user_data[user_data_idx].user_data_idx = user_data_idx;
        user_data[user_data_idx].thread_idx    = thread_idx;
    }

    outstanding_cmds     = 0;
    total_cmds_submitted = 0;
    total_cmds_done      = 0;
    failure_cnt          = 0;
    thread_start_time    = get_cycle_count();
    test_desc_idx        = 0;
    num_cmds             = num_tests * submits_per_test;
    user_data_idx        = 0;
    results              = malloc_results(2, MAX_BYTE_LEN + 8);

    while (TRUE)
    {
        if (gxcr_pka_has_results(handle))
        {
            test_end_time = get_cycle_count();
            gxcr_pka_get_results(handle, results);
            if (process_pka_test_results(handle, results, test_end_time))
            {
                outstanding_cmds--;
                total_cmds_done++;
                if (num_cmds <= total_cmds_done)
                    break;
            }
        }

        cmds_left_to_submit = num_cmds - total_cmds_submitted;
        if ((cmds_left_to_submit != 0) && (outstanding_cmds < cmds_outstanding))
        {
            // Need to get the next user_data structure to use and the next
            // test_desc idx to run.
            user_data_ptr = &user_data[user_data_idx++];
            if (256 <= user_data_idx)
                user_data_idx = 0;

            test_idx = thread_state->randomize_tests[test_desc_idx++];
            if (num_tests <= test_desc_idx)
                test_desc_idx = 0;

            user_data_ptr->test_idx  = test_idx;
            user_data_ptr->test_desc = test_descs[test_idx];
            user_data_ptr->test_desc_stats = &thread_state->test_desc_stats[test_idx];
            if (SUCCESS == submit_pka_test(handle, user_data_ptr, TRUE))
            {
                outstanding_cmds++;
                total_cmds_submitted++;
            }
            else if (10 < failure_cnt)
                break;
            else
                failure_cnt++;
        }
        else
            busy_delay();
    }

    thread_end_time             = get_cycle_count();
    thread_state->thread_cycles = thread_end_time - thread_start_time;
    gxcr_pka_close(handle);
}

static void *pka_test_thread(void *arg)
{
    thread_arg_t *thread_arg_ptr;
    uint32_t      thread_idx;

    thread_arg_ptr = (thread_arg_t *) arg;
    thread_idx     = thread_arg_ptr->thread_idx;
    tmc_cpus_set_my_cpu(thread_arg_ptr->cpu_number);

    // Wait here for all threads to be ready.
    tmc_spin_barrier_wait(&thread_start_barrier);

    execute_tests_by_thread(thread_idx, &thread_states[thread_idx]);

    __insn_mf();
    return NULL;
}

static status_t create_tests (gxcr_pka_handle_t *handle)
{
    thread_state_t *thread_state;
    test_stats_t   *test_desc_stats;
    uint32_t        thread_idx, test_idx;
    uint16_t       *randomize_tests;
    status_t        status;

    free_test_descs(test_descs);
    if (SUCCESS != chk_bit_lens(&test_kind))
        return FAILURE;

    num_tests = test_kind.num_key_systems * test_kind.tests_per_key_system;
    status    = create_pka_test_descs(handle, &test_kind, test_descs,
                                      check_results, verbosity);
    if (status != SUCCESS)
        return FAILURE;

    // Now create the per thread test structures.
    // randomize array.
    for (thread_idx = 0;  thread_idx < num_of_threads;  thread_idx++)
    {
        thread_state = &thread_states[thread_idx];
        if (thread_state->randomize_tests != NULL)
            free(thread_state->randomize_tests);

        if (thread_state->test_desc_stats != NULL)
            free(thread_state->test_desc_stats);

        randomize_tests = malloc(num_tests * sizeof(uint16_t));
        test_desc_stats = malloc(num_tests * sizeof(test_stats_t));

        memset(randomize_tests, 0, num_tests * sizeof(uint16_t));
        memset(test_desc_stats, 0, num_tests * sizeof(test_stats_t));

        thread_state->randomize_tests = randomize_tests;
        thread_state->test_desc_stats = test_desc_stats;

        // For now just use identity map.
        for (test_idx = 0;  test_idx < num_tests;  test_idx++)
            randomize_tests[test_idx] = test_idx;

        // Init the min_latency field to a big number.
        for (test_idx = 0;  test_idx < num_tests;  test_idx++)
            test_desc_stats[test_idx].min_latency = 0xFFFFFFFF;
    }

    return SUCCESS;
}

static int run_test(gxcr_pka_handle_t *handle)
{
    cpu_set_t dataplane_cpus, other_cpus;
    uint32_t  num_dataplane_cpus, num_other_cpus, total_num_cpus, thread_idx;
    int64_t   worker_cpu;
    int       rc;

    // Next create the common test cases and distribute amongst the various
    // worker tiles.
    if (SUCCESS != create_tests(handle))
    {
        printf("create_tests failure\n");
        return -3;
    }

    tmc_cpus_get_dataplane_cpus(&dataplane_cpus);
    tmc_cpus_get_online_cpus(&other_cpus);
    tmc_cpus_remove_cpus(&other_cpus, &dataplane_cpus);

    num_dataplane_cpus = tmc_cpus_count(&dataplane_cpus);
    num_other_cpus     = tmc_cpus_count(&other_cpus);
    total_num_cpus     = num_dataplane_cpus + num_other_cpus;
    if (total_num_cpus < num_of_threads)
        return -3;

    for (thread_idx = 0; thread_idx < num_of_threads; thread_idx++)
    {
        worker_cpu = -1;
        if (thread_idx < num_dataplane_cpus)
            worker_cpu = tmc_cpus_find_nth_cpu(&dataplane_cpus, thread_idx);
        else if (thread_idx < total_num_cpus)
            worker_cpu = tmc_cpus_find_nth_cpu(&other_cpus,
                                               thread_idx - num_dataplane_cpus);

        if (worker_cpu < 0)
            return -1;

        thread_args[thread_idx].thread_idx = thread_idx;
        thread_args[thread_idx].cpu_number = worker_cpu;

        rc = pthread_create(&threads[thread_idx], NULL, pka_test_thread,
                            &thread_args[thread_idx]);
        if (rc != 0)
        {
            printf("Error creating the server threads\n");
            break;
        }
    }

    // Next start the threads.
    tmc_spin_barrier_wait(&thread_start_barrier);
    overall_start_time = get_cycle_count();
    printf("Running the tests.\n");

    // Wait for each thread to complete their running of the test.
    join_threads(num_of_threads);
    overall_end_time = get_cycle_count();
    return 0;
}



static uint64_t to_usecs(uint64_t cycles_div_256)
{
    return (1000000 * 256 * cycles_div_256) / cpu_freq;
}

static void update_test_stats(test_stats_t *test_stats, uint32_t num)
{
    uint64_t avg, var, std_dev;

    avg     = test_stats->total_latency / num;
    var     = (test_stats->latency_squared / num) - (avg * avg);
    std_dev = isqrt(var);

    test_stats->min_latency     = to_usecs(test_stats->min_latency);
    test_stats->max_latency     = to_usecs(test_stats->max_latency);
    test_stats->avg_latency     = to_usecs(avg);
    test_stats->latency_std_dev = to_usecs(std_dev);
}

static void analyze_results (void)
{
    thread_state_t *thread_state;
    test_stats_t   *thread_stats, *test_stats;
    uint32_t        thread_idx, test_idx, num, bad_results;

    memset(&overall_test_stats, 0, sizeof(overall_test_stats));
    overall_cmds_done              = 0;
    overall_bad_results            = 0;
    overall_test_stats.min_latency = 0xFFFFFFFF;

    for (thread_idx = 0; thread_idx < num_of_threads; thread_idx++)
    {
        thread_state = &thread_states[thread_idx];
        thread_stats = &thread_state->thread_stats;
        thread_stats->min_latency = 0xFFFFFFFF;

        for (test_idx = 0; test_idx < num_tests; test_idx++)
        {
            test_stats  = &thread_state->test_desc_stats[test_idx];
            num         = test_stats->num_submitted;
            bad_results = test_stats->errors;

            thread_stats->num_submitted   += num;
            thread_stats->errors          += bad_results;
            thread_stats->total_latency   += test_stats->total_latency;
            thread_stats->latency_squared += test_stats->latency_squared;
            thread_stats->min_latency      = MIN(thread_stats->min_latency,
                                                 test_stats->min_latency);
            thread_stats->max_latency      = MAX(thread_stats->max_latency,
                                                 test_stats->max_latency);

            if (num != 0)
                update_test_stats(test_stats, num);
        }

        num = thread_stats->num_submitted;

        overall_cmds_done                  += num;
        overall_bad_results                += thread_stats->errors;
        overall_test_stats.total_latency   += thread_stats->total_latency;
        overall_test_stats.latency_squared += thread_stats->latency_squared;
        overall_test_stats.min_latency = MIN(overall_test_stats.min_latency,
                                             thread_stats->min_latency);
        overall_test_stats.max_latency = MAX(overall_test_stats.max_latency,
                                             thread_stats->max_latency);

        if (num != 0)
            update_test_stats(thread_stats, num);
    }

    if (overall_cmds_done != 0)
        update_test_stats(&overall_test_stats, overall_cmds_done);
}

static void report_per_thread_results(void)
{
    thread_state_t *thread_state;
    test_stats_t   *thread_stats, *test_stats;
    uint64_t        microsecs64, millisecs64;
    uint32_t        thread_idx, test_idx, bad_results, num_cmds;
    uint32_t        usecs_rem, cmds_per_sec;

    for (thread_idx = 0; thread_idx < num_of_threads; thread_idx++)
    {
        thread_state = &thread_states[thread_idx];
        thread_stats = &thread_state->thread_stats;

        // Only report per thread per test stats if the num_tests is > 1.
        if (1 < num_tests)
        {
            for (test_idx = 0; test_idx < num_tests; test_idx++)
            {
                test_stats   = &thread_state->test_desc_stats[test_idx];
                bad_results  = test_stats->errors;
                num_cmds     = test_stats->num_submitted;

                printf("    thread_idx=%u test_idx=%u num=%u errors=%u min=%u "
                       "avg=%u max=%u std_dev=%u (usecs)\n",
                       thread_idx, test_idx, num_cmds, bad_results,
                       (uint32_t) test_stats->min_latency,
                       (uint32_t) test_stats->avg_latency,
                       (uint32_t) test_stats->max_latency,
                       (uint32_t) test_stats->latency_std_dev);
            }
        }

        microsecs64  = (1000000 * thread_state->thread_cycles) / cpu_freq;
        millisecs64  = microsecs64 / 1000;
        usecs_rem    = (uint32_t) (microsecs64 - (millisecs64 * 1000));
        num_cmds     = thread_stats->num_submitted;
        cmds_per_sec = (uint32_t) ((1000000 * (uint64_t) num_cmds) /
                                   microsecs64);

        printf("  thread_idx=%u millisecs=%u.%03u num cmds=%u errors=%u "
               "cmdsPerSec=%u\n", thread_idx, (uint32_t) millisecs64,
               usecs_rem, num_cmds, thread_stats->errors, cmds_per_sec);

        printf("  thread_idx=%u min=%u avg=%u max=%u std_dev=%u (usecs)\n\n",
               thread_idx, (uint32_t) thread_stats->min_latency,
               (uint32_t) thread_stats->avg_latency,
               (uint32_t) thread_stats->max_latency,
               (uint32_t) thread_stats->latency_std_dev);
    }
}

static void report_results(uint64_t total_time_cycles, uint32_t num_threads)
{
    uint64_t microsecs64, millisecs64;
    uint32_t usecs_rem, num_cmds, cmds_per_sec;

    microsecs64  = (1000000 * total_time_cycles) / cpu_freq;
    millisecs64  = microsecs64 / 1000;
    usecs_rem    = (uint32_t) (microsecs64 - (millisecs64 * 1000));
    num_cmds     = overall_cmds_done;
    cmds_per_sec = (uint32_t) ((1000000 * (uint64_t) num_cmds) /
                               microsecs64);

    printf("\n");
    printf("total time millisecs=%u.%03u total cmds=%u errors=%u cmdsPerSec=%u "
           "(bit_len=%u",
           (uint32_t) millisecs64, usecs_rem, num_cmds, overall_bad_results,
           cmds_per_sec, test_kind.bit_len);

    if (test_kind.second_bit_len != 0)
        printf(" second_bit_len=%u", test_kind.second_bit_len);

    printf(")\n");

    if (overall_cmds_done != 0)
        printf("latency num_threads=%u min=%u avg=%u max=%u std_dev=%u "
               "(usecs)\n",
               num_threads, (uint32_t) overall_test_stats.min_latency,
               (uint32_t) overall_test_stats.avg_latency,
               (uint32_t) overall_test_stats.max_latency,
               (uint32_t) overall_test_stats.latency_std_dev);

    if (report_thread_stats)
    {
        printf("\nPer Thread Per Test results:\n");
        report_per_thread_results();
    }
}



static void print_usage(void)
{
    printf("'./speed_test <options>' - where options can be:\n");
    printf("  -b <bit_len>         primary bit_len to use\n");
    printf("  -e ( big | little )  endianness of the interface\n");
    printf("  -h                   print this message and exit\n");
    printf("  -k <num_keys>        num of different key subsystems to make\n");
    printf("  -m <runs_per_test>   num of runs of each test per thread\n");
    printf("  -n <num_tests>       num of tests (per key subsystem) to make\n");
    printf("  -q <cmds_outstanding>number of cmds each thread keeps in play\n");
    printf("  -r                   report the per thread stats/results\n");
    printf("  -s <second_bit_len>  secondary bit_len for some cryptosystems\n");
    printf("  -t <num_threads>     number of threads/tiles to use\n");
    printf("  -v <verbosity>       verbosity level - in range 0-3\n");
    printf("  -y ( yes | no )      check_results if set to yes\n");
    printf("  -c <test_kind>       name of the test kind.  One of:\n");
    printf("     ADD, SUBTRACT, MULTIPLY, DIVIDE, DIV_MOD, MODULO\n");
    printf("     SHIFT_LEFT, SHIFT_RIGHT, MOD_INVERT\n");
    printf("     MOD_EXP, RSA_MOD_EXP, RSA_VERIFY, RSA_MOD_EXP_WITH_CRT\n");
    printf("     ECC_ADD, ECC_DOUBLE, ECC_MULTIPLY\n");
    printf("     ECDSA_GEN, ECDSA_VERIFY, ECDSA_GEN_VERIFY\n");
    printf("     DSA_GEN, DSA_VERIFY, DSA_GEN_VERIFY\n\n");
    printf("The default command line options (except for -b and -s) are:\n");
    printf("  '-c MOD_EXP -e big -k 1 -m 100 -n 4 -q 10 -t 1 -v 0 -y no'\n");
    printf("The defaults for '-b' and '-s' depend upon the test name (as \n");
    printf("given by '-c') as follows:\n");
    printf("a) the default for '-b' is 1024 for all tests except\n");
    printf("   for the ECC_* tests and ECDSA_* tests when it is 256.\n");
    printf("b) the default for -s is 33 for RSA_VERIFY, 'bit_len - 1'\n");
    printf("   for DIVIDE, DIV_MOD, MODULO, and DSA_* tests, 'bit_len / 2'\n");
    printf("   for the ECDSA_* tests and unused for for all other tests.\n\n");
}



static status_t process_options(int argc, char *argv[])
{
    pka_test_name_t test_name;
    uint32_t        num_tests, thread_cnt, bit_len, test_runs, key_systems;
    uint32_t        num_outstanding;
    int             optionChar;

    while ((optionChar = getopt(argc, argv, "b:c:e:hk:m:n:q:rs:t:v:y:")) != -1)
    {
        switch (optionChar)
        {
        case 'b':
            bit_len = atoi(optarg);
            if (bit_len <= 32)
            {
                printf("primary key bit_len must be >= 33 bits\n");
                return FAILURE;
            }
            else if (4096 < bit_len)
            {
                printf("primary key bit_len must be <= 4096 bits\n");
                return FAILURE;
            }
            else
                test_kind.bit_len = bit_len;
            break;

        case 'c':
            test_name = lookup_test_name(optarg);
            if (test_name == TEST_NOP)
            {
                printf("Option -c needs to be followed by legal test_name\n");
                return FAILURE;
            }
            else
                test_kind.test_name = test_name;

            break;

        case 'e':
            if (strcasecmp(optarg, "big") == 0)
                big_endian = TRUE;
            else if (strcasecmp(optarg, "little") == 0)
                big_endian = FALSE;
            else
            {
                printf("Option -e needs to be followed by either"
                       " 'big' or 'little'\n");
                return FAILURE;
            }
            break;

        case 'h':
            help = TRUE;
            break;

        case 'k':
            key_systems = atoi(optarg);
            if (key_systems != 1)
            {
                printf("currenly the num of key systems can only be 1\n");
                return FAILURE;
            }
            else
                test_kind.num_key_systems = key_systems;
            break;

        case 'm':
            test_runs = atoi(optarg);
            if (test_runs == 0)
            {
                printf("num of times to run each test cannot be 0\n");
                return FAILURE;
            }
            else if (1000000 < test_runs)
            {
                printf("num of times to run each test) (per thread) must be "
                       "<= 1000000\n");
                return FAILURE;
            }
            else
                submits_per_test = test_runs;
            break;

        case 'n':
            num_tests = atoi(optarg);
            if (num_tests == 0)
            {
                printf("num of tests per key subsystem cannot be 0\n");
                return FAILURE;
            }
            else if (MAX_NUM_TESTS < num_tests)
            {
                printf("num of tests per key subsystem must be <= %u\n",
                       MAX_NUM_TESTS);
                return FAILURE;
            }
            else
                test_kind.tests_per_key_system = num_tests;
            break;

        case 'q':
            num_outstanding = atoi(optarg);
            if (num_outstanding == 0)
            {
                printf("num of oustanding cmds cannot be 0\n");
                return FAILURE;
            }
            else if (128 < num_outstanding)
            {
                printf("num of oustanding cmds must be <= 128\n");
                return FAILURE;
            }
            else
                cmds_outstanding = num_outstanding;
            break;

        case 'r':
            report_thread_stats = TRUE;
            break;

        case 's':
            bit_len = atoi(optarg);
            if (bit_len <= 8)
            {
                printf("secondary key bit_len must be >= 9 bits\n");
                return FAILURE;
            }
            else if (4095 < bit_len)
            {
                printf("secondary key bit_len must be <= 4095 bits\n");
                return FAILURE;
            }
            else
                test_kind.second_bit_len = bit_len;
            break;

        case 't':
            thread_cnt = atoi(optarg);
            if (thread_cnt == 0)
            {
                printf("number of threads/tiles cannot be 0\n");
                return FAILURE;
            }
            else if (MAX_SERVER_TILES < thread_cnt)
            {
                printf("number of threads/tiles must be <= %u\n",
                       MAX_SERVER_TILES);
                return FAILURE;
            }
            else
                num_of_threads = atoi(optarg);
            break;

        case 'v':
            verbosity = atoi(optarg);
            break;

        case 'y':
            if (strcasecmp(optarg, "yes") == 0)
                check_results = TRUE;
            else if (strcasecmp(optarg, "no") == 0)
                check_results = FALSE;
            else
            {
                printf("Option -y needs to be followed by either"
                       " 'yes' or 'no'\n");
                return FAILURE;
            }
            break;
        }
    }

    return SUCCESS;
}



int main (int argc, char *argv[])
{
    gxcr_pka_handle_t *handle;
    int                return_code = 0;

    // Set argument defaults:
    num_of_threads      = 1;
    cmds_outstanding    = 10;
    submits_per_test    = 100;
    check_results       = FALSE;
    report_thread_stats = FALSE;
    big_endian          = TRUE;
    help                = FALSE;
    verbosity           = 0;

    test_kind.bit_len              = 0;
    test_kind.second_bit_len       = 0;
    test_kind.num_key_systems      = 1;
    test_kind.tests_per_key_system = 4;
    test_kind.test_name            = TEST_MOD_EXP;

    if (2 <= argc)
    {
        if (SUCCESS != process_options(argc, argv))
            return -1;
    }
    else
        help = TRUE;

    if (help)
    {
        print_usage();
        return 0;
    }

    // Set the default bit_len (i.e. when not explicitly set on the command
    // line) depending on the kind of test.
    if (test_kind.bit_len == 0)
        test_kind.bit_len = DEFAULT_BIT_LEN[test_kind.test_name];

    if (test_kind.second_bit_len == 0)
    {
        switch (test_kind.test_name)
        {
        case TEST_DIVIDE:
        case TEST_DIV_MOD:
        case TEST_MODULO:
        case TEST_DSA_GEN:
        case TEST_DSA_VERIFY:
        case TEST_DSA_GEN_VERIFY:
            test_kind.second_bit_len = test_kind.bit_len - 1;
            break;

        case TEST_ECDSA_GEN:
        case TEST_ECDSA_VERIFY:
        case TEST_ECDSA_GEN_VERIFY:
            test_kind.second_bit_len = test_kind.bit_len / 2;
            break;

        case TEST_RSA_VERIFY:
            test_kind.second_bit_len = 33;
            break;

        default:
            test_kind.second_bit_len = 0;
            break;
        }
    }

    cpu_freq = tmc_perf_get_cpu_speed();
    printf("cpu_freq=%ld\n", cpu_freq);
    printf("Creating %u %s tests of test kind %s using bit_lens %u and %u\n",
           test_kind.tests_per_key_system,
           big_endian ? "BIG_ENDIAN" : "LITTLE_ENDIAN",
           test_name_to_string(test_kind.test_name), test_kind.bit_len,
           test_kind.second_bit_len);
    printf("Running each test %u times with %u cmds outstanding on %u "
           "threads\n", submits_per_test, cmds_outstanding, num_of_threads);
    printf("Tests %s be checked and thread stats %s be reported. "
           "Verbosity=%u\n", check_results ? "will" : " will not",
           report_thread_stats ? "will" : "will not", verbosity);

    handle = gxcr_pka_open(big_endian);
    if (handle == NULL)
    {
        printf("Failed to open mica pka handle on the main thread\n");
        return -5;
    }

    init_test_utils(handle);
    tmc_spin_barrier_init(&thread_start_barrier, num_of_threads + 1);

    return_code = run_test(handle);
    analyze_results();
    report_results(overall_end_time - overall_start_time, num_of_threads);

    return return_code;
}
