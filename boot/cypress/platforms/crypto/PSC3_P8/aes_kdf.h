/*******************************************************************************
* \file aes_kdf.h
* \version 1.0
*
* This module provides CS-PSC3X8 KDF-CMAC encryption support for MCUBoot.
* This includes AES-CTR operations using CS-PSC3X8 hardware acceleration.
********************************************************************************
* \copyright
* Copyright (c) 2025-2026, Infineon Technologies AG, or an affiliate of
* Infineon Technologies AG. All rights reserved.
*
* This software, associated documentation and materials ("Software") is owned by
* Infineon Technologies AG or one of its affiliates ("Infineon") and is protected
* by and subject to worldwide patent protection, worldwide copyright laws, and
* international treaty provisions. Therefore, you may use this Software only as
* provided in the license agreement accompanying the software package from which
* you obtained this Software. If no license agreement applies, then any use,
* reproduction, modification, translation, or compilation of this Software is
* prohibited without the express written permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE IS
* PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING,
* BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF THIRD-PARTY RIGHTS
* AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A SPECIFIC USE/PURPOSE
* OR MERCHANTABILITY. Infineon reserves the right to make changes to the Software
* without notice. You are responsible for properly designing, programming, and
* testing the functionality and safety of your intended application of the
* Software, as well as complying with any legal requirements related to its use.
* Infineon does not guarantee that the Software will be free from intrusion,
* data theft or loss, or other breaches ("Security Breaches"), and Infineon
* shall have no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

#ifndef BOOTUTIL_CRYPTO_AES_KDF_H
#define BOOTUTIL_CRYPTO_AES_KDF_H

#include "mcuboot_config/mcuboot_config.h"

#ifdef USE_ENCRYPTION_KDF_CTR

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOTUTIL_CRYPTO_AES_CTR_KEY_SIZE BOOT_ENC_KEY_SIZE
#define BOOTUTIL_CRYPTO_AES_CTR_BLOCK_SIZE (16U)

int kbkdf_cmac_crypto_init(void);
void kbkdf_cmac_crypto_deinit(void);
int kbkdf_cmac_derive_key(const uint8_t *const ikm, const uint16_t ikm_len,
                          const uint8_t *const label, const uint16_t label_len,
                          const uint8_t *const context, const uint16_t context_len,
                          uint8_t *okm, uint16_t *okm_len);

int kbkdf_cmac_trng_generate(uint8_t *seed, uint8_t *seed_inv, uint8_t offset);

#ifdef __cplusplus
}
#endif

#endif /* USE_ENCRYPTION_KDF_CTR */

#endif /* BOOTUTIL_CRYPTO_AES_KDF_H */
