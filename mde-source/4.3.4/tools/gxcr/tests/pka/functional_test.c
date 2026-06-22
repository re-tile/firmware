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

#include <gxcr/pka.h>
#include "pka_test_utils.h"


#define MAX_NUM_TESTS  1000

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


static boolean_t       big_endian;
static boolean_t       help;
static pka_test_kind_t test_kind;

static test_desc_t *test_descs[MAX_NUM_TESTS];
static uint32_t     num_tests;

static uint32_t verbosity = 0;



static status_t run_basic_test(gxcr_pka_handle_t *handle,
                               test_desc_t       *test_desc,
                               uint32_t           test_idx)
{
    pka_test_kind_t *test_kind;
    pka_test_name_t  test_name;
    pka_operand_t   *result, *result2, *answer, *answer2;
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

    answer  = basic->answer;
    answer2 = NULL;
    result2 = NULL;

    switch (test_name)
    {
    case TEST_ADD:
        result = sync_add(handle, basic->first, basic->second);
        break;

    case TEST_SUBTRACT:
        result = sync_subtract(handle, basic->first, basic->second);
        break;

    case TEST_MULTIPLY:
        result = sync_multiply(handle, basic->first, basic->second);
        break;

    case TEST_DIVIDE:
        result = sync_divide(handle, basic->first, basic->second);
        break;

    case TEST_DIV_MOD:
        answer2 = basic->answer2;
        result  = sync_modulo(handle, basic->first, basic->second);
        result2 = sync_divide(handle, basic->first, basic->second);
        break;

    case TEST_MODULO:
        result = sync_modulo(handle, basic->first, basic->second);
        break;

    case TEST_MOD_INVERT:
        result = sync_mod_inverse(handle, basic->first, basic->second);
        break;

    case TEST_SHIFT_LEFT:
        result = sync_shift_left(handle, basic->first, basic->shift_cnt);
        break;

    case TEST_SHIFT_RIGHT:
        result = sync_shift_right(handle, basic->first, basic->shift_cnt);
        break;

    default:
        Assert(FALSE);
    }

    if (3 <= verbosity)
    {
        if (test_name == TEST_DIV_MOD)
        {
            print_pka_operand("result = ", result, "\n");
            print_pka_operand("result2= ", result2, "\n");
            print_pka_operand("answer = ", answer, "\n");
            print_pka_operand("answer2= ", answer2, "\n");
        }
        else
        {
            print_pka_operand("result = ", result, "\n");
            print_pka_operand("answer = ", answer, "\n");
        }
    }

    // Compare result and answer to make sure they agree.
    if (test_name == TEST_DIV_MOD)
    {
        if ((pka_compare(result,  answer)  == PKA_EQUAL) &&
            (pka_compare(result2, answer2) == PKA_EQUAL))
            return SUCCESS;
    }
    else if (pka_compare(result, answer) == PKA_EQUAL)
        return SUCCESS;

    return FAILURE;
}



