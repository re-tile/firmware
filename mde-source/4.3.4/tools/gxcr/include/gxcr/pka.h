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


#ifndef _PKA_H_
#define _PKA_H_

///
/// @file
///
/// An API for interfacing to the Tilera MiCA Public Key Hardware
///
/// @addtogroup gxcr_pka_
/// @{
///
/// An API for interfacing to the Tilera MiCA Public Key Hardware.
///
/// This file forms an interface to the User Mode MiCA PKA driver.
///
/// The MiCA PKA hardware makes available a number of arithmetic
/// (both basic ops (like add and multipy) as well as complex ops (like modular
/// exponentiation and DSA).  It can be used for RSA, Diffie-Hallman,
/// Elliptic Curve Cryptology (odd prime number not the binary version_, and
/// the Federal Digital Signature Algorithm (DSA as documented in FIPS 186)
/// public-private key systems/
///
/// Note that most of the functions in this file are asynchronous - they create
/// and queue a request, and then the user must poll for results.  Consequently
/// most requests include an opaque value - called user_data - that can be
/// obtained when the operation is completed.  In particular ALL asynchronous
/// functions in this API have a user_data parameter, and no synchronous
/// functions have such a parameter except 'gxcr_pka_result_info'.
///
/// The user_data value can then be used to associate a result with the
/// original request.  This is especially important because results can be
/// returned in a different order than the order of the requests.  So unless
/// the user restricts themselves to only one outstanding request at a time
/// (effectively making the request synchronous) they will need to use the
/// 'user_data' parameter.
///
/// Almost all functions in this API require a 'handle' argument to be passed
/// to them.  A handle is created and initialized via the gxcr_pka_open
/// function and freed via the gxcr_pka_close function.  A handle logically
/// represents a single request and single result queue to and from the MiCA
/// PKA hardware.  But because the PKA hardware has multiple parallel 'engines'
/// (even within a single MiCA shim) going from the single request queue to
/// the single result queue, one can still experience reordering.
///
/// Typically a unique handle will be opened for each thread in the user's
/// program, but this is not required.  If multiple threads are going to use
/// the same handle however, the user will need to use some mutal exclusion
/// technique of their own to guarantee that only one thread uses a given handle
/// with the functions in this module at a time, since currently there are no
/// internal locks.
///
/// For example to use this module to do a single RSA encryption, one could do
/// the following:
///
/// @code
/// // 64-bit RSA encryption example.
///
/// gxcr_pka_handle_t handle;
/// pka_operand_t     encrypt_key, n, msg;
/// pka_results_t     results;
/// uint8_t           result_buf[8];
/// char             *key_string     = "0x633649F8F2228670";
/// char             *modulus_string = "0x87C1F8442909789F";
/// char             *plaintext      = "0x4869207468657265"; // hex "Hi there"
/// char              ciphertext[20];
///
/// memset(&encrypt_key, 0, sizeof(pka_operand_t));
/// memset(&n,           0, sizeof(pka_operand_t));
/// memset(&msg,         0, sizeof(pka_operand_t));
/// memset(&results,     0, sizeof(pka_results_t));
/// results.results[0].buf_ptr = result_buf;
/// results.results[0].buf_len = sizeof(result_buf);
///
/// gxcr_pka_from_hex_string(key_string,     &encrypt_key);
/// gxcr_pka_from_hex_string(modulus_string, &n);
/// gxcr_pka_from_hex_string(plaintext,      &msg);
///
/// gxcr_pka_open(&handle, 1);  // Use big-endian
/// gxcr_pka_mod_exp(&handle, NULL, &encrypt_key, &n, &msg);
/// gxcr_pka_get_results(&handle, &results);
/// gxcr_pka_close(&handle);
///
/// gxcr_pka_to_hex_string(&results.results[0], ciphertext, 20);
/// printf("plaintext='%s'\nciphertext='%s'\n", plaintext, ciphertext);
///
/// // ciphertext should be '9F5E3B6F177E25B6'
/// @endcode
///

#include <stdint.h>

/// MAX_OPERAND_CNT defines the largest number of big integer operands used by
/// any operation in this API.
#define MAX_OPERAND_CNT  11

/// MAX_RESULT_CNT defines the largest number of big integers returned by any
/// operation in this API.  In particular, a given asynchronous request
/// function always returns the same number of result big integers, but
/// depending on the operation this can be either 0, 1 or 2 big integers.
#define MAX_RESULT_CNT   2


