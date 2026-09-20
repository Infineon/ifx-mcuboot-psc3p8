/*******************************************************************************
* \file cryptolite_ecdsa.c
* \version 1.0
*
* This file provides ECDSA signature verification using Cryptolite hardware.
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

#if defined(USE_CRYPTOLITE_ECDSA)

#include <string.h>
#include <cmsis_compiler.h>
#include "ecdsa_cryptolite.h"
#include "bootutil/bootutil_log.h"
#include "cy_syslib.h"


/*******************************************************************************
* Function Name: ecdsa_cryptolite_verify_signature
********************************************************************************
* MCUBoot-compatible ECDSA signature verification using Cryptolite hardware acceleration
*
* This function adapts MCUBoot's signature verification interface to use
* the Cryptolite hardware-accelerated ECDSA implementation.
*
* \param hash      Pointer to the hash of the data to verify (SHA-256/384/512)
* \param hlen      Length of the hash in bytes (32/48/64 respectively)
* \param sig       Pointer to the signature in (r,s) format from ASN.1 decoding
* \param slen      Length of the signature in bytes (64/96/132 respectively)
* \param pubkey    Pointer to the public key including 0x04 prefix from ASN.1 decoding
* \param key_len   Length of the public key in bytes (65/97/133 respectively)
*
* \return fih_int  FIH_SUCCESS if signature is valid, FIH_FAILURE otherwise
*******************************************************************************/
fih_int ecdsa_cryptolite_verify_signature(uint8_t *hash, uint32_t hlen,
                                          uint8_t *sig, size_t slen,
                                          uint8_t *pubkey, size_t key_len)
{
    fih_int fih_rc = FIH_FAILURE;
    cy_en_cryptolite_ecc_curve_id_t curve_id;
    (void)key_len;

    /* Validate input parameters */
    if (hash == NULL || sig == NULL || pubkey == NULL) {
        BOOT_LOG_ERR("Verifying signature with NULL pointer input parameters");
        FIH_RET(FIH_FAILURE);
    }

#if defined(USE_SHA256)
    curve_id = CY_CRYPTOLITE_ECC_ECP_SECP256R1;
#elif defined(USE_SHA384)
    curve_id = CY_CRYPTOLITE_ECC_ECP_SECP384R1;
#elif defined(USE_SHA512)
    curve_id = CY_CRYPTOLITE_ECC_ECP_SECP521R1;
#else
    curve_id = CY_CRYPTOLITE_ECC_ECP_UNKNOWN;
#endif

    BOOT_LOG_DBG("ECDSA verifying signature with curve ID %d, hash_len=%d, sig_len=%d",
                 (int)curve_id, (int)hlen, (int)slen);

    /* Call the Cryptolite ECDSA verification function */
    FIH_CALL(ecdsa_cryptolite_signature_validate, fih_rc, curve_id, hash, hlen,
             &pubkey[PUB_KEY_OFFSET_CRYPTOLITE], sig);

    FIH_RET(fih_rc);
}

