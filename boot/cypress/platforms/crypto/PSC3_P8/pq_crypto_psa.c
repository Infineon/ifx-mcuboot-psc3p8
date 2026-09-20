/*******************************************************************************
* \file pq_crypto_psa.c
* \version 1.0
*
* This file provides post-quantum cryptography verification implementation
* using LMS (Leighton-Micali Signature) for MCUBoot integration.
* Adapted for MCUBoot integration in PSC3_P8 platform.
********************************************************************************
* \copyright
* Copyright (c) 2025, Infineon Technologies AG, or an affiliate of
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

#if defined(USE_LMS)

#include "pq_crypto.h"
#include "lms.h"

fih_int pq_crypto_verify(uint8_t *key, size_t key_size,
                         const uint8_t *data,
                         size_t data_length,
                         const uint8_t *signature,
                         size_t signature_length)
{
    int rc;
    fih_int fih_rc;
    mbedtls_lms_public_t ctx;

    mbedtls_lms_public_init(&ctx);

    rc = mbedtls_lms_import_public_key(&ctx, key, key_size);
    fih_rc = fih_int_encode_zero_equality(rc);
    if (fih_not_eq(fih_rc, FIH_SUCCESS)) {
        goto out;
    }

    rc = mbedtls_lms_verify(&ctx, data, data_length, signature, signature_length);
    fih_rc = fih_int_encode_zero_equality(rc);

out:
    mbedtls_lms_public_free(&ctx);
    FIH_RET(fih_rc);
}

#endif /* USE_LMS */

/*
 * Avoid warning from -pedantic. This is included
 * because ISO C forbids an empty translation unit.
 */
typedef int lms_iso_c_forbids_empty_translation_units;