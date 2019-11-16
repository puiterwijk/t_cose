/*
 *  t_cose_sign_verify_test.c
 *
 * Copyright 2019, Laurence Lundblade
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See BSD-3-Clause license in README.md
 */

#include "t_cose_openssl_test.h"
#include "t_cose_sign1_sign.h"
#include "t_cose_sign1_verify.h"
#include "q_useful_buf.h"
#include "t_cose_make_test_pub_key.h"

#include "t_cose_crypto.h" /* Just for t_cose_crypto_sig_size() */


int_fast32_t sign_verify_basic_test_alg(int32_t cose_alg)
{
    struct t_cose_sign1_sign_ctx   sign_ctx;
    enum t_cose_err_t              return_value;
    Q_USEFUL_BUF_MAKE_STACK_UB(    signed_cose_buffer, 300);
    struct q_useful_buf_c          signed_cose;
    struct t_cose_key              key_pair;
    struct q_useful_buf_c          payload;
    struct t_cose_sign1_verify_ctx verify_ctx;

    /* -- Get started with context initialization, selecting the alg -- */
    t_cose_sign1_sign_init(&sign_ctx, 0, cose_alg);

    /* Make an ECDSA key pair that will be used for both signing and
     * verification.
     */
    return_value = make_ecdsa_key_pair(cose_alg, &key_pair);
    if(return_value) {
        return 1000 + return_value;
    }
    t_cose_sign1_set_signing_key(&sign_ctx, key_pair,  NULL_Q_USEFUL_BUF_C);

    t_cose_sign1_sign(&sign_ctx,
                      Q_USEFUL_BUF_FROM_SZ_LITERAL("payload"),
                      signed_cose_buffer,
                      &signed_cose);
    if(return_value) {
        return 2000 + return_value;
    }

    /* Verification */
    t_cose_sign1_verify_init(&verify_ctx, 0);

    t_cose_sign1_set_verification_key(&verify_ctx, key_pair);

    return_value = t_cose_sign1_verify(&verify_ctx,
                                       signed_cose,         /* COSE to verify */
                                       &payload,  /* Payload from signed_cose */
                                       NULL);         /* Don't return headers */
    if(return_value) {
        return 5000 + return_value;
    }

    /* OpenSSL uses malloc to allocate buffers for keys, so they have to be freed */
    free_ecdsa_key_pair(key_pair);

    /* compare payload output to the one expected */
    if(q_useful_buf_compare(payload, Q_USEFUL_BUF_FROM_SZ_LITERAL("payload"))) {
        return 6000;
    }

    return 0;
}


int_fast32_t sign_verify_basic_test()
{
    int_fast32_t return_value;

    return_value  = sign_verify_basic_test_alg(T_COSE_ALGORITHM_ES256);
    if(return_value) {
        return 20000 + return_value;
    }

#ifndef T_COSE_DISABLE_ES384
    return_value  = sign_verify_basic_test_alg(T_COSE_ALGORITHM_ES384);
    if(return_value) {
        return 30000 + return_value;
    }
#endif

#ifndef T_COSE_DISABLE_ES512
    return_value  = sign_verify_basic_test_alg(T_COSE_ALGORITHM_ES512);
    if(return_value) {
        return 50000 + return_value;
    }
#endif

    return 0;

}


