/*******************************************************************************
* \file cryptolite_ecdsa.h
* \version 1.0
*
* This file provides the declarations for ECDSA cryptography using Cryptolite.
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

#ifndef CRYPTOLITE_ECDSA_H
#define CRYPTOLITE_ECDSA_H

#if defined(USE_CRYPTOLITE_ECDSA)

#include <string.h>
#include "cy_cryptolite_ecdsa.h"
#include "cy_cryptolite_utils.h"
#include "mcuboot_config/mcuboot_config.h"
#include "bootutil/fault_injection_hardening.h"


/*******************************************************************************
*                            ECDSA Defines
*******************************************************************************/

/* Buffer sizes based on configured hash algorithm */
#if defined(USE_SHA256) || defined(USE_SHA384) || defined(USE_SHA512)
    #define MAX_ECC_COORDINATE_SIZE_CRYPTOLITE  (CY_CRYPTOLITE_ECC_P521_BYTE_SIZE)
    #define MAX_SIGNATURE_SIZE_CRYPTOLITE       (CY_CRYPTOLITE_ECC_P521_BYTE_SIZE * 2U)
#else
    #error "Hash algorithm must be defined for buffer sizing (USE_SHA256, USE_SHA384, or USE_SHA512)"
#endif

/* MCUBoot stores ECDSA public keys in uncompressed format: key_offset + X + Y coordinates
* - P-256: 1 + 32 + 32 = 65 bytes (key_offset + 64 bytes coordinates)
* - P-384: 1 + 48 + 48 = 97 bytes (key_offset + 96 bytes coordinates)
* - P-521: 1 + 66 + 66 = 133 bytes (key_offset + 132 bytes coordinates)
* Cryptolite expects only the coordinate data (X + Y) without the key_offset */
#define PUB_KEY_OFFSET_CRYPTOLITE (0x1U)

/*******************************************************************************
*                            Function Declarations
*******************************************************************************/

/* Low-level ECDSA signature validation using Cryptolite hardware */
fih_int ecdsa_cryptolite_signature_validate(cy_en_cryptolite_ecc_curve_id_t curve_id,
                                            uint8_t *hash, uint32_t hash_len,
                                            uint8_t *pub_key, uint8_t *sign);

/* MCUBoot-compatible ECDSA signature verification using Cryptolite */
fih_int ecdsa_cryptolite_verify_signature(uint8_t *hash, uint32_t hlen,
                                          uint8_t *sig, size_t slen,
                                          uint8_t *pubkey, size_t key_len);

#endif /* defined(USE_CRYPTOLITE_ECDSA) */

#endif /* CRYPTOLITE_ECDSA_H */

/* [] END OF FILE */