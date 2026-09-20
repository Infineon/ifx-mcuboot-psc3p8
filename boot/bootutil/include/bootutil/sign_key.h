/*
 * SPDX-License-Identifier: Apache-2.0
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

#ifndef BOOTUTIL_SIGN_KEY_H
#define BOOTUTIL_SIGN_KEY_H

#include <stddef.h>
#include <stdint.h>

/* mcuboot_config.h is needed for MCUBOOT_HW_KEY to work */
#include "mcuboot_config/mcuboot_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(USE_CRYPTOLITE_ECDSA) || defined(USE_ROM_CRYPTO_ECDSA)
#if defined(USE_SHA256)
/* ECDSA256 related defines */
#if !defined(NUM_ECC_BYTES)
#define NUM_ECC_BYTES (256U / 8U)
#endif /* NUM_ECC_BYTES */
#define EC_KEY_SZ (138)
#elif defined(USE_SHA384)
/* ECDSA384 related defines */
#if !defined(NUM_ECC_BYTES)
#define NUM_ECC_BYTES (384U / 8U)
#endif /* NUM_ECC_BYTES */
#define EC_KEY_SZ (207)
#elif defined(USE_SHA512)
/* ECDSA521 related defines */
#if !defined(NUM_ECC_BYTES)
#define NUM_ECC_BYTES (66U)
#endif /* NUM_ECC_BYTES */
#define EC_KEY_SZ (284)
#endif /* USE_SHA256 */
#endif /* USE_CRYPTOLITE_ECDSA || USE_ROM_CRYPTO_ECDSA */

#if !defined(MCUBOOT_HW_KEY)
#if defined(SFLASH_KEYS)
struct bootutil_key {
    uint8_t *key;
    unsigned int *len;
};

extern struct bootutil_key bootutil_keys[];
#else /* SFLASH_KEYS */
struct bootutil_key {
    const uint8_t *key;
    const unsigned int *len;
};

extern const struct bootutil_key bootutil_keys[];
#endif /* SFLASH_KEYS */
#else /* !MCUBOOT_HW_KEY */
struct bootutil_key {
    uint8_t *key;
    unsigned int *len;
};

extern struct bootutil_key bootutil_keys[];

/**
 * Retrieve the hash of the corresponding public key for image authentication.
 *
 * @param[in]      image_index      Index of the image to be authenticated.
 * @param[out]     public_key_hash  Buffer to store the key-hash in.
 * @param[in,out]  key_hash_size    As input the size of the buffer. As output
 *                                  the actual key-hash length.
 *
 * @return                          0 on success; nonzero on failure.
 */
int boot_retrieve_public_key_hash(uint8_t image_index,
                                  uint8_t *public_key_hash,
                                  size_t *key_hash_size);
#endif /* !MCUBOOT_HW_KEY */

extern const int bootutil_key_cnt;

#if defined(SFLASH_KEYS)
/**
 * Convert RAW EC public key (X9.62 format) to DER format (SubjectPublicKeyInfo).
 * Used when SFLASH stores RAW keys but IMAGE_TLV_KEYHASH validation requires DER format.
 *
 * @param[out]  der_key      Buffer to store the DER-encoded key (must be at least EC_KEY_SZ)
 * @param[out]  der_key_len  Pointer to store the actual DER key length
 * @param[in]   raw_key      Input RAW EC public key (0x04 + X + Y coordinates)
 * @param[in]   raw_key_len  Length of the RAW key (65 for EC256, 97 for EC384, 133 for EC521)
 *
 * @return                   0 on success; negative error code on failure
 */
int build_der_key_with_mbedtls(uint8_t *der_key, size_t *der_key_len,
                                const uint8_t *raw_key, size_t raw_key_len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BOOTUTIL_SIGN_KEY_H */
