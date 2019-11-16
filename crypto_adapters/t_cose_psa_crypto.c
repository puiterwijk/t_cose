/*
 * t_cose_psa_crypto.c
 *
 * Copyright 2019, Laurence Lundblade
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See BSD-3-Clause license in README.mdE.
 */


#include "t_cose_crypto.h"  /* The interface this implements */
#include "crypto.h"         /* PSA / TF_M stuff */


/* Avoid compiler warning due to unused argument */
#define ARG_UNUSED(arg) (void)(arg)


static psa_algorithm_t cose_alg_id_to_psa_alg_id(int32_t cose_alg_id)
{
    return cose_alg_id == COSE_ALGORITHM_ES256 ? PSA_ALG_ECDSA(PSA_ALG_SHA_256) :
#ifndef T_COSE_DISABLE_ES384
           cose_alg_id == COSE_ALGORITHM_ES384 ? PSA_ALG_ECDSA(PSA_ALG_SHA_384) :
#endif
#ifndef T_COSE_DISABLE_ES512
           cose_alg_id == COSE_ALGORITHM_ES512 ? PSA_ALG_ECDSA(PSA_ALG_SHA_512) :
#endif
                                                 0;
    /* psa/crypto_values.h doesn't seem to define a "no alg" value, but
     * zero seems OK for that use in the ECDSA context. */
}


/**
 * \brief Map a PSA error into a t_cose error for signing.
 *
 * \param[in] err   The PSA status.
 *
 * \return The t_cose error.
 */
static enum t_cose_err_t psa_status_to_t_cose_error_signing(psa_status_t err)
{
    /* Intentionally keeping to fewer mapped errors to save object code */
    return err == PSA_SUCCESS                   ? T_COSE_SUCCESS :
           err == PSA_ERROR_INVALID_SIGNATURE   ? T_COSE_ERR_SIG_VERIFY :
           err == PSA_ERROR_NOT_SUPPORTED       ? T_COSE_ERR_UNSUPPORTED_SIGNING_ALG:
           err == PSA_ERROR_INSUFFICIENT_MEMORY ? T_COSE_ERR_INSUFFICIENT_MEMORY :
           err == PSA_ERROR_TAMPERING_DETECTED  ? T_COSE_ERR_TAMPERING_DETECTED :
                                                  T_COSE_ERR_FAIL;
}


/*
 * See documentation in t_cose_crypto.h
 */
enum t_cose_err_t
t_cose_crypto_pub_key_verify(int32_t               cose_algorithm_id,
                             struct t_cose_key     verification_key,
                             struct q_useful_buf_c kid,
                             struct q_useful_buf_c hash_to_verify,
                             struct q_useful_buf_c signature)
{
    psa_algorithm_t   psa_alg_id;
    psa_status_t      psa_result;
    enum t_cose_err_t return_value;
    psa_key_handle_t  verification_key_psa;

    /* This implementation does no look up keys by kid in the key store */
    ARG_UNUSED(kid);

    /* Convert to PSA algorithm ID scheme */
    psa_alg_id = cose_alg_id_to_psa_alg_id(cose_algorithm_id);

    /* This implementation supports ECDSA and only ECDSA. The
     * interface allows it to support other, but none are implemented.
     * This implementation works for different keys lengths and
     * curves. That is the curve and key length as associated
     * with the signing_key passed in, not the cose_algorithm_id
     * This check looks for ECDSA signing as indicated by COSE and rejects what
     * is not.
     */
    if(!PSA_ALG_IS_ECDSA(psa_alg_id)) {
        return_value = T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
        goto Done;
    }

    verification_key_psa = (psa_key_handle_t)verification_key.k.key_handle;

    psa_result = psa_asymmetric_verify(verification_key_psa,
                                       psa_alg_id,
                                       hash_to_verify.ptr,
                                       hash_to_verify.len,
                                       signature.ptr,
                                       signature.len);

    return_value = psa_status_to_t_cose_error_signing(psa_result);

  Done:
    return return_value;
}


/*
 * See documentation in t_cose_crypto.h
 */
enum t_cose_err_t
t_cose_crypto_pub_key_sign(int32_t                cose_algorithm_id,
                           struct t_cose_key      signing_key,
                           struct q_useful_buf_c  hash_to_sign,
                           struct q_useful_buf    signature_buffer,
                           struct q_useful_buf_c *signature)
{
    enum t_cose_err_t return_value;
    psa_status_t      psa_result;
    psa_algorithm_t   psa_alg_id;
    psa_key_handle_t  signing_key_psa;
    size_t            signature_len;