/// \cond
typedef enum
{
    PKA_ADD,           // value + addend               -> result.
    PKA_SUBTRACT,      // value - subtrahend           -> result.
    PKA_MULTIPLY,      // value * multiplier           -> result.
    PKA_DIVIDE,        // value / divisor              -> result.
    PKA_MODULO,        // value mod modulus            -> result.
    PKA_SHIFT_LEFT,    // value << shift_amount        -> result
    PKA_SHIFT_RIGHT,   // value >> shift_amount        -> result
    PKA_COMPARE,       // value <> comparend           -> compare_result
    PKA_MOD_EXP,       // value^exponent mod modulus   -> result

    // c^d mod p*q -> result, which is done faster using the precalculated
    // values q_inv = q^(-1) mod p, d_p = d mod p-1, d_q = d mod q-1 and then
    // doing m1 = c^d_p mod p, m2 = c^d_q mod q, h = q_inv * (m1 - m2) mod p,
    // and finally m2 + h * q -> result where p=operand1, q=operand2,
    // c=operand3, d_p=operand4, d_q=operand5, q_inv=operand6.
    PKA_EXP_WITH_CRT,

    PKA_MOD_INVERT,    // value^-1 mod modulus -> result

    // pointA + pointB -> pointC on elliptic curve "y^2 = x^3 + a*x + b mod p"
    // where pointA.x=operand1, pointA.y=operand2, pointB.x=operand3,
    // pointB.y=operand4, p=operand5, a=operand6 and b=operand7  The two
    // results are pointC.x=result1 and pointC.y=result2.
    PKA_ECC_ADD,

    // k * pointA -> pointC on elliptic curve "y^2 = x^3 + a*x + b mod p"
    // where k=operand1, pointA.x=operand2, pointA.y=operand3, p=operand4,
    // a=operand5, and b=operand6.  The two results are pointC.x=result1 and
    // pointC.y=result2.
    PKA_ECC_MULTIPLY,

    // ECDSA_GEN(base_point, k, alpha, h) -> r,s using elliptic curve
    // "y^2 = x^3 + ax+ b mod p", where PointA.x=operand1, PointA.y=operand2,
    // k=operand3, alpha=operand4, h=operand5, p=operand6, a=operand7 and
    // b=operand8.  The two results are r=result1 and s=result2.
    PKA_ECDSA_GEN,

    // ECDSA_VERIFY(base_point, public_point, h, r, s) -> compare_results.
    // r using elliptic curve "y^2 = x^3 + ax+ b mod p", where
    // basePointA.x=operand1, basePointA.y=operand2, publicPoint.x=operand3,
    // publicPoint.y=operand4, h=operand5, r_prime=operand6, s_prime=operand7,
    // p=operand8, a=operand9, b=operand10, n=operand11.
    PKA_ECDSA_VERIFY,

    // DSA_GEN(p, g, n, k, alpha, h) -> r,s.  Here p=operand1,
    // n=operand2, g=operand3, k=operand4, alpha=operand5, h=operand6,
    // n=operand7.  The two results are r=result1 and s=result2.
    PKA_DSA_GEN,

    // DSA_VERIFY(p, g, n, h, r, s) -> compare_results.
    PKA_DSA_VERIFY
} pka_opcode_t;
/// \endcond

/// The MAX_BYTE_LEN constant defines the byte length of the largest operand
/// that the current PKA hardware supports.
#define MAX_BYTE_LEN        (130 * 4)

/// The OTHER_MAX_BYTE_LEN constant defines the byte length of the largest
/// operand supported for certain operations like "modular exponentiation using
/// the chinese remainder theorem":.
#define OTHER_MAX_BYTE_LEN  (66 * 4)


/// gxcr_pka Error codes.
/// This enumeration lists the error codes returned by the main API functions.
typedef enum
{
    PKA_NO_ERROR                =     0,  ///< Successful return code.
    PKA_OPERAND_MISSING         = -1500,  ///< operand missing
    PKA_OPERAND_BUF_MISSING     = -1501,  ///< operand buf is NULL
    PKA_OPERAND_LEN_ZERO        = -1502,  ///< operand len is 0
    PKA_OPERAND_LEN_TOO_SHORT   = -1503,  ///< operand len is too short for op
    PKA_OPERAND_LEN_TOO_LONG    = -1504,  ///< operand len is too long for op
    PKA_OPERAND_LEN_A_LT_LEN_B  = -1505,  ///< operand ordering error
    PKA_OPERAND_VAL_GE_MODULUS  = -1506,  ///< value operand is >= modulus
    PKA_OPERAND_Q_GE_OPERAND_P  = -1507,  ///< q operand is >= p operand
    PKA_OPERAND_MODULUS_IS_EVEN = -1508,  ///< modulus must be odd for this op
    PKA_RESULT_MUST_BE_POSITIVE = -1509,  ///< all result big integers >= 0
    PKA_OPERAND_FIFO_FULL       = -1510,  ///< operand request fifo full
    PKA_CMD_RING_FULL           = -1511,  ///< cmd request fifo full
    PKA_DRIVER_TOO_BUSY         = -1512,  ///< PKA driver backlog too large
    PKA_BAD_OPERAND_CNT         = -1513,  ///< wrong operand_cnt for cmd
    PKA_TRY_GET_RESULTS_FAILED  = -1514,  ///< try found result fifo empty
    PKA_TRY_GET_RANDOM_FAILED   = -1515,  ///< random number fifo empty
    PKA_RESULT_BUF_NULL         = -1516,  ///< result buf ptr is NULL
    PKA_RESULT_BUF_TOO_SMALL    = -1517,  ///< result buf_len too small
    PKA_BAD_RESULT_IDX          = -1518,  ///< bad rsult_idx
    PKA_RESULT_FIFO_EMPTY       = -1519   ///< result fifo empty
} gxcr_pka_err_t;

/// The pka_comparison_t enumeration is the result type for any comparisons.
typedef enum
{
    PKA_NO_COMPARE, PKA_LESS_THAN, PKA_EQUAL, PKA_GREATER_THAN
} pka_comparison_t;


/// The pka_operand_t is the record type used to represent big integer numbers.
/// Despite its name, it is used to represent all big integers including the
/// results.
typedef struct  // 16 bytes long
{
    uint8_t *buf_ptr;       ///< Pointer to the buffer holding the big integer.
    uint16_t buf_len;       ///< Size of the buffer holding the big integer.
    uint16_t actual_len;    ///< Actual minimum # of bytes used by the operand.
    uint8_t  is_encrypted;  ///< Reserved for future use.
    uint8_t  big_endian;    ///< Indicates byte order of the big integer operand
    uint8_t  internal_use;  ///< Internal use.  Must be set to 0 by users.
    uint8_t  pad;           ///< Reserved for future use.
} pka_operand_t;

/// The pka_operands_t record type is used to package the entire set of
/// input operands (big integers) of a single crypto operation.
typedef struct  // 4 + (16 * 11) = 180 bytes long
{
    uint8_t       operand_cnt;                ///< Number of valid operands.
    uint8_t       shift_amount;               ///< Holds the shift amount arg.
    uint8_t       encrypt_results[2];         ///< Reserved for future use.
    pka_operand_t operands[MAX_OPERAND_CNT];  ///< Actual operand descriptors.
} pka_operands_t;