static status_t run_mod_exp_test(gxcr_pka_handle_t *handle,
                                 test_desc_t       *test_desc,
                                 uint32_t           test_idx)
{
    mod_exp_key_system_t *mod_exp_keys;
    pka_test_kind_t      *test_kind;
    pka_test_name_t       test_name;
    test_mod_exp_t       *test_mod_exp;
    pka_operand_t        *result, *answer;

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
    result = sync_mod_exp(handle, test_mod_exp->exponent, mod_exp_keys->modulus,
                          test_mod_exp->base);
    answer = test_mod_exp->answer;

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



static status_t run_rsa_test(gxcr_pka_handle_t *handle,
                             test_desc_t       *test_desc,
                             uint32_t           test_idx)
{
    rsa_key_system_t *rsa_keys;
    pka_test_kind_t  *test_kind;
    pka_test_name_t   test_name;
    pka_operand_t    *result, *answer;
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

        result = sync_mod_exp(handle, rsa_keys->private_key, rsa_keys->n,
                              test_rsa->msg);
        answer = test_rsa->answer;
        break;

    case TEST_RSA_VERIFY:
        if (3 <= verbosity)
        {
            print_pka_operand("base     = ", test_rsa->msg,        "\n");
            print_pka_operand("exponent = ", rsa_keys->public_key, "\n");
            print_pka_operand("modulus  = ", rsa_keys->n,          "\n");
        }

        result = sync_mod_exp(handle, rsa_keys->public_key, rsa_keys->n,
                              test_rsa->msg);
        answer = test_rsa->answer;
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

        result = sync_exp_with_crt(handle, rsa_keys->p, rsa_keys->q,
                                   test_rsa->msg, rsa_keys->d_p,
                                   rsa_keys->d_q, rsa_keys->qinv);
        answer = test_rsa->answer;
        break;

    default:
        Assert(FALSE);
    }

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



static status_t run_ecc_test(gxcr_pka_handle_t *handle,
                             test_desc_t       *test_desc,
                             uint32_t           test_idx)
{
    ecc_key_system_t *ecc_keys;
    pka_test_kind_t  *test_kind;
    pka_test_name_t   test_name;
    ecc_curve_t      *curve;
    ecc_point_t      *pointA, *result_pt, *answer_pt;
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

        result_pt = sync_ecc_add(handle, curve, pointA, ecc_test->pointB);
        break;

    case TEST_ECC_DOUBLE:
        result_pt = sync_ecc_add(handle, curve, pointA, pointA);
        break;

    case TEST_ECC_MULTIPLY:
        if (3 <= verbosity)
            print_pka_operand("multiplier   = ", ecc_test->multiplier, "\n");

        result_pt = sync_ecc_multiply(handle, curve, pointA,
                                      ecc_test->multiplier);
        break;

    default:
        Assert(FALSE);
    }

    answer_pt = ecc_test->answer;
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



static status_t run_ecdsa_test(gxcr_pka_handle_t *handle,
                               test_desc_t       *test_desc,
                               uint32_t           test_idx)
{
    ecdsa_key_system_t *keys;
    dsa_signature_t    *signature;
    pka_test_kind_t    *test_kind;
    pka_test_name_t     test_name;
    pka_operand_t      *base_pt_order, *hash;
    test_ecdsa_t       *ecdsa_test;
    ecc_curve_t        *curve;
    ecc_point_t        *base_pt;
    status_t            status;

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

    switch (test_name)
    {
    case TEST_ECDSA_GEN:
        if (3 <= verbosity)
        {
            print_pka_operand("private_key   = ", keys->private_key, "\n");
            print_pka_operand("k             = ", ecdsa_test->k,     "\n");
        }

        signature = sync_ecdsa_gen(handle, curve, base_pt, base_pt_order,
                                   keys->private_key, ecdsa_test->hash,
                                   ecdsa_test->k);
        if (3 <= verbosity)
        {
            print_pka_operand("signature->r  = ", signature->r,        "\n");
            print_pka_operand("signature->s  = ", signature->s,        "\n");
        }

        if (signatures_are_equal(signature, ecdsa_test->signature))
            status = SUCCESS;
        else
            status = FAILURE;

        break;

    case TEST_ECDSA_VERIFY:
        if (3 <= verbosity)
        {
            print_pka_operand("public_key->x = ", keys->public_key->x, "\n");
            print_pka_operand("public_key->y = ", keys->public_key->y, "\n");
            print_pka_operand("signature->r  = ", signature->r,        "\n");
            print_pka_operand("signature->s  = ", signature->s,        "\n");
        }

        status = sync_ecdsa_verify(handle, curve, base_pt, base_pt_order,
                                   keys->public_key, hash,
                                   ecdsa_test->signature);
        break;

    case TEST_ECDSA_GEN_VERIFY:
        if (3 <= verbosity)
        {
            print_pka_operand("private_key   = ", keys->private_key,   "\n");
            print_pka_operand("k             = ", ecdsa_test->k,       "\n");
            print_pka_operand("public_key->x = ", keys->public_key->x, "\n");
            print_pka_operand("public_key->y = ", keys->public_key->y, "\n");
            print_pka_operand("signature->r  = ", signature->r,        "\n");
            print_pka_operand("signature->s  = ", signature->s,        "\n");
        }

        signature = sync_ecdsa_gen(handle, curve, base_pt, base_pt_order,
                                   keys->private_key, hash, ecdsa_test->k);
        status = sync_ecdsa_verify(handle, curve, base_pt, base_pt_order,
                                   keys->public_key, hash,
                                   ecdsa_test->signature);
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



static status_t run_dsa_test(gxcr_pka_handle_t *handle,
                             test_desc_t       *test_desc,
                             uint32_t           test_idx)
{
    dsa_key_system_t *keys;
    pka_test_kind_t  *test_kind;
    pka_test_name_t   test_name;
    dsa_signature_t  *signature;
    test_dsa_t       *dsa_test;
    status_t          status;

    test_kind = (pka_test_kind_t  *) test_desc->test_kind;
    keys      = (dsa_key_system_t *) test_desc->key_system;
    dsa_test  = (test_dsa_t       *) test_desc->test_operands;
    signature = dsa_test->signature;
    test_name = test_kind->test_name;

    if (3 <= verbosity)
    {
        printf("\nRunning test_idx=%u test_name=%s\n", test_idx,
               test_name_to_string(test_name));
        print_pka_operand("prime        = ", keys->p,        "\n");
        print_pka_operand("q            = ", keys->q,        "\n");
        print_pka_operand("g            = ", keys->g,        "\n");
        print_pka_operand("hash         = ", dsa_test->hash, "\n");
    }

    switch (test_kind->test_name)
    {
    case TEST_DSA_GEN:
        if (3 <= verbosity)
        {
            print_pka_operand("private_key  = ", keys->private_key, "\n");
            print_pka_operand("k            = ", dsa_test->k,       "\n");
        }

        signature = sync_dsa_gen(handle, keys->p, keys->q, keys->g,
                                 keys->private_key, dsa_test->hash,
                                 dsa_test->k);
        if (3 <= verbosity)
        {
            print_pka_operand("signature->r = ", signature->r, "\n");
            print_pka_operand("signature->s = ", signature->s, "\n");
        }

        if (signatures_are_equal(signature, dsa_test->signature))
            status = SUCCESS;
        else
            status = FAILURE;

        break;

    case TEST_DSA_VERIFY:
        if (3 <= verbosity)
        {
            print_pka_operand("public_key   = ", keys->public_key, "\n");
            print_pka_operand("signature->r = ", signature->r,     "\n");
            print_pka_operand("signature->s = ", signature->s,     "\n");
        }

        status = sync_dsa_verify(handle, keys->p, keys->q, keys->g,
                                 keys->public_key, dsa_test->hash,
                                 dsa_test->signature);
        break;

    case TEST_DSA_GEN_VERIFY:
        if (3 <= verbosity)
        {
            print_pka_operand("private_key  = ", keys->private_key, "\n");
            print_pka_operand("k            = ", dsa_test->k,       "\n");
            print_pka_operand("public_key   = ", keys->public_key,  "\n");
        }

        signature = sync_dsa_gen(handle, keys->p, keys->q, keys->g,
                                 keys->private_key, dsa_test->hash,
                                 dsa_test->k);
        if (3 <= verbosity)
        {
            print_pka_operand("signature->r = ", signature->r, "\n");
            print_pka_operand("signature->s = ", signature->s,  "\n");
        }

        status = sync_dsa_verify(handle, keys->p, keys->q, keys->g,
                                 keys->public_key, dsa_test->hash,
                                 dsa_test->signature);
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



static status_t run_test(gxcr_pka_handle_t *handle,
                         test_desc_t       *test_desc,
                         uint32_t           test_idx)
{
    pka_test_kind_t *test_kind;

    Assert(test_desc->test_category == PKA_TEST);
    test_kind = (pka_test_kind_t *) test_desc->test_kind;

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
        return run_basic_test(handle, test_desc, test_idx);

    case TEST_MOD_EXP:
        return run_mod_exp_test(handle, test_desc, test_idx);

    case TEST_RSA_MOD_EXP:
    case TEST_RSA_VERIFY:
    case TEST_RSA_MOD_EXP_WITH_CRT:
        return run_rsa_test(handle, test_desc, test_idx);

    case TEST_ECC_ADD:
    case TEST_ECC_DOUBLE:
    case TEST_ECC_MULTIPLY:
        return run_ecc_test(handle, test_desc, test_idx);

    case TEST_ECDSA_GEN:
    case TEST_ECDSA_VERIFY:
    case TEST_ECDSA_GEN_VERIFY:
        return run_ecdsa_test(handle, test_desc, test_idx);

    case TEST_DSA_GEN:
    case TEST_DSA_VERIFY:
    case TEST_DSA_GEN_VERIFY:
        return run_dsa_test(handle, test_desc, test_idx);

    default:
        Assert(FALSE);
        return FAILURE;
    }
}



static status_t create_tests(gxcr_pka_handle_t *handle)
{
    free_test_descs(test_descs);
    if (SUCCESS != chk_bit_lens(&test_kind))
        return FAILURE;

    num_tests = test_kind.num_key_systems * test_kind.tests_per_key_system;
    return create_pka_test_descs(handle, &test_kind, test_descs, TRUE,
                                 verbosity);
}

static uint32_t run_tests(gxcr_pka_handle_t *handle)
{
    uint32_t test_idx, errors = 0;

    for (test_idx = 0;  test_idx < num_tests;  test_idx++)
        if (run_test(handle, test_descs[test_idx], test_idx) == FAILURE)
            errors++;

    return errors;
}

static uint32_t execute_test(gxcr_pka_handle_t *handle)
{
    status_t status;
    uint32_t errors;

    status = create_tests(handle);
    if (status != SUCCESS)
    {
        printf("Failure creating the tests\n");
        return 0;
    }

    errors = run_tests(handle);
    printf("\nRan %u tests with %u errors (bit_len=%u",
           num_tests, errors, test_kind.bit_len);
    if (test_kind.second_bit_len != 0)
        printf(" second_bit_len=%u", test_kind.second_bit_len);

    printf(")\n");
    return errors;
}



static void print_usage(void)
{
    printf("'./functional_test <options>' - where options can be:\n");
    printf("  -b <bit_len>         primary bit_len to use\n");
    printf("  -e ( big | little )  endianness of the interface\n");
    printf("  -h                   print this message and exit\n");
    printf("  -k <num_keys>        num of different key subsystems to make\n");
    printf("  -n <num_tests>       num of tests (per key subsystem) to make\n");
    printf("  -s <second_bit_len>  secondary bit_len for some cryptoSystems\n");
    printf("  -v <verbosity>       verbosity level - in range 0-3\n");
    printf("  -c <test_kind>       name of the test kind.  One of:\n");
    printf("     ADD, SUBTRACT, MULTIPLY, DIVIDE, DIV_MOD, MODULO\n");
    printf("     SHIFT_LEFT, SHIFT_RIGHT, MOD_INVERT\n");
    printf("     MOD_EXP, RSA_MOD_EXP, RSA_VERIFY, RSA_MOD_EXP_WITH_CRT\n");
    printf("     ECC_ADD, ECC_DOUBLE, ECC_MULTIPLY\n");
    printf("     ECDSA_GEN, ECDSA_VERIFY, ECDSA_GEN_VERIFY\n");
    printf("     DSA_GEN, DSA_VERIFY, DSA_GEN_VERIFY\n\n");
    printf("The default command line options (except for -b and -s) are:\n");
    printf("  '-c MOD_EXP -e big -k 1 -n 10 -v 0'\n");
    printf("The defaults for '-b' and '-s' depend upon the test name (as \n");
    printf("given by '-c') as follows:\n");
    printf("a) the default for '-b' is 1024 for all tests except\n");
    printf("   for the ECC_* tests and ECDSA_* tests when it is 256.\n");
    printf("b) the default for -s is 33 for RSA_VERIFY, 'bit_len - 1'\n");
    printf("   for DIVIDE, DIV_MOD, MODULO, and DSA_* tests, 'bit_len / 2'\n");
    printf("   for the ECDSA_* tests and unused for for all other tests.\n\n");
}

static void process_options(int argc, char *argv[])
{
    pka_test_name_t test_name;
    uint32_t        num_tests, bit_len, key_systems;
    int             optionChar;

    while ((optionChar = getopt(argc, argv, "b:c:e:hk:n:s:v:")) != -1)
    {
        switch (optionChar)
        {
        case 'b':
            bit_len = atoi(optarg);
            if (bit_len <= 32)
                printf("primary key bit_len must be >= 33 bits\n");
            else if (4096 < bit_len)
                printf("primary key bit_len must be <= 4096 bits\n");
            else
                test_kind.bit_len = bit_len;
            break;

        case 'c':
            test_name = lookup_test_name(optarg);
            if (test_name == TEST_NOP)
                printf("Option -c needs to be followed by legal test_name\n");
            else
                test_kind.test_name = test_name;
            break;

        case 'e':
            if (strcasecmp(optarg, "big") == 0)
                big_endian = TRUE;
            else if (strcasecmp(optarg, "little") == 0)
                big_endian = FALSE;
            else
                printf("Option -e needs to be followed by either"
                       " 'big' or 'little'\n");
            break;

        case 'h':
            help = TRUE;
            break;

        case 'k':
            key_systems = atoi(optarg);
            if (key_systems != 1)
                printf("currenly the num of key systems can only be 1\n");
            else
                test_kind.num_key_systems = key_systems;
            break;

        case 'n':
            num_tests = atoi(optarg);
            if (num_tests == 0)
                printf("num of tests per key subsystem cannot be 0\n");
            else if (MAX_NUM_TESTS < num_tests)
                printf("num of tests per key subsystem must be <= %u\n",
                       MAX_NUM_TESTS);
            else
                test_kind.tests_per_key_system = num_tests;
            break;

        case 's':
            bit_len = atoi(optarg);
            if (bit_len <= 8)
                printf("secondary key bit_len must be >= 9 bits\n");
            else if (4095 < bit_len)
                printf("secondary key bit_len must be <= 4095 bits\n");
            else
                test_kind.second_bit_len = bit_len;
            break;

        case 'v':
            verbosity = atoi(optarg);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    gxcr_pka_handle_t *handle;
    int                return_code = 0;

    // Set argument defaults:
    big_endian  = TRUE;
    help        = FALSE;
    verbosity   = 0;

    test_kind.bit_len              = 0;
    test_kind.second_bit_len       = 0;
    test_kind.num_key_systems      = 1;
    test_kind.tests_per_key_system = 10;
    test_kind.test_name            = TEST_MOD_EXP;

    if (2 <= argc)
        process_options(argc, argv);
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

    if (test_byte_swap_cpy(256) == 0)
    {
        printf("Byte Swap test failed.\n");
        return 2;
    }
    else if (verbosity != 0)
        printf("Byte Swap test PASSED.\n");

    handle = gxcr_pka_open(big_endian);
    if (handle == NULL)
    {
        printf("Failed to open mica pka handle on the main thread\n");
        return 5;
    }

    init_test_utils(handle);
    return_code = execute_test(handle);

    return return_code;
}
