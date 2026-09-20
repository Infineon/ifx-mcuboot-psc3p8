/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2016-2019 JUUL Labs
 * Copyright (c) 2017 Linaro LTD
 * Copyright (C) 2021 Arm Limited
 *
 * Original license:
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>

#include "mcuboot_config/mcuboot_config.h"
#include "cyboot_flash_list.h"

#include "bootutil/sign_key.h"

#include "mbedtls/oid.h"
#include "mbedtls/asn1.h"
#include "mbedtls/asn1write.h"
#include "bootutil_priv.h"

#ifdef USE_LMS
#include "pq_crypto.h"
#endif

#ifdef USE_CRYPTOLITE_ECDSA
#include "ecdsa_cryptolite.h"
#endif /* USE_CRYPTOLITE_ECDSA */

#ifdef MCUBOOT_SIGN_XMSS
/* XMSS verification API from cy-mbedtls-acceleration */
#include "ifx_xmss.h"
#endif

#if defined(MCUBOOT_SIGN_MLDSA44) || defined(MCUBOOT_SIGN_MLDSA65) || defined(MCUBOOT_SIGN_MLDSA87)
#include "user_settings.h"
#include <wolfssl/wolfcrypt/dilithium.h>
#endif

#if defined(USE_CRYPTOLITE_ECDSA)
static int
bootutil_import_key(uint8_t **cp, uint8_t *end)
{
    size_t len;
    mbedtls_asn1_buf alg;
    mbedtls_asn1_buf param;

    if (mbedtls_asn1_get_tag(cp, end, &len,
        MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE)) {
        return -1;
    }
    end = *cp + len;

    /* ECParameters (RFC5480) - parse algorithm identifier and curve parameters */
    if (mbedtls_asn1_get_alg(cp, end, &alg, &param)) {
        return -2;
    }

    /* Note: Algorithm and curve OID validation is not needed.
     * - Key length validation (below) ensures correct EC key size
     * - Cryptolite hardware uses the correct curve based on hash algorithm
     * - Invalid keys will fail cryptolite signature verification */

    /* ECPoint (RFC5480) */
    if (mbedtls_asn1_get_bitstring_null(cp, end, &len)) {
        return -6;
    }
    if (*cp + len != end) {
        return -7;
    }

    if (len != 2 * NUM_ECC_BYTES + 1) {
        return -8;
    }

    return 0;
}

/*
 * cp points to ASN1 string containing an integer.
 * Verify the tag, and that the length is 32 bytes.
 */
static int
bootutil_read_bigint(uint8_t i[NUM_ECC_BYTES], uint8_t **cp, uint8_t *end)
{
    size_t len;

    if (mbedtls_asn1_get_tag(cp, end, &len, MBEDTLS_ASN1_INTEGER)) {
        return -3;
    }

    if (len >= NUM_ECC_BYTES) {
        (void)memcpy(i, *cp + len - NUM_ECC_BYTES, NUM_ECC_BYTES);
    } else {
        (void)memset(i, 0, NUM_ECC_BYTES - len);
        (void)memcpy(i + NUM_ECC_BYTES - len, *cp, len);
    }
    *cp += len;
    return 0;
}

/*
 * Read in signature. Signature has r and s encoded as integers.
 */