/// The pka_results_t record type is used to package up the entire set of
/// output operands (of which there can be 0, 1 or 2) as well as other
/// result values like the compare_result (only used for compare op), the
/// result_cnt and the overall status.
typedef struct  // 8 + 4 + (16 * 2) = 44 bytes long
{
    void            *user_data;       ///< Same opaque user_data ptr passed in.
    uint8_t          opcode;          ///< Opcode of the associated request.
    uint8_t          result_cnt;      ///< Result cnt must be 0, 1 or 2.
    uint8_t          status;          ///< Same as result_code.
    pka_comparison_t compare_result;  ///< Result of a comparison.
    pka_operand_t    results[MAX_RESULT_CNT]; ///< Actual result operand descs.
} pka_results_t;

/// The ecc_curve_t record type is used to hold all of the parameters defining
/// an elliptic curve over a large prime number finite field.  The prime used
/// as the modulus is called 'p'.  The parameters of the general curve are
/// called 'a' and 'b'.  The formula defining the curve is:
/// @code
/// // The curve is defined as all possible (x,y) values such that
/// // x,y are integers in the range 0..p-1 (where p must be an odd prime).
/// // and the x,y values also satisfy:
/// y^2 mod p = (x^3 + a*x + b) mod p
/// @endcode
typedef struct
{
    pka_operand_t *p;  ///< large integer prime defining the finite field
    pka_operand_t *a;  ///< coefficient of x in the defining eqn
    pka_operand_t *b;  ///< constant coefficient in defining eqn
} ecc_curve_t;

/// The ecc_point_t record type is used to represent a point on an elliptic
/// curve defined over a finite field.
typedef struct
{
    pka_operand_t *x;  ///< big integer x coordinate of a point on a EC curve
    pka_operand_t *y;  ///< big integer y coordinate of a point on a EC curve
} ecc_point_t;

/// The dsa_signature record type is used to package up the two large integer
/// values (called 'r' and 's' in the DSA standard) into a DSA signature.
typedef struct
{
    pka_operand_t *r;  ///< big integer value called 'r' in the standard
    pka_operand_t *s;  ///< big integer value called 's' in the standard
} dsa_signature_t;


/// \cond Type is opaque.
typedef struct gxcr_pka_handle_s gxcr_pka_handle_t;
/// \endcond

/// Initialize a PKA handle.
/// @param  use_big_endian  When non-zero, then all operands and results use
///                         big endian byte order (i.e. the most significant
///                         bytes come first).  Otherwise when zero the
///                         operands use little endian byte order (i.e. the
///                         least significant bytes have the lowest address).
/// @return                 Returns a new gxcr_pka_handle_t pointer (aka handle)
///                         upon success, returns NULL upon failure.
gxcr_pka_handle_t* gxcr_pka_open(uint8_t use_big_endian);

/// Close a PKA handle.
/// @param  handle          A pointer to an intialized PKA handle object.
/// @return                 Zero if successful, an error code otherwise.
int gxcr_pka_close(gxcr_pka_handle_t *handle);

/// Get the Handle's use_big_endian  value.
///
/// Get the value of use_big_endian used when this handle was opened.
/// @param  handle          A pointer to an intialized PKA handle object.
/// @return                 Returns zero for little endian and 1 for big endian.
uint8_t gxcr_pka_get_endian(gxcr_pka_handle_t *handle);

/// Enable Request Tracing.
///
/// The enable_driver_tracing function is used to trace a set of requests to see
/// how they are being implement by the User-Level PKA driver.  The output from
/// this trace request goes into the general pka-driver.log.
/// Tracing starts with the next request that is submitted and ends when
/// disable_driver_tracing or when the supplied max_num_requests has been
/// submitted.
/// @param handle       A pointer to an intialized PKA handle object.
/// @param max_num_requests
void enable_driver_tracing(gxcr_pka_handle_t *handle,
                           uint32_t           max_num_requests);

/// Disable Request Tracing.
///
/// The disable_driver_tracing function turns off tracing for this handle, if
/// it was enable.  If tracing was already disabled then calling this again
/// does nothing.
/// @param handle       A pointer to an intialized PKA handle object.
void disable_driver_tracing(gxcr_pka_handle_t *handle);

/// Fast Combined Byte Swap and Copy.
///
/// This function can be used to convert a big-endian big integer operand into
/// a little-endian version and vice versa.
/// @param dst_ptr   A pointer to the destination buffer.
/// @param src_ptr   A pointer the the source operand's buffer.
/// @param copy_len  The actual size of the source operand in bytes.
void byte_swap_cpy(void *dst_ptr, void *src_ptr, uint32_t copy_len);

/// Convert Hex ASCII String into a Big Integer.
///
/// This function can be used to take a big integer represented as hexadecimal
/// ascii strings and convert it into the pkt_operand_t format need to be this
/// module.  The hex_string should start with 0x and be null terminated.
/// Note if value's buf_ptr is NULL, this function will malloc a buffer for it.
/// @param hex_string  A pointer to a NUL terminated hexadecimal string.
/// @param value       A pointer to a pka_operand_t descriptor that is to be
///                    filled in.
/// @return            Returns zero on success, -1 on failure.
int gxcr_pka_from_hex_string(char *hex_string, pka_operand_t *value);

/// Convert a Big Integer into a HEX ASCII String.
///
/// This function can be used to take a big integer in the pkt_operand_t format
/// and convert it into a hexadecimal string.  The converted string is always
/// NUL terminated, and so the buf_len must be long enough to hold this extra
/// byte.  The string will always be a hex string with leading 0x characters.
/// @param value       A pointer to a big integer.
/// @param string_buf  A pointer to a buffer used to hold the hexadecimal string
/// @param buf_len     The length of the destination buffer in bytes.
/// @return            Returns zero on success, -1 on failure.
int gxcr_pka_to_hex_string(pka_operand_t *value,
                           char          *string_buf,
                           uint32_t       buf_len);