/*******************************************************************************
* Function Name: ecdsa_cryptolite_signature_validate
****************************************************************************//**
* Validates a digital signature with ECDSA algorithm using Cryptolite hardware.
*
* \param curve_id   ECDSA curve ID, \ref cy_en_cryptolite_ecc_curve_id_t
* \param hash       The pointer to the SHA for the message to be authenticated.
* \param hash_len   A length of SHA hash in bytes.
* \param pub_key    The pointer to the public key (X,Y coordinates without 0x04 prefix).
* \param sign       The pointer to the digital signature for the message (R,S format).
*
* \return
* FIH_SUCCESS if signature is valid, FIH_FAILURE otherwise
*******************************************************************************/
fih_int ecdsa_cryptolite_signature_validate(cy_en_cryptolite_ecc_curve_id_t curve_id,
                                            uint8_t *hash, uint32_t hash_len,
                                            uint8_t *pub_key, uint8_t *sign)
{
    volatile fih_int ret = FIH_FAILURE;
    cy_en_cryptolite_status_t init_ret = CY_CRYPTOLITE_BAD_PARAMS;
    cy_stc_cryptolite_ecc_buffer_t ecc_buffer = {0};
    cy_stc_cryptolite_context_ecdsa_t ctx = {0};

    /* size in bytes for big-integer values - pubkey.{x,y}, sign.{r,s} */
    uint32_t num_len = 0UL;
    switch (curve_id)
    {
    case CY_CRYPTOLITE_ECC_ECP_SECP256R1:
        num_len = 32UL;
        break;
    case CY_CRYPTOLITE_ECC_ECP_SECP384R1:
        num_len = 48UL;
        break;
    case CY_CRYPTOLITE_ECC_ECP_SECP521R1:
        num_len = 66UL;
        break;
    default:
        BOOT_LOG_ERR("Unsupported curve_id=%d", curve_id);
        break;
    }

    if (0UL == num_len)
    {
        FIH_RET(FIH_FAILURE);
    }

    init_ret = Cy_Cryptolite_ECC_Init(CRYPTOLITE, &ctx, &ecc_buffer);
    if (CY_CRYPTOLITE_SUCCESS == init_ret)
    {
        volatile cy_en_cryptolite_sig_verify_result_t sign_valid = CY_CRYPTOLITE_SIG_INVALID;
        volatile cy_en_cryptolite_status_t verify_ret = CY_CRYPTOLITE_BAD_PARAMS;

        /* Create local copies to avoid modifying input data stored in flash */
        uint8_t local_pubkey_x[MAX_ECC_COORDINATE_SIZE_CRYPTOLITE];
        uint8_t local_pubkey_y[MAX_ECC_COORDINATE_SIZE_CRYPTOLITE];
        uint8_t local_signature[MAX_SIGNATURE_SIZE_CRYPTOLITE];

        /* Declare the ECC key structure using local copies to avoid flash modification*/
        const cy_stc_cryptolite_ecc_key ecc_pub_key =
        {
            .type = PK_PUBLIC,
            .curveID = curve_id,
            {
                .x = (void *) local_pubkey_x,
                .y = (void *) local_pubkey_y,
            },
            .k = NULL
        };

        /* Copy data to local buffers - use 66 bytes for P-521 */
        memcpy(local_pubkey_x, &pub_key[0], num_len);
        memcpy(local_pubkey_y, &pub_key[num_len], num_len);
        memcpy(local_signature, sign, 2UL * num_len);

        /* Invert endianness for local copy of public key */
        Cy_Cryptolite_InvertEndianness(local_pubkey_x, num_len);
        Cy_Cryptolite_InvertEndianness(local_pubkey_y, num_len);
        /* ECDSA components: R and S, each requiring separate endianness inversion */
        Cy_Cryptolite_InvertEndianness(local_signature, num_len);
        Cy_Cryptolite_InvertEndianness(&local_signature[num_len], num_len);
        /* Invert endianness for hash */
        Cy_Cryptolite_InvertEndianness(hash, hash_len);

        /* Invoke Cryptolite hardware accelerator for ECDSA signature verification */
        verify_ret = Cy_Cryptolite_ECC_VerifyHash(CRYPTOLITE, &ctx, local_signature,
                                                  2UL * num_len, hash, hash_len,
                                                  (cy_en_cryptolite_sig_verify_result_t *)&sign_valid,
                                                  &ecc_pub_key);

        BOOT_LOG_DBG("ECDSA verification result: verify_ret=0x%08x, sign_valid=0x%08x",
                     verify_ret, sign_valid);

        /* Enhanced fault injection hardening for ECDSA verification result */
        if ((CY_CRYPTOLITE_SUCCESS == verify_ret) && (CY_CRYPTOLITE_SIG_VALID == sign_valid))
        {
#if defined(FIH_ENABLE_DOUBLE_VARS)
            ret.val = FIH_SUCCESS.val;
#endif /* FIH_ENABLE_DOUBLE_VARS */

#if defined(MCUBOOT_FIH_PROFILE_ON)            
            /* Call Random Delay */
            (void)fih_delay();
#endif /* MCUBOOT_FIH_PROFILE_ON */

            /* Second verification with different comparison order */
            if ((CY_CRYPTOLITE_SIG_VALID == sign_valid) && (CY_CRYPTOLITE_SUCCESS == verify_ret))
            {
#if defined(FIH_ENABLE_DOUBLE_VARS)
                ret.msk = FIH_SUCCESS.msk;
#else
                ret = FIH_SUCCESS;
#endif /* FIH_ENABLE_DOUBLE_VARS */
            }
        }
    }

    /* Clear buffer regardless status of previous calls. Ignore the status
    * because CY_CRYPTOLITE_SUCCESS is returned if function input parameters
    * are valid (the code above guarantees that input parameters are valid).
    */
    (void) Cy_Cryptolite_ECC_Free(CRYPTOLITE, &ctx);

    /* Revert hash again */
    Cy_Cryptolite_InvertEndianness(hash, hash_len);

    FIH_RET(ret);
}

#endif /* defined(USE_CRYPTOLITE_ECDSA) */

/*
 * Avoid warning from -pedantic. This is included
 * because ISO C forbids an empty translation unit.
 */
typedef int cryptolite_iso_c_forbids_empty_translation_units;


/* [] END OF FILE */