int_fast32_t sign_verify_sig_fail_test()
{
    struct t_cose_sign1_sign_ctx   sign_ctx;
    QCBOREncodeContext             cbor_encode;
    enum t_cose_err_t              return_value;
    Q_USEFUL_BUF_MAKE_STACK_UB(    signed_cose_buffer, 300);
    struct q_useful_buf_c          signed_cose;
    struct t_cose_key              key_pair;
    struct q_useful_buf_c          payload;
    QCBORError                     cbor_error;
    struct t_cose_sign1_verify_ctx verify_ctx;


    /* Make an ECDSA key pair that will be used for both signing and
     * verification.
     */
    return_value = make_ecdsa_key_pair(T_COSE_ALGORITHM_ES256, &key_pair);
    if(return_value) {
        return 1000 + return_value;
    }

    QCBOREncode_Init(&cbor_encode, signed_cose_buffer);

    t_cose_sign1_sign_init(&sign_ctx,  0,  T_COSE_ALGORITHM_ES256);
    t_cose_sign1_set_signing_key(&sign_ctx, key_pair,NULL_Q_USEFUL_BUF_C);

    return_value = t_cose_sign1_encode_headers(&sign_ctx, &cbor_encode);
    if(return_value) {
        return 2000 + return_value;
    }

    QCBOREncode_AddSZString(&cbor_encode, "payload");


    return_value = t_cose_sign1_encode_signature(&sign_ctx, &cbor_encode);
    if(return_value) {
        return 3000 + return_value;
    }

    cbor_error = QCBOREncode_Finish(&cbor_encode, &signed_cose);
    if(cbor_error) {
        return 4000 + cbor_error;
    }

    /* tamper with the pay load to see that the signature verification fails */
    size_t xx = q_useful_buf_find_bytes(signed_cose, Q_USEFUL_BUF_FROM_SZ_LITERAL("payload"));
    if(xx == SIZE_MAX) {
        return 99;
    }
    ((char *)signed_cose.ptr)[xx] = 'h';


    t_cose_sign1_verify_init(&verify_ctx, 0);

    t_cose_sign1_set_verification_key(&verify_ctx, key_pair);

    return_value = t_cose_sign1_verify(&verify_ctx,
                                       signed_cose,         /* COSE to verify */
                                       &payload,  /* Payload from signed_cose */
                                       NULL);         /* Don't return headers */

    if(return_value != T_COSE_ERR_SIG_VERIFY) {
        return 5000 + return_value;
    }

    free_ecdsa_key_pair(key_pair);

    return 0;
}