/// Faster Software Comparison.
///
/// Instead of using the HW comparison function, it is usually faster and more
/// convenient to instead call this function.  This is a synchronous/blocking
/// function.
/// @param value        One of the two big integers that is to be compared.
/// @param comparend    The other big integer that is to be compared.
/// @return             Returns value of enum type pka_comparison_t.
pka_comparison_t pka_compare(pka_operand_t *value, pka_operand_t *comparend);

/// Get Operand Byte Length.
///
/// Utility function used to get the actual byte length of a given big integer
/// operand.  This is a synchronous/blocking function.
/// @param value  A pointer to the pka_operand_t descriptor for a big integer.
/// @return       The length of the big integer in bytes.
uint32_t pka_operand_byte_len(pka_operand_t *value);

/// Get Operand Bit Length.
///
/// Utility function used to get the actual bit length of a given big integer
/// operand.  This is a synchronous/blocking function.
/// @param value  A pointer to the pka_operand_t descriptor for a big integer.
/// @return       The length of the big integer in bytes * 8.
uint32_t pka_operand_bit_len(pka_operand_t *value);


/// Big Integer Add function.
///
/// This function computes <B>value + addend</B>.
/// @param handle       An initialized PKA handle to use for this request.
/// @param user_data    Opaque user pointer that is returned with the result.
/// @param value        One of the two big integers whose sum is requested.
/// @param addend       The other of the two big integers whose sum is
///                     requested.
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_add(gxcr_pka_handle_t *handle,
                            void              *user_data,
                            pka_operand_t     *value,
                            pka_operand_t     *addend);

/// Big Integer Subtraction function.
///
/// His functions computes <B>value - subtrahend</B>.
/// *NOTE that this function must yield a positive result and so the value
/// MUST be >= than the subtrahend.*
/// @param handle       An initialized PKA handle to use for this request.
/// @param user_data    Opaque user pointer that is returned with the result.
/// @param value        The LARGER of the two big integers whose difference is
///                     requested.
/// @param subtrahend   The SMALLER of the two big integers whose difference is
///                     requested.
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_subtract(gxcr_pka_handle_t *handle,
                                 void              *user_data,
                                 pka_operand_t     *value,
                                 pka_operand_t     *subtrahend);

/// Big Integer Multiply function.
///
/// This function computes <B>value * multiplier</B>.
/// @param handle       An initialized PKA handle to use for this request.
/// @param user_data    Opaque user pointer that is returned with the result.
/// @param value        One of the two big integers whose product is requested.
/// @param multiplier   The other of the two big integers whose product is
///                     requested.
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_multiply(gxcr_pka_handle_t *handle,
                                 void              *user_data,
                                 pka_operand_t     *value,
                                 pka_operand_t     *multiplier);

/// Big Integer Divide function.
///
/// This function computes BOTH <B>value / divisor</B>, and
/// <B>value mod divisor</B>.  Note the the final results returned will always
/// have result_cnt = 2.
/// @param handle       An initialized PKA handle to use for this request.
/// @param user_data    Opaque user pointer that is returned with the result.
/// @param value        The big integer whose quotient and remainder is desired.
/// @param divisor      The big integer divisor.  Must not be zero.
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_divide(gxcr_pka_handle_t *handle,
                               void              *user_data,
                               pka_operand_t     *value,
                               pka_operand_t     *divisor);

/// Big Integer Mod function.
/// The function implements the mod function on big integers, i.e. computes
/// <B>value mod modulus</B>.  Note that the modulus must be odd and be
/// larger than 2^32.
/// @param handle       An initialized PKA handle to use for this request.
/// @param user_data    Opaque user pointer that is returned with the result.
/// @param value        The big integer whose mod is desired.
/// @param modulus      The big integer modulus.  Must be odd.
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_modulo(gxcr_pka_handle_t *handle,
                               void              *user_data,
                               pka_operand_t     *value,
                               pka_operand_t     *modulus);

/// Big Integer Shift Left function.
///
/// Computes <B>'value << shift_amount'</B>.
/// @param handle       An initialized PKA handle to use for this request.
/// @param user_data    Opaque user pointer that is returned with the result.
/// @param value        The big integer that is to be shifted left.
/// @param shift_amount The amount that the big integer 'value' is to be shifted
///                     by.  MUST be in the range 1..32?
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_shift_left(gxcr_pka_handle_t *handle,
                                   void              *user_data,
                                   pka_operand_t     *value,
                                   uint32_t           shift_amount);

/// Big Integer Shift Right function.
///
/// Computes <B>'value >> shift_amount'</B>.
/// @param handle       An initialized PKA handle to use for this request.
/// @param user_data    Opaque user pointer that is returned with the result.
/// @param value        The big integer that is to be shifted right.
/// @param shift_amount The amount that the big integer 'value' is to be shifted
///                     by.  MUST be in the range 1..32?
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_shift_right(gxcr_pka_handle_t *handle,
                                    void              *user_data,
                                    pka_operand_t     *value,
                                    uint32_t           shift_amount);

/// Big Integer Comparison.
///
/// This function compares the big integer value with the big integer comparend
/// and returns the result of the comparison in the compare_result field of
/// the pka_results_t.
/// @param handle       An initialized PKA handle to use for this request.
/// @param user_data    Opaque user pointer that is returned with the result.
/// @param value        One of the two big integers that is to be compared.
/// @param comparend    The other big integer that is to be compared.
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_compare(gxcr_pka_handle_t *handle,
                                void              *user_data,
                                pka_operand_t     *value,
                                pka_operand_t     *comparend);