static int
bootutil_decode_sig(uint8_t signature[NUM_ECC_BYTES * 2], uint8_t *cp, uint8_t *end)
{
    int rc;
    size_t len;

    rc = mbedtls_asn1_get_tag(&cp, end, &len,
                              MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
    if (rc) {
        return -1;
    }
    if (cp + len > end) {
        return -2;
    }

    rc = bootutil_read_bigint(signature, &cp, end);
    if (rc) {
        return -3;
    }
    rc = bootutil_read_bigint(signature + NUM_ECC_BYTES, &cp, end);
    if (rc) {
        return -4;
    }
    return 0;
}

#if defined(SFLASH_KEYS)
/**
 * Builds a DER-encoded EC public key from a raw public key using MbedTLS ASN.1 functions
 *
 * @param der_key      Buffer to store the resulting DER-encoded key
 * @param der_key_len  Pointer to store the resulting DER key length
 * @param raw_key      Input raw EC public key
 * @param raw_key_len  Length of the raw key
 * @return             0 on success, negative error code on failure
 */
int build_der_key_with_mbedtls(uint8_t *der_key, size_t *der_key_len, const uint8_t *raw_key, size_t raw_key_len) {
    /*
    * MbedTLS ASN.1 functions write data from the end of buffer backwards
    * This approach allows writing without knowing the total length in advance
    */
    uint8_t *start_buf = der_key;
    uint8_t *p = der_key + EC_KEY_SZ;
    int rc;

    uint8_t *bit_string_start;
    const unsigned char *curve_oid;

    /* Write the raw key data first (working from end to beginning) */
    p -= raw_key_len;
    memcpy(p, raw_key, raw_key_len);

    /* Add gap byte (required for BIT STRING) */
    *--p = 0x00;
    bit_string_start = p;

    size_t bit_string_len = raw_key_len + 1;
    rc = mbedtls_asn1_write_len(&p, start_buf, bit_string_len);
    if (rc < 0) {
        return rc;
    }

    rc = mbedtls_asn1_write_tag(&p, start_buf, MBEDTLS_ASN1_BIT_STRING);
    if (rc < 0) {
        return rc;
    }

    /* Save pointer for algorithm parameters size calculation */
    uint8_t *params_start = p;
    size_t curve_oid_len;

#if defined(USE_SHA256)
    const unsigned char secp256r1_oid[] = {
        0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07
    };
    curve_oid = secp256r1_oid;
    curve_oid_len = sizeof(secp256r1_oid);
#elif defined(USE_SHA384)
    static const unsigned char secp384r1_oid[] = {
        0x2B, 0x81, 0x04, 0x00, 0x22
    };
    curve_oid = secp384r1_oid;
    curve_oid_len = sizeof(secp384r1_oid);
#elif defined(USE_SHA512)
    static const unsigned char secp521r1_oid[] = {
        0x2B, 0x81, 0x04, 0x00, 0x23
    };
    curve_oid = secp521r1_oid;
    curve_oid_len = sizeof(secp521r1_oid);
#endif

    rc = mbedtls_asn1_write_raw_buffer(&p, start_buf, curve_oid, curve_oid_len);
    if (rc < 0) {
        return rc;
    }

    rc = mbedtls_asn1_write_len(&p, start_buf, curve_oid_len);
    if (rc < 0) {
        return rc;
    }

    rc = mbedtls_asn1_write_tag(&p, start_buf, MBEDTLS_ASN1_OID);
    if (rc < 0) {
        return rc;
    }

    /*
    * Add OID for the algorithm (id-ecPublicKey)
    * This is common for all EC keys, regardless of curve
    */
    const unsigned char ec_pubkey_oid[] = {
        0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01
    };

    rc = mbedtls_asn1_write_raw_buffer(&p, start_buf, ec_pubkey_oid, sizeof(ec_pubkey_oid));

    if (rc < 0) {
        return rc;
    }

    rc = mbedtls_asn1_write_len(&p, start_buf, sizeof(ec_pubkey_oid));
    if (rc < 0) {
        return rc;
    }

    rc = mbedtls_asn1_write_tag(&p, start_buf, MBEDTLS_ASN1_OID);
    if (rc < 0) {
        return rc;
    }

    /*
    *  Add inner SEQUENCE tag and length (algorithm identifier)
    *  This sequence contains two OIDs: id-ecPublicKey and the curve OID
    */
    size_t inner_len = params_start - p;

    rc = mbedtls_asn1_write_len(&p, start_buf, inner_len);
    if (rc < 0) {
        return rc;
    }

    rc = mbedtls_asn1_write_tag(&p, start_buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
    if (rc < 0) {
        return rc;
    }

    /*
    *  Add outer SEQUENCE tag and length (the entire public key)
    *  This sequence contains the algorithm identifier and the BIT STRING with the key
    */
    size_t outer_len = bit_string_start + bit_string_len - p;
    rc = mbedtls_asn1_write_len(&p, start_buf, outer_len);
    if (rc < 0) {
        return rc;
    }

    rc = mbedtls_asn1_write_tag(&p, start_buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
    if (rc < 0) {
        return rc;
    }

    /*: Calculate total length of DER-encoded key */
    *der_key_len = (der_key + EC_KEY_SZ) - p;

    memmove(der_key, p, *der_key_len);

    return 0;
}
#endif /* SFLASH_KEYS */
#endif /* USE_CRYPTOLITE_ECDSA */

#ifdef MCUBOOT_SIGN_XMSS
static fih_int xmss_verify_signature_api(uint8_t *message, uint64_t *message_len,
                                         const uint8_t *signed_message, size_t signed_message_len,
                                         const uint8_t *pubkey)
{
    int32_t rc = ifx_xmss_verify(message, message_len, signed_message,
                                 signed_message_len, pubkey);

    if (rc != IFX_XMSS_SIG_VALID) {
        FIH_RET(FIH_FAILURE);
    }

    FIH_RET(FIH_SUCCESS);
}

/*
 * XMSS signature verification function
 * Uses XMSS verification API from cy-mbedtls-acceleration
 * FI-protected: Uses FIH_CALL and redundant verification
 */
static fih_int xmss_verify_signature(uint8_t *hash, uint32_t hlen, 
                                     uint8_t *sig, size_t slen,
                                     uint8_t *pubkey, size_t key_len)
{
    fih_int fih_rc = FIH_FAILURE;
    fih_int fih_rc2 = FIH_FAILURE;
    uint64_t mlen = 0;
    
    /* Static buffers for bootloader - no malloc */
    static unsigned char sm[2532] = {0};  /* Max signature + message buffer */
    static unsigned char m[2532] = {0};     /* Message buffer (max hash size) */

    if (pubkey == NULL || key_len == 0U) {
        FIH_RET(FIH_FAILURE);
    }
    
    /* Check buffer sizes */
    size_t sm_len = slen + hlen;
    if (sm_len > sizeof(sm) || hlen > sizeof(m)) {
        FIH_RET(FIH_FAILURE);
    }
    
    /* Format: [signature || message] for xmss_sign_open */
    memcpy(sm, sig, slen);
    memcpy(sm + slen, hash, hlen);

    /* FI protection: Verify signature using FIH_CALL */
    FIH_CALL(xmss_verify_signature_api, fih_rc, m, &mlen, sm, sm_len, pubkey);
    
    /* Check FIH result */
    if (fih_not_eq(fih_rc, FIH_SUCCESS)) {
        FIH_RET(FIH_FAILURE);
    }
    
    /* Verify that extracted message length matches */
    if (mlen != hlen) {
        FIH_RET(FIH_FAILURE);
    }
    
    /* FI protection: Use FIH-protected memcmp for message verification */
    FIH_CALL(boot_fih_memequal, fih_rc2, m, hash, hlen);
    
    /* Verify both FIH checks succeeded */
    if (fih_not_eq(fih_rc2, FIH_SUCCESS)) {
        FIH_RET(FIH_FAILURE);
    }
    
    /* Redundant check: standard memcmp as second layer */
    if (memcmp(m, hash, hlen) != 0) {
        FIH_RET(FIH_FAILURE);
    }
    
    FIH_RET(FIH_SUCCESS);
}
#endif /* MCUBOOT_SIGN_XMSS */

#if defined(MCUBOOT_SIGN_MLDSA44) || defined(MCUBOOT_SIGN_MLDSA65) || defined(MCUBOOT_SIGN_MLDSA87)
/*
 * ML-DSA (Dilithium) signature verification function
 * Uses WolfSSL ML-DSA implementation with NIST FIPS 204 API
 * FI-protected: Uses FIH_CALL and redundant verification
 */
static fih_int mldsa_verify_signature(uint8_t *hash, uint32_t hlen, 
                                      uint8_t *sig, size_t slen,
                                      uint8_t *pubkey, size_t key_len)
{
    fih_int fih_rc = FIH_FAILURE;
    /* Static: MlDsaKey (~20KB for ML-DSA-87) cannot be stack-allocated
     * on CM33 without stack overflow. Safe: bootloader is single-threaded. */
    static MlDsaKey key;  
    int ret;
    int verify_result = 0;
    byte level = 0;

    /* Parameter validation */
    if (hash == NULL || sig == NULL || pubkey == NULL) {
        FIH_RET(FIH_FAILURE);
    }

    if (hlen == 0 || slen == 0 || key_len == 0) {
        FIH_RET(FIH_FAILURE);
    }

    /* Determine ML-DSA level based on public key size */
#if defined(MCUBOOT_SIGN_MLDSA44)
    if (key_len == ML_DSA_LEVEL2_PUB_KEY_SIZE) {
        level = 2;  /* ML-DSA-44 */
    } else
#endif
#if defined(MCUBOOT_SIGN_MLDSA65)
    if (key_len == ML_DSA_LEVEL3_PUB_KEY_SIZE) {
        level = 3;  /* ML-DSA-65 */
    } else
#endif
#if defined(MCUBOOT_SIGN_MLDSA87)
    if (key_len == ML_DSA_LEVEL5_PUB_KEY_SIZE) {
        level = 5;  /* ML-DSA-87 */
    } else
#endif
    {
        /* Invalid key size */
        FIH_RET(FIH_FAILURE);
    }

    /* Zero the static key struct before each use to prevent stale state
     * if a previous call returned before wc_MlDsaKey_Free() was reached */
    memset(&key, 0, sizeof(key));

    /* Initialize ML-DSA key (newer API: wc_MlDsaKey_Init)
     * Note: NULL heap and INVALID_DEVID for embedded stack-based memory */
    ret = wc_MlDsaKey_Init(&key, NULL, INVALID_DEVID);
    if (ret != 0) {
        FIH_RET(FIH_FAILURE);
    }

    /* Set ML-DSA security level parameters (newer API: wc_MlDsaKey_SetParams)
     * Level 2 = ML-DSA-44, Level 3 = ML-DSA-65, Level 5 = ML-DSA-87 */
    ret = wc_MlDsaKey_SetParams(&key, level);
    if (ret != 0) {
        wc_MlDsaKey_Free(&key);
        FIH_RET(FIH_FAILURE);
    }

    /* Import raw public key (newer API: wc_MlDsaKey_ImportPubRaw)
     * Note: Parameter order changed - key first, then data */
    ret = wc_MlDsaKey_ImportPubRaw(&key, pubkey, (word32)key_len);
    if (ret != 0) {
        wc_MlDsaKey_Free(&key);
        FIH_RET(FIH_FAILURE);
    }

    /* Determine hash type based on compile-time configuration */
#if defined(USE_SHA256)
    enum wc_HashType hash_type = WC_HASH_TYPE_SHA256;
#elif defined(USE_SHA384)
    enum wc_HashType hash_type = WC_HASH_TYPE_SHA384;
#elif defined(USE_SHA512)
    enum wc_HashType hash_type = WC_HASH_TYPE_SHA512;
#else
    #error "No hash algorithm defined: define USE_SHA256, USE_SHA384, or USE_SHA512"
#endif

    /* HashML-DSA (FIPS 204 p5.4)
     * mu = SHAKE256(tr || 0x01 || 0x00 || OID(hash) || hash) */
    ret = wc_dilithium_verify_ctx_hash(sig, (word32)slen, NULL, 0,
                                        hash_type, hash,
                                       (word32)hlen, &verify_result, &key);

    /* Free key resources (newer API: wc_MlDsaKey_Free) */
    wc_MlDsaKey_Free(&key);

    /* Check verification result */
    if (ret == 0 && verify_result == 1) {
        fih_rc = FIH_SUCCESS;
    }

    FIH_RET(fih_rc);
}
#endif /* MCUBOOT_SIGN_MLDSA44 || MCUBOOT_SIGN_MLDSA65 || MCUBOOT_SIGN_MLDSA87 */

fih_int
bootutil_verify_sig(uint8_t *hash, uint32_t hlen, uint8_t *sig, size_t slen,
                    uint8_t key_id)
{
    uint8_t *pubkey = NULL;
    uint8_t *end = NULL;
    size_t key_len = 0;
    fih_int fih_rc = FIH_FAILURE;

#if defined(USE_CRYPTOLITE_ECDSA)
    int rc = -1;
    uint8_t signature[2 * NUM_ECC_BYTES];
#if defined(SFLASH_KEYS)
    uint8_t der_key[EC_KEY_SZ];
    size_t der_key_len;
#endif /* SFLASH_KEYS */
#endif /* USE_CRYPTOLITE_ECDSA */

#if defined(USE_LMS)
    uint8_t signature[slen];
#endif /* USE_LMS */

    /* Fix warnings about unused variables when image validation is disabled */
#if !defined(USE_LMS) && !defined(USE_CRYPTOLITE_ECDSA) && !defined(MCUBOOT_SIGN_XMSS) && \
    !defined(MCUBOOT_SIGN_MLDSA44) && !defined(MCUBOOT_SIGN_MLDSA65) && !defined(MCUBOOT_SIGN_MLDSA87)
    (void)key_len;
    (void)hash;
    (void)hlen;
    (void)sig;
    (void)slen;
    (void)key_id;
#endif

#ifdef MCUBOOT_SIGN_XMSS
    /* XMSS signature verification */
    if (key_id < BOOT_NUM_SLOTS && bootutil_keys[key_id].key != NULL) {
        pubkey = (uint8_t *)bootutil_keys[key_id].key;
        /* XMSS key length: Direct dereference gets stored length (68 bytes for XMSS-SHA2_10_256)
         * Unlike ECDSA, XMSS keys are stored in raw format without ASN.1 encoding */
        key_len = *bootutil_keys[key_id].len;
        
        /* FI protection: XMSS verification using FIH_CALL */
        FIH_CALL(xmss_verify_signature, fih_rc, hash, hlen, sig, slen, pubkey, key_len);
        FIH_RET(fih_rc);
    }
    FIH_RET(FIH_FAILURE);
#endif /* MCUBOOT_SIGN_XMSS */

#if defined(MCUBOOT_SIGN_MLDSA44) || defined(MCUBOOT_SIGN_MLDSA65) || defined(MCUBOOT_SIGN_MLDSA87)
    /* ML-DSA signature verification */
    if (key_id < BOOT_NUM_SLOTS && bootutil_keys[key_id].key != NULL) {
        pubkey = (uint8_t *)bootutil_keys[key_id].key;
        /* ML-DSA key length: Raw format public key
         * ML-DSA-44: 1312 bytes, ML-DSA-65: 1952 bytes, ML-DSA-87: 2592 bytes */
        key_len = *bootutil_keys[key_id].len;
        
        /* FI protection: ML-DSA verification using FIH_CALL */
        FIH_CALL(mldsa_verify_signature, fih_rc, hash, hlen, sig, slen, pubkey, key_len);
        FIH_RET(fih_rc);
    }
    FIH_RET(FIH_FAILURE);
#endif /* MCUBOOT_SIGN_MLDSA44 || MCUBOOT_SIGN_MLDSA65 || MCUBOOT_SIGN_MLDSA87 */

#if defined(USE_CRYPTOLITE_ECDSA)
    pubkey = (uint8_t *)bootutil_keys[key_id].key;
#if defined(SFLASH_KEYS)
    size_t raw_key_len = *bootutil_keys[key_id].len;

    rc = build_der_key_with_mbedtls(der_key, &der_key_len, pubkey, raw_key_len);
    if (rc != 0) {
        FIH_RET(FIH_FAILURE);
    }

    pubkey = der_key;
    end = pubkey + der_key_len;
#else
    end = pubkey + *bootutil_keys[key_id].len;
#endif /* SFLASH_KEYS */

    rc = bootutil_import_key(&pubkey, end);

    if (rc != 0) {
        FIH_RET(FIH_FAILURE);
    }

    key_len = end - pubkey;

    rc = bootutil_decode_sig(signature, sig, sig + slen);

    if (rc != 0) {
        FIH_RET(FIH_FAILURE);
    }
#if !defined(USE_SHA512)
    if (hlen != NUM_ECC_BYTES) {
        FIH_RET(FIH_FAILURE);
    }
#else
    if (hlen != NUM_ECC_BYTES - 2) {
        FIH_RET(FIH_FAILURE);
    }
#endif /*USE_SHA512*/
#endif /* USE_CRYPTOLITE_ECDSA */

/* Verification parameters for LMS signature */
#if defined(USE_LMS)
    if (sizeof(signature) < slen){
        FIH_RET(FIH_FAILURE);
    }

    memcpy(signature, sig, slen);
    pubkey = (uint8_t *)bootutil_keys[key_id].key;
    end = pubkey + *bootutil_keys[key_id].len;
    key_len = end - pubkey;
#endif /* USE_LMS */

    if (pubkey == NULL || end == NULL){
        FIH_RET(FIH_FAILURE);
    }

/* Verification for lms signature */
#if defined(USE_LMS)
    FIH_CALL(pq_crypto_verify, fih_rc, pubkey, key_len, hash, hlen, signature, sizeof(signature));
#endif /*USE_LMS*/

#if defined(USE_CRYPTOLITE_ECDSA)
    /* ECDSA verification using Cryptolite hardware acceleration */
    FIH_CALL(ecdsa_cryptolite_verify_signature, fih_rc, hash, hlen, signature,
             sizeof(signature), pubkey, key_len);
#endif /* USE_CRYPTOLITE_ECDSA */

    FIH_RET(fih_rc);
}