int_fast32_t sign_verify_make_cwt_test()
{
    struct t_cose_sign1_sign_ctx   sign_ctx;
    QCBOREncodeContext             cbor_encode;
    enum t_cose_err_t              return_value;
    Q_USEFUL_BUF_MAKE_STACK_UB(    signed_cose_buffer, 300);
    struct q_useful_buf_c          signed_cose;
    struct t_cose_key              key_pair;
    struct q_useful_buf_c          payload;
    QCBORError                     cbor_error;
    struct t_cose_sign1_verify_ctx verify_ctx;



    /* -- initialize for signing --
     *  No special options selected
     */
    t_cose_sign1_sign_init(&sign_ctx,  0,  T_COSE_ALGORITHM_ES256);


    /* -- Key and kid --
     * The ECDSA key pair made is both for signing and verification.
     * The kid comes from RFC 8932
     */
    return_value = make_ecdsa_key_pair(T_COSE_ALGORITHM_ES256, &key_pair);
    if(return_value) {
        return 1000 + return_value;
    }
    t_cose_sign1_set_signing_key(&sign_ctx,
                                  key_pair,
                                  Q_USEFUL_BUF_FROM_SZ_LITERAL("AsymmetricECDSA256"));


    /* -- Encoding context and output of headers -- */
    QCBOREncode_Init(&cbor_encode, signed_cose_buffer);
    return_value = t_cose_sign1_encode_headers(&sign_ctx, &cbor_encode);
    if(return_value) {
        return 2000 + return_value;
    }


    /* -- The payload as from RFC 8932 -- */
    QCBOREncode_OpenMap(&cbor_encode);
    QCBOREncode_AddSZStringToMapN(&cbor_encode, 1, "coap://as.example.com");
    QCBOREncode_AddSZStringToMapN(&cbor_encode, 2, "erikw");
    QCBOREncode_AddSZStringToMapN(&cbor_encode, 3, "coap://light.example.com");
    QCBOREncode_AddInt64ToMapN(&cbor_encode, 4, 1444064944);
    QCBOREncode_AddInt64ToMapN(&cbor_encode, 5, 1443944944);
    QCBOREncode_AddInt64ToMapN(&cbor_encode, 6, 1443944944);
    const uint8_t xx[] = {0x0b, 0x71};
    QCBOREncode_AddBytesToMapN(&cbor_encode, 7, Q_USEFUL_BUF_FROM_BYTE_ARRAY_LITERAL(xx));
    QCBOREncode_CloseMap(&cbor_encode);


    /* -- Finish up the COSE_Sign1. This is where the signing happens -- */
    return_value = t_cose_sign1_encode_signature(&sign_ctx, &cbor_encode);
    if(return_value) {
        return 2000 + return_value;
    }

    /* Finally close off the CBOR formatting and get the pointer and length
     * of the resulting COSE_Sign1
     */
    cbor_error = QCBOREncode_Finish(&cbor_encode, &signed_cose);
    if(cbor_error) {
        return 3000 + cbor_error;
    }
    /* --- Done making COSE Sign1 object  --- */


    /* Compare to expected from CWT RFC */
    /* The first part, the intro and protected headers must be the same */
    const uint8_t rfc8392_first_part_bytes[] = {
        0xd2, 0x84, 0x43, 0xa1, 0x01, 0x26, 0xa1, 0x04, 0x52, 0x41, 0x73, 0x79,
        0x6d, 0x6d, 0x65, 0x74, 0x72, 0x69, 0x63, 0x45, 0x43, 0x44, 0x53, 0x41,
        0x32, 0x35, 0x36, 0x58, 0x50, 0xa7, 0x01, 0x75, 0x63, 0x6f, 0x61, 0x70,
        0x3a, 0x2f, 0x2f, 0x61, 0x73, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c,
        0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x02, 0x65, 0x65, 0x72, 0x69, 0x6b, 0x77,
        0x03, 0x78, 0x18, 0x63, 0x6f, 0x61, 0x70, 0x3a, 0x2f, 0x2f, 0x6c, 0x69,
        0x67, 0x68, 0x74, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e,
        0x63, 0x6f, 0x6d, 0x04, 0x1a, 0x56, 0x12, 0xae, 0xb0, 0x05, 0x1a, 0x56,
        0x10, 0xd9, 0xf0, 0x06, 0x1a, 0x56, 0x10, 0xd9, 0xf0, 0x07, 0x42, 0x0b,
        0x71};
    struct q_useful_buf_c fp = Q_USEFUL_BUF_FROM_BYTE_ARRAY_LITERAL(rfc8392_first_part_bytes);
    struct q_useful_buf_c head = q_useful_buf_head(signed_cose, sizeof(rfc8392_first_part_bytes));
    if(q_useful_buf_compare(head, fp)) {
        return -1;
    }

    /* --- Start verifying the COSE Sign1 object  --- */
    /* Run the signature verification */
    t_cose_sign1_verify_init(&verify_ctx, 0);

    t_cose_sign1_set_verification_key(&verify_ctx, key_pair);

    return_value =
        t_cose_sign1_verify(&verify_ctx,
                            signed_cose,                    /* COSE to verify */
                            &payload,             /* Payload from signed_cose */
                            NULL);         /* Don't return headers */

    if(return_value) {
        return 4000 + return_value;
    }

    /* Format the expected payload CBOR fragment */

    /* Skip the key id, because this has the short-circuit key id */
    const size_t kid_encoded_len =
      1 +
      1 +
      1 +
      strlen("AsymmetricECDSA256"); // length of short-circuit key id


    /* compare payload output to the one expected */
    if(q_useful_buf_compare(payload, q_useful_buf_tail(fp, kid_encoded_len + 8))) {
        return 5000;
    }
    /* --- Done verifying the COSE Sign1 object  --- */

    return 0;
}




static int size_test(int32_t               cose_algorithm_id,
                     struct q_useful_buf_c kid,
                     struct t_cose_key     key_pair)
{
    struct t_cose_sign1_sign_ctx   sign_ctx;
    QCBOREncodeContext             cbor_encode;
    enum t_cose_err_t              return_value;
    struct q_useful_buf            nil_buf;
    size_t                         calculated_size;
    QCBORError                     cbor_error;
    struct q_useful_buf_c          actual_signed_cose;
    Q_USEFUL_BUF_MAKE_STACK_UB(    signed_cose_buffer, 300);
    struct q_useful_buf_c          payload;
    size_t                         sig_size;