/// Modular Exponentiation function.
///
/// This function implements the mathematical
/// expression <B>'value<SUP>exponent</SUP> mod modulus'</B>.
/// It is the basis for both RSA encryption and decryption as well as the
/// Diffie_Hellman key distribution scheme.  For example RSA is defined by:
/// @code
/// // Pick two large distinct random prime numbers p and q.
/// // Pick a random integer e such that 1 < e < (p-1)*(q-1) AND
/// // gcd(e, (p-1)*(q-1)) = 1.
/// n = p * q;
/// d = (e^-1) mod ((p-1)*(q-1));
/// // I.e. find d such that `(e * d) mod ((p-1)*(q-1)) = 1`.
/// // Then publish (e, n) as the public key.  (d, n) is the private key.
/// @endcode
/// where the encryption and decryption operations are defined as:
/// @code
/// ciphertext = (plaintext^e)  mod n;  // encryption
/// plaintext  = (ciphertext^e) mod n;  // decryption
/// @endcode
/// This function returns once the requested mathematic operation has been
/// queued - not when the operation has completed.
/// @param handle     An initialized PKA handle to use for this request.
/// @param user_data  Opaque user pointer that is returned with the result.
/// @param exponent   The big integer exponent.  For RSA, this exponent could
///                   be either the e or d part of the public or private key.
/// @param modulus    The big integer modulus.  Must be odd.  Note that when
///                   used by RSA, this will NOT be a prime number, but instead
///                   will be the product of two big prime numbers.
/// @param value      The big integer whose modular power is requested.
/// @return           Returns 0 on success, or one of the gxcr_pka error codes
///                   on failure.
gxcr_pka_err_t gxcr_pka_mod_exp(gxcr_pka_handle_t *handle,
                                void              *user_data,
                                pka_operand_t     *exponent,
                                pka_operand_t     *modulus,
                                pka_operand_t     *value);

/// Optimized Modular Exponentiation function for RSA.
///
/// The gxcr_pka_exp_with_crt makes use of the Chinese Remainder Theorem in
/// order to implement a full RSA modular exponentiation using two smaller
/// (half-sized) modular exponentiation, plus a big integer subtraction,
/// multiplication and addition.  Since
/// the performance of the modular exponentiation is very roughly cubic in
/// the size of the operands, each smaller modular exponentation runs roughly
/// 4 times faster (but there are now twice as many of them), and in total can
/// give a 3x speed up.
/// Note that this function is only useful to the side that has both the
/// public AND private RSA keys, since it requires as input some of the
/// intermediate values used when creating a RSA public-private key pair.
/// In addition, its is assumed that the following values have been precomputed:
/// @code
/// d_p  = d mod (p-1);
/// d_q  = d mod (q-1);
/// qinv = q^-1 mod p;   // I.e. Find qinv such that '(qinv * q) mod p = 1'
/// @endcode
/// Specifically this function implements the mathematical expression
/// <B>'c<SUP>d</SUP> mod p*q'</B> by doing the following computation:
/// @code
/// m1 = (c^d_p) mod p;
/// m2 = (c^d_q) mod q;
/// h  = (q_inv * (m1 - m2)) mod p;
/// return m2 + (h * q);
/// @endcode
/// @param handle     An initialized PKA handle to use for this request.
/// @param user_data  Opaque user pointer that is returned with the result.
/// @param p          A big integer prime number.  RSA modulus = `p*q`.  Note
///                   p MUST be larger than q.
/// @param q          A big integer prime number.  RSA modulus = `p*q`.  Note
///                   q MUST be smaller than p.
/// @param c          A big integer representing the msg to be decrypted.
/// @param d_p        The big integer value `d mod (p-1)` where d is the private
///                   key.
/// @param d_q        The big integer value `d mod (q-1)` where d is the private
///                   key.
/// @param qinv       The big integer value `q^-1 mod p` - i.e. the modular
///                   inverse of q, using modulus p.
/// @return           Returns 0 on success, or one of the gxcr_pka error codes
///                   on failure.
gxcr_pka_err_t gxcr_pka_exp_with_crt(gxcr_pka_handle_t *handle,
                                     void              *user_data,
                                     pka_operand_t     *p,
                                     pka_operand_t     *q,
                                     pka_operand_t     *c,
                                     pka_operand_t     *d_p,
                                     pka_operand_t     *d_q,
                                     pka_operand_t     *qinv);

/// Modular Inversion Function.
///
/// Implements <B>'value^<SUP>-1</SUP> mod modulus'</B>, i.e. finds a big
/// integer such that when it is multiplied by value mod modulus results in
/// the value 1.  If the modulus is a prime (normal case or required?), then
/// such an inverse value must exist (as long as value != 0) and must be
/// unique.
/// @param handle     An initialized PKA handle to use for this request.
/// @param user_data  Opaque user pointer that is returned with the result.
/// @param value      The big integer whose modular inverse is requested.
/// @param modulus    The big integer modulus.  Must be odd.
/// @return           Returns 0 on success, or one of the gxcr_pka error codes
///                   on failure.
gxcr_pka_err_t gxcr_pka_mod_invert(gxcr_pka_handle_t *handle,
                                   void              *user_data,
                                   pka_operand_t     *value,
                                   pka_operand_t     *modulus);