    psa_alg_id = cose_alg_id_to_psa_alg_id(cose_algorithm_id);

    /* This implementation supports ECDSA and only ECDSA. The
     * interface allows it to support other, but none are implemented.
     * This implementation works for different keys lengths and
     * curves. That is the curve and key length as associated
     * with the signing_key passed in, not the cose_algorithm_id
     * This check looks for ECDSA signing as indicated by COSE and rejects what
     * is not.
     */
    if(!PSA_ALG_IS_ECDSA(psa_alg_id)) {
        return_value = T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
        goto Done;
    }

    signing_key_psa = (psa_key_handle_t)signing_key.k.key_handle;

    /* It is assumed that psa_asymmetric_sign() is checking signature_buffer
     * length and won't write off the end of it.
     */
    psa_result = psa_asymmetric_sign(signing_key_psa,
                                     psa_alg_id,
                                     hash_to_sign.ptr,
                                     hash_to_sign.len,
                                     signature_buffer.ptr, /* Sig buf */
                                     signature_buffer.len, /* Sig buf size */
                                    &signature_len);       /* Sig length */

    return_value = psa_status_to_t_cose_error_signing(psa_result);

    if(return_value == T_COSE_SUCCESS) {
        /* Success, fill in the return useful_buf */
        signature->ptr = signature_buffer.ptr;
        signature->len = signature_len;
    }

  Done:
     return return_value;
}


/*
 * See documentation in t_cose_crypto.h
 */
enum t_cose_err_t t_cose_crypto_sig_size(int32_t           cose_algorithm_id,
                                         struct t_cose_key signing_key,
                                         size_t           *sig_size)
{
    enum t_cose_err_t return_value;
    psa_key_handle_t  signing_key_psa;
    psa_key_type_t    key_type;
    size_t            key_len_bits;
    size_t            key_len_bytes;

    /* If desparate to save code, this can return the constant
     * T_COSE_MAX_SIG_SIZE instead of doing an exact calculation.
     * The buffer size calculation will return too large of a value
     * and waste a little heap / stack, but everything will still
     * work (except the tests that test for exact values will
     * fail). This will save 100 bytes or so of obejct code.
     */

    if(!t_cose_algorithm_is_ecdsa(cose_algorithm_id)) {
        return_value = T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
        goto Done;
    }

    signing_key_psa = (psa_key_handle_t)signing_key.k.key_handle;

    psa_status_t status = psa_get_key_information(signing_key_psa,
                                                  &key_type,
                                                  &key_len_bits);

    (void)key_type; /* Avoid unused parameter error */

    return_value = psa_status_to_t_cose_error_signing(status);
    if(return_value == T_COSE_SUCCESS) {
        /* Calculation of size per RFC 8152 section 8.1 -- round up to
         * number of bytes. */
        key_len_bytes = key_len_bits / 8;
        if(key_len_bits % 8) {
            key_len_bytes++;
        }
        /* double because signature is made of up r and s values */
        *sig_size = key_len_bytes * 2;
    }

    return_value = T_COSE_SUCCESS;
Done:
    return return_value;
}




/**
 * \brief Convert COSE algorithm ID to a PSA algorithm ID
 *
 * \param[in] cose_hash_alg_id   The COSE-based ID for the
 *
 * \return PSA-based hash algorithm ID, or USHRT_MAX on error.
 *
 */
static inline psa_algorithm_t
cose_hash_alg_id_to_psa(int32_t cose_hash_alg_id)
{
    return cose_hash_alg_id == COSE_ALGORITHM_SHA_256 ? PSA_ALG_SHA_256 :
#ifndef T_COSE_DISABLE_ES384
           cose_hash_alg_id == COSE_ALGORITHM_SHA_384 ? PSA_ALG_SHA_384 :
#endif
#ifndef T_COSE_DISABLE_ES512
           cose_hash_alg_id == COSE_ALGORITHM_SHA_512 ? PSA_ALG_SHA_512 :
#endif
                                                        UINT16_MAX;
}


/**
 * \brief Map a PSA error into a t_cose error for hashes.
 *
 * \param[in] status   The PSA status.
 *
 * \return The t_cose error.
 */