    /* ---- Common Set up ---- */
    payload = Q_USEFUL_BUF_FROM_SZ_LITERAL("payload");
    return_value = t_cose_crypto_sig_size(cose_algorithm_id, key_pair, &sig_size);

    /* ---- First calculate the size ----- */
    nil_buf = (struct q_useful_buf) {NULL, INT32_MAX};
    QCBOREncode_Init(&cbor_encode, nil_buf);

    t_cose_sign1_sign_init(&sign_ctx,  0,  cose_algorithm_id);
    t_cose_sign1_set_signing_key(&sign_ctx, key_pair, kid);

    return_value = t_cose_sign1_encode_headers(&sign_ctx, &cbor_encode);
    if(return_value) {
        return 2000 + return_value;
    }

    QCBOREncode_AddEncoded(&cbor_encode, payload);

    return_value = t_cose_sign1_encode_signature(&sign_ctx, &cbor_encode);
    if(return_value) {
        return 3000 + return_value;
    }

    cbor_error = QCBOREncode_FinishGetSize(&cbor_encode, &calculated_size);
    if(cbor_error) {
        return 4000 + cbor_error;
    }

    /* ---- General sanity check ---- */
    size_t expected_min = sig_size + payload.len + kid.len;

    if(calculated_size < expected_min || calculated_size > expected_min + 30) {
        return -1;
    }



    /* ---- Now make a real COSE_Sign1 and compare the size ---- */
    QCBOREncode_Init(&cbor_encode, signed_cose_buffer);

    t_cose_sign1_sign_init(&sign_ctx,  0,  cose_algorithm_id);
    t_cose_sign1_set_signing_key(&sign_ctx, key_pair, kid);

    return_value = t_cose_sign1_encode_headers(&sign_ctx, &cbor_encode);
    if(return_value) {
        return 2000 + return_value;
    }

    QCBOREncode_AddEncoded(&cbor_encode, payload);

    return_value = t_cose_sign1_encode_signature(&sign_ctx, &cbor_encode);
    if(return_value) {
        return 3000 + return_value;
    }

    cbor_error = QCBOREncode_Finish(&cbor_encode, &actual_signed_cose);
    if(actual_signed_cose.len != calculated_size) {
        return -2;
    }

    /* ---- Again with one-call API to make COSE_Sign1 ---- */\
    t_cose_sign1_sign_init(&sign_ctx,  0,  cose_algorithm_id);
    t_cose_sign1_set_signing_key(&sign_ctx, key_pair, kid);
    return_value = t_cose_sign1_sign(&sign_ctx,
                                     payload,
                                     signed_cose_buffer,
                                     &actual_signed_cose);
    if(return_value) {
        return 7000 + return_value;
    }

    if(actual_signed_cose.len != calculated_size) {
        return -3;
    }

    return 0;
}



int_fast32_t sign_verify_get_size_test()
{
    enum t_cose_err_t   return_value;
    struct t_cose_key   key_pair;
    int32_t             result;

    return_value = make_ecdsa_key_pair(T_COSE_ALGORITHM_ES256, &key_pair);
    if(return_value) {
        return 1000 + return_value;
    }

    result = size_test(T_COSE_ALGORITHM_ES256, NULL_Q_USEFUL_BUF_C, key_pair);
    if(result) {
        return result;
    }

    return_value = make_ecdsa_key_pair(T_COSE_ALGORITHM_ES384, &key_pair);
    if(return_value) {
        return 1000 + return_value;
    }

    result = size_test(T_COSE_ALGORITHM_ES384, NULL_Q_USEFUL_BUF_C, key_pair);
    if(result) {
        return result;
    }


    return_value = make_ecdsa_key_pair(T_COSE_ALGORITHM_ES512, &key_pair);
    if(return_value) {
        return 1000 + return_value;
    }

    result = size_test(T_COSE_ALGORITHM_ES512, NULL_Q_USEFUL_BUF_C, key_pair);
    if(result) {
        return result;
    }


    result = size_test(T_COSE_ALGORITHM_ES512,
                       Q_USEFUL_BUF_FROM_SZ_LITERAL("greasy kid stuff"),
                       key_pair);
    if(result) {
        return result;
    }

    return 0;
}