/// Implements modular elliptic curve addition.
///
/// In particular, given two points
/// on an elliptic curve defined using a finite field defined by a large prime
/// number, determine their sum.  <B> Note that elliptic curves using a modulus
/// of 2^m (aka binary fields) are NOT supported. </B> The elliptic curve is
/// assumed to be defined by the set of all points `(x,y)` such that x and y
/// are integers in the range 0..p-1 and such that they satisfy the eqn:
/// @code
/// y^2 = x^3 + a*x + b mod p;
/// // where x and y are in the range 0..p-1 and p is a big prime number.
/// @endcode
/// The elliptic curve is then defined by the three big integer parameters,
/// a, b and p.  These three parameters are not completely independent.
/// In particular, the discriminant defined below must be non-zero.
/// @code
/// // Definition of the discriminant.  Must be non-zero.
/// discriminant = -16 * (4*a^3 + 27*b^2);
/// @endcode
///
/// The definition of elliptic curve point addition leds to the following
/// equations for computing the result point.  Notice that the result from
/// this operation has a result_cnt of 2 because it returns two large integers,
/// namely the x and y coordinate of the result point.
/// @code
/// if pointA.x == pointB.x then
///     s        = (3*pointA.x^2 + curve.a) / (2 * pointA.y) mod curve.p;
///     result.x = s*s - 2*pointA.x                          mod curve.p;
/// else
///     s        = ((pointA.y - pointB.y) / (pointA.x - pointB.x))  mod curve.p;
///     result.x = s*s - (pointA.x + pointB.x)                      mod curve.p;
/// endif
/// result.y = pointA.y + s * (result.x - pointA.x) mod curve.p;
/// @endcode
/// @param handle     An initialized PKA handle to use for this request.
/// @param user_data  Opaque user pointer that is returned with the result.
/// @param curve      A pointer to an ecc_curve_t object, which supplies the
///                   curve parameters a, b, and p where p must be prime.
/// @param pointA     A pointer to an ecc_point_t object, which supplies the
///                   x and y coordinates (as big integer) for the first point.
/// @param pointB     A pointer to an ecc_point_t object, which supplies the
///                   x and y coordinates (as big integer) for the second point.
/// @param user_data  Opaque user pointer that is returned with the result.
/// @return           Returns 0 on success, or one of the gxcr_pka error codes
///                   on failure.
gxcr_pka_err_t gxcr_pka_ecc_add(gxcr_pka_handle_t *handle,
                                void              *user_data,
                                ecc_curve_t       *curve,
                                ecc_point_t       *pointA,
                                ecc_point_t       *pointB);

/// Implements modular elliptic curve multiplication.
///
/// In particular, given a
/// point on an elliptic curve and a 'scalar' m, multiply the point by the
/// scalar giving a new result point.  Scalar multiplication is defined to be
/// equivalent to repeated elliptic curve addition (see gxcr_pka_ecc_add above
/// for details of elliptic curve addition).  <B> Note that elliptic curves
/// using a modulus of 2^m are NOT supported. </B>
/// @param handle     An initialized PKA handle to use for this request.
/// @param user_data  Opaque user pointer that is returned with the result.
/// @param curve      A pointer to an ecc_curve_t object, which supplies the
///                   curve parameters a, b, and p where p must be prime.
/// @param pointA     A pointer to an ecc_point_t object, which supplies the
///                   x and y coordinates (as big integer) for the first point.
/// @param multiplier A big integer indicating the number of times that pointA
///                   should be added to itself.
/// @param user_data  Opaque user pointer that is returned with the result.
/// @return           Returns 0 on success, or one of the gxcr_pka error codes
///                   on failure.
gxcr_pka_err_t gxcr_pka_ecc_multiply(gxcr_pka_handle_t *handle,
                                     void              *user_data,
                                     ecc_curve_t       *curve,
                                     ecc_point_t       *pointA,
                                     pka_operand_t     *multiplier);

/// Implements the Elliptic Curve DSA Signature Generation algorithm.
///
/// Specifically implements the following eqns:
/// @code
/// k_inv = k^-1 mod base_pt_order; // Modular inverse of k.
/// KG    = k * base_pt;            // This is an ECC point multiplication.
/// r     = KG.x mod base_pt_order;
/// s     = (k_inv * (hash + private_key * r) mod base_pt_order;
/// @endcode
/// Note that the final result of this operation is a dsa_signature_t object
/// containing two big integers (hence the result_cnt will be 2) called r and
/// s by the standard.
/// @param handle      An initialized PKA handle to use for this request.
/// @param user_data   Opaque user pointer that is returned with the result.
/// @param curve       A pointer to an ecc_curve_t object, which supplies the
///                    curve parameters a, b, and p where p must be prime.
/// @param base_pt     A pointer to an ecc_point_t object, which supplies the
///                    x and y coordinates (as big integer) for the base point.
/// @param base_pt_order The big integer number such that when base_pt is
///                    multiplied by this number (i.e. using
///                    gxcr_pka_ecc_multiply above) the result is 1?
/// @param private_key The big integer used as the private key.  Refered to as
///                    alpha in the FIPS-186 spec.
/// @param hash        The hash (using one of SHA hash algorithms) of the msg.
///                    Also called the message digest.
/// @param k           A big integer used an additional ranom number secret
///                    value in the algorithm,
/// @return            Returns 0 on success, or one of the gxcr_pka error codes
///                    on failure.
gxcr_pka_err_t gxcr_pka_ecdsa_gen(gxcr_pka_handle_t *handle,
                                  void              *user_data,
                                  ecc_curve_t       *curve,
                                  ecc_point_t       *base_pt,
                                  pka_operand_t     *base_pt_order,
                                  pka_operand_t     *private_key,
                                  pka_operand_t     *hash,
                                  pka_operand_t     *k);

/// Implements the Elliptic Curve DSA Signature Verification algorithm.
///
/// Specifically implements the following eqns:
/// @code
/// // Note that public_key = g^private_key mod p
/// // Chk that 0 < r < base_pt_order and 0 < s < base_pt_order
/// s_inv  = s^(-1)                mod base_pt_order;
/// u1     = (hash        * s_inv) mod base_pt_order;
/// u2     = (signature.r * s_inv) mod base_pt_order;
/// sum_pt = (u1 * base_pt) + (u2 * public_key);   // ECC adds and mults.
/// // Chk that signature.r == sum_pt.x mod base_pt_order;
/// @endcode
/// Note that the final result of this operation is a boolean - result_code -
/// and so the result_cnt will be 0!
/// @param handle      An initialized PKA handle to use for this request.
/// @param user_data   Opaque user pointer that is returned with the result.
/// @param curve       A pointer to an ecc_curve_t object, which supplies the
///                    curve parameters a, b, and p where p must be prime.
/// @param base_pt     A pointer to an ecc_point_t object, which supplies the
///                    x and y coordinates (as big integer) for the base point.
/// @param base_pt_order The big integer number such that when base_pt is
///                    multiplied by this number (i.e. using
///                    gxcr_pka_ecc_multiply above) the result is 1?
/// @param public_key  A pointer to an ecc_point_t object, which represents the
///                    public key of this crypto system.
/// @param hash        The hash (using one of SHA hash algorithms) of the msg.
///                    Also called the message digest.
/// @param signature   A pointer to a dsa_signature object containing two large
///                    integers, called r and s in the standard, representing
///                    a cryptographically secure digital signature.
/// @return            Returns 0 on success, or one of the gxcr_pka error codes
///                    on failure.
gxcr_pka_err_t gxcr_pka_ecdsa_verify(gxcr_pka_handle_t *handle,
                                     void              *user_data,
                                     ecc_curve_t       *curve,
                                     ecc_point_t       *base_pt,
                                     pka_operand_t     *base_pt_order,
                                     ecc_point_t       *public_key,
                                     pka_operand_t     *hash,
                                     dsa_signature_t   *signature);