static enum t_cose_err_t
psa_status_to_t_cose_error_hash(psa_status_t status)
{
    /* Intentionally limited to just this minimum set of errors to
     * save object code as hashes don't really fail much
     */
    return status == PSA_SUCCESS                ? T_COSE_SUCCESS :
           status == PSA_ERROR_NOT_SUPPORTED    ? T_COSE_ERR_UNSUPPORTED_HASH :
           status == PSA_ERROR_BUFFER_TOO_SMALL ? T_COSE_ERR_HASH_BUFFER_SIZE :
                                                  T_COSE_ERR_HASH_GENERAL_FAIL;
}


/*
 * See documentation in t_cose_crypto.h
 */
enum t_cose_err_t t_cose_crypto_hash_start(struct t_cose_crypto_hash *hash_ctx,
                                           int32_t cose_hash_alg_id)
{
    /* Here's how t_cose_crypto_hash is used with PSA hashes.
     *
     * If you look inside psa_hash.handle is just a uint32_t that is
     * used as a handle. To avoid modifying t_cose_crypto.h in a
     * PSA-specific way, this implementation just copies the PSA
     * handle from the generic t_cose_crypto_hash on entry to a hash
     * function, and back on exit.
     *
     * This could have been implemented by modifying t_cose_crypto.h
     * so that psa_hash_operation_t is a member of t_cose_crypto_hash.
     * It's nice to not have to modify t_cose_crypto.h.
     *
     * This would have been cleaner if psa_hash_operation_t didn't
     * exist and the PSA crypto just used a plain pointer or integer
     * handle.  If psa_hash_operation_t is changed to be different
     * than just the single uint32_t, then this code has to change.
     *
     * The status member of t_cose_crypto_hash is used to hold a
     * psa_status_t error code.
     */
    psa_hash_operation_t psa_hash;
    psa_algorithm_t      psa_alg;

    /* Map the algorithm ID */
    psa_alg = cose_hash_alg_id_to_psa(cose_hash_alg_id);

    /* initialize PSA hash context */
    psa_hash = (psa_hash_operation_t){0};

    /* Actually do the hash set up */
    hash_ctx->status = psa_hash_setup(&psa_hash, psa_alg);

    /* Copy the PSA handle back into the context */
    hash_ctx->context.handle = psa_hash.handle;

    /* Map errors and return */
    return psa_status_to_t_cose_error_hash((psa_status_t)hash_ctx->status);
}


/*
 * See documentation in t_cose_crypto.h
 */
void t_cose_crypto_hash_update(struct t_cose_crypto_hash *hash_ctx,
                               struct q_useful_buf_c      data_to_hash)
{
    /* See t_cose_crypto_hash_start() for context handling details */
    psa_hash_operation_t psa_hash;

    /* Copy the PSA handle out of the generic context */
    psa_hash.handle = (uint32_t)hash_ctx->context.handle;

    if(hash_ctx->status != PSA_SUCCESS) {
        /* In error state. Nothing to do. */
        return;
    }

    if(data_to_hash.ptr == NULL) {
        /* This allows for NULL buffers to be passed in all the way at
         * the top of signer or message creator when all that is
         * happening is the size of the result is being computed.
         */
        return;
    }

    /* Actually hash the data */
    hash_ctx->status = psa_hash_update(&psa_hash,
                                       data_to_hash.ptr,
                                       data_to_hash.len);

    /* Copy the PSA handle back into the context. */
    hash_ctx->context.handle = psa_hash.handle;
}


/*
 * See documentation in t_cose_crypto.h
 */
enum t_cose_err_t
t_cose_crypto_hash_finish(struct t_cose_crypto_hash *hash_ctx,
                          struct q_useful_buf        buffer_to_hold_result,
                          struct q_useful_buf_c     *hash_result)
{
    /* See t_cose_crypto_hash_start() for context handling details */
    psa_hash_operation_t psa_hash;
    psa_status_t         status;

    /* Copy the PSA handle out of the generic context */
    psa_hash.handle = (uint32_t)hash_ctx->context.handle;
    status = (psa_status_t)hash_ctx->status;

    if(status != PSA_SUCCESS) {
        /* Error state. Nothing to do */
        goto Done;
    }

    /* Actually finish up the hash */
    status = psa_hash_finish(&psa_hash,
                                       buffer_to_hold_result.ptr,
                                       buffer_to_hold_result.len,
                                       &(hash_result->len));

    hash_result->ptr = buffer_to_hold_result.ptr;

    /* Copy the PSA handle back into the context. */
    hash_ctx->context.handle = psa_hash.handle;

Done:
    return psa_status_to_t_cose_error_hash(status);
}