/// Implements the Original Digital Signature Generation Algorithm
///
/// Specifically implements the following eqns:
/// @code
/// k_inv       = k^-1        mod q;
/// signature.r = (g^k mod p) mod q;
/// signature.s = (k_inv * (hash + private_key * signature.r) mod q;
/// @endcode
/// Note that the final result of this operation is a dsa_signature_t object
/// containing two big integers (hence the result_cnt will be 2) called r and
/// s by the standard.
/// @param handle      An initialized PKA handle to use for this request.
/// @param user_data   Opaque user pointer that is returned with the result.
/// @param p           A big prime number.
/// @param q           A big prime number that divides 'p-1'.
/// @param g           A big integer, also refered to as the generator.
/// @param private_key A big integer used as the private key.  Refered to as
///                    alpha in the FIPS-186 spec.
/// @param hash        The hash (using one of SHA hash algorithms) of the msg.
///                    Also called the message digest.
/// @param k           A big integer used an additional ranom number secret
///                    value in the algorithm,
/// @return            Returns 0 on success, or one of the gxcr_pka error codes
///                    on failure.
gxcr_pka_err_t gxcr_pka_dsa_gen(gxcr_pka_handle_t *handle,
                                void              *user_data,
                                pka_operand_t     *p,
                                pka_operand_t     *q,
                                pka_operand_t     *g,
                                pka_operand_t     *private_key,
                                pka_operand_t     *hash,
                                pka_operand_t     *k);

/// Implements the Original Digital Signature Verification Algorithm
///
/// Specifically implements the following eqns:
/// @code
/// // Note that public_key = g^private_key mod p
/// // Chk that 0 < r < q and 0 < s < q
/// s_inv  = signature.s^(-1) mod q;
/// u1     = (hash        * s_inv) mod q;
/// u2     = (signature.r * s_inv) mod q;
/// v      = (((g^u1) * (public_key^u2)) mod p) mod q;
/// // Chk that v == signtaure.r
/// @endcode
/// Note that the final result of this operation is a boolean - result_code -
/// and so the result_cnt will be 0!
/// @param handle      An initialized PKA handle to use for this request.
/// @param user_data   Opaque user pointer that is returned with the result.
/// @param p           A big prime number.
/// @param q           A big prime number that divides 'p-1'.
/// @param g           A big integer, also refered to as the generator.
/// @param public_key  A big integer used as the public key.  Refered to as
///                    alpha in the FIPS-186 spec.
/// @param hash        The hash (using one of SHA hash algorithms) of the msg.
///                    Also called the message digest.
/// @param signature   A pointer to a dsa_signature object containing two large
///                    integers, called r and s in the standard, representing
///                    a cryptographically secure digital signature.
/// @return            Returns 0 on success, or one of the gxcr_pka error codes
///                    on failure.
gxcr_pka_err_t gxcr_pka_dsa_verify(gxcr_pka_handle_t *handle,
                                   void              *user_data,
                                   pka_operand_t     *p,
                                   pka_operand_t     *q,
                                   pka_operand_t     *g,
                                   pka_operand_t     *public_key,
                                   pka_operand_t     *hash,
                                   dsa_signature_t   *signature);



/// \cond
// Implements ANY of the operations supported by the chip.
gxcr_pka_err_t gxcr_pka_submit_op(gxcr_pka_handle_t *handle,
                                  void              *user_data,
                                  pka_opcode_t       opcode,
                                  pka_operands_t    *operands);
/// \endcond

/// Wait for Results Fifo to be Non-empty.
///
/// This function waits indefinitely for any new results to be available
/// for retrieval.
/// @param handle      An initialized PKA handle to use for this request.
void gxcr_pka_wait_for_results(gxcr_pka_handle_t *handle);

/// Check if the Results Fifo is Non-empty.
///
/// This function checks the result fifo associated with the given handle
/// and returns 1 if there is a result available for processing, 0 otherwise.
/// @param handle      An initialized PKA handle to use for this request.
/// @return            Returns 1 if result available, 0 if result fifo is empty.
int gxcr_pka_has_results(gxcr_pka_handle_t *handle);

/// Get Next Results from the Result Fifo.
///
/// This function waits until a result is available, and then copies it out to
/// the user.  Note that once this functions returns, the result fifo head is
/// advanced (i.e. the result copied out is removed from the system).
/// @param handle      An initialized PKA handle to use for this request.
/// @param results     A pointer to an <B>ibnitialized</B> pka_results_t
///                    structure.
/// @return            Always returns 0.
int gxcr_pka_get_results(gxcr_pka_handle_t *handle, pka_results_t *results);

/// Try to Get Next Results from the Result Fifo.
///
/// TRY to Get the Next Results from the Result Fifo.
/// This function is similar to gxcr_pka_get_results, except that it doesn't
/// wait for a result to become available.  Returns PKA_TRY_GET_RESULTS_FAILED
/// (which is always < 0) if there isn't a result available now.  Returns 0 if
/// there was a result available and it was copied out AND removed from the
/// result fifo.
/// @param handle      An initialized PKA handle to use for this request.
/// @param results     A pointer to an <B>ibnitialized</B> pka_results_t
///                    structure.
/// @return            Returns 0 if there was a result available, otherwise
///                    returns PKA_TRY_GET_RESULTS_FAILED.
gxcr_pka_err_t gxcr_pka_try_get_results(gxcr_pka_handle_t *handle,
                                        pka_results_t     *results);

/// Returns PKA_TRY_GET_RESULTS_FAILED (which is always < 0) if there isn't a
/// result available right now.  This function does NOT wait for a result.
/// In this case, the opcode, status and user_data are left unmodified.
/// If there is a result available, then it returns a positive value which is
/// the result_cnt's value for the head of the results fifo (i.e. the number of
/// result operands associated with this result), which can be 0, 1 or 2.
/// In this case, it will also copy out the opcode, status, and/or user_data,
/// if the associated output ptr is != NULL.
/// @param handle      An initialized PKA handle to use for this request.
/// @param opcode
/// @param status
/// @param user_data
/// @return             Returns 0 on success, or one of the gxcr_pka error codes
///                     on failure.
gxcr_pka_err_t gxcr_pka_result_info(gxcr_pka_handle_t *handle,
                                    uint8_t           *opcode,
                                    uint32_t          *status,
                                    void             **user_data);

/// Get the Lengths of the Result Operand from Head of Result Fifo.
///
// Returns TRY_GET_RESULTS_FAILED (which is always < 0) if the results fifo
// is currently empty.  This function does NOT wait for a result.  In this case
// the result operand lengths are left unmodified.  If there is a result
// available, then it returns a positive value which is the result_cnt's value
// for the head of the results fifo (i.e. the number of result operands
// associated with this result), which can be 0, 1 or 2.  In this case, it
// will also copy out the result operand lengths, it the associated output is
// != NULL.  Note that if the result_cnt is 0, then both result operands
// are considered to have zero length.  Also when the result_cnt is 1, then
// the result_operand1_len (corresponding to the second result operand, or
// result_idx == 1) is zero.
/// @param handle             An initialized PKA handle to use for this request.
/// @param result_operand0_len A pointer to a 32-bit integer where the length of
///                            the first result operand is to be stored.  I the
///                            result_cnt is 0 then the length stored will be 0.
/// @param result_operand1_len A pointer to a 32-bit integer where the length of
///                            second result operand is to be stored.  If
///                            result_cnt is < 2 then the length stored will be
///                            0
/// @return                    Returns result_cnt.
int gxcr_pka_result_lengths(gxcr_pka_handle_t *handle,
                            uint32_t          *result_operand0_len,
                            uint32_t          *result_operand1_len);

/// Copy out a Result Operand from Head of Result Fifo.
///
/// Used to copy out a specific result operand.  Does NOT advance the result
/// fifo head.  After using this call, the user still MUST call
/// gxcr_pka_advance_result_fifo_head.
/// @param handle         An initialized PKA handle to use for this request.
/// @param result_idx     Normally zero, but can be 1 when the associated cmd
///                       has a result_cnt of 2 and the user wants to copy out
///                       the second big integer result.
/// @param result_buf     A pointer to a result buffer where the big number
///                       operand will be copied into.
/// @param result_buf_len The length of the result_buf
/// @return               Returns 0 on success, or one of the gxcr_pka error
///                       codes on failure.
gxcr_pka_err_t gxcr_pka_copy_result_operand(gxcr_pka_handle_t *handle,
                                            uint32_t           result_idx,
                                            uint8_t           *result_buf,
                                            uint32_t           result_buf_len);

/// Consume Head of the Result Fifo.
///
/// Consumes/advances the result at the head of the result fifo.  This fcn
/// MUST be called OR  gxcr_pka_get_results must be called to move to next
/// item in the result fifo.  Returns 0 on SUCCESS, < 0 if there was an error.
/// It is an error to call this fcn if there is no result available.
/// @param handle         An initialized PKA handle to use for this request.
/// @return               Returns 0 on success, or one of the gxcr_pka error
///                       codes on failure.
gxcr_pka_err_t gxcr_pka_pop_result_fifo(gxcr_pka_handle_t *handle);

/// Get a Block of Random Bytes.
///
/// This function attempts to get 'num_rand_bytes' bytes from the per-handle
/// random number fifo.  If there are not enough bytes to fulfill the request,
/// this function will wait (busy spinning) until there are enough and then
/// return.
/// @param handle         An initialized PKA handle to use for this request.
/// @param rand_bytes     A pointer to a buffer to hold the random bytes.
/// @param num_rand_bytes The number of random bytes requested.
/// @return               Returns 0 on success, or one of the gxcr_pka error
///                       codes on failure.
gxcr_pka_err_t gxcr_pka_get_rand_bytes(gxcr_pka_handle_t *handle,
                                       uint8_t           *rand_bytes,
                                       uint32_t           num_rand_bytes);

/// Try to Get a Block of Random Bytes.
///
/// This function attempts to get 'num_rand_bytes' bytes from the per-handle
/// random number fifo, but does not wait.  If there are not enough bytes to
/// fulfill the request at the time of the call, the call fails, an error
/// code is returned and no bytes are copied into the buffer.  Otherwise the
/// call succeeds, the requested number of bytes are moved from the random
/// number fifo to the result buffer.
/// @param  handle        An initialized PKA handle to use for this request.
/// @param rand_bytes     A pointer to a buffer to hold the random bytes.
/// @param num_rand_bytes The number of random bytes requested.
/// @return               Returns 0 on success, or one of the gxcr_pka error
///                       codes on failure.
gxcr_pka_err_t gxcr_pka_try_get_rand_bytes(gxcr_pka_handle_t *handle,
                                           uint8_t           *rand_bytes,
                                           uint32_t           num_rand_bytes);

/// @}

#endif
