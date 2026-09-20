/*******************************************************************************
* \file sha_port.h
* \version 2.0
*
* This file provides the port layer for SHA MCUBoot functions using Cryptolite.
* Adapts Cryptolite SHA implementation to MCUBoot's expected interface.
********************************************************************************
* \copyright
* Copyright (c) 2023-2025, Infineon Technologies AG, or an affiliate of 
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

#pragma once

#include "cyboot_flash_list.h"
#include "cy_cryptolite_common.h"
#include "cy_cryptolite_sha.h"
#include "crypto.h"

#if defined(USE_SHA256)
#define BOOTUTIL_CRYPTO_SHA_BLOCK_SIZE (64)
#define BOOTUTIL_CRYPTO_SHA_DIGEST_SIZE (32)
#elif defined(USE_SHA384)
#define BOOTUTIL_CRYPTO_SHA_BLOCK_SIZE (128)
#define BOOTUTIL_CRYPTO_SHA_DIGEST_SIZE (48)
#elif defined(USE_SHA512)
#define BOOTUTIL_CRYPTO_SHA_BLOCK_SIZE (128)
#define BOOTUTIL_CRYPTO_SHA_DIGEST_SIZE (64)
#endif

typedef cy_stc_cryptolite_context_sha_t bootutil_sha_context;

static inline int bootutil_sha_init(bootutil_sha_context *ctx)
{
    int ret = -1;
    cy_en_cryptolite_status_t crt_ret = CY_CRYPTOLITE_BAD_PARAMS;

    if (NULL != ctx)
    {
#if defined(USE_SHA256)
        crt_ret = Cy_Cryptolite_Sha_Init(CRYPTOLITE, CY_CRYPTOLITE_MODE_SHA256, ctx);
#elif defined(USE_SHA384)
        crt_ret = Cy_Cryptolite_Sha_Init(CRYPTOLITE, CY_CRYPTOLITE_MODE_SHA384, ctx);
#elif defined(USE_SHA512)
        crt_ret = Cy_Cryptolite_Sha_Init(CRYPTOLITE, CY_CRYPTOLITE_MODE_SHA512, ctx);
#endif

        if (CY_CRYPTOLITE_SUCCESS == crt_ret)
        {
            crt_ret = Cy_Cryptolite_Sha_Start(CRYPTOLITE, ctx);
        }
    }

    if (CY_CRYPTOLITE_SUCCESS == crt_ret) {
        ret = 0;
    }

    return ret;
}

static inline void bootutil_sha_drop(bootutil_sha_context *ctx)
{
    (void)ctx;
}

static inline int bootutil_sha_update(bootutil_sha_context *ctx,
                                         const void *data,
                                         uint32_t data_len)
{
    int ret = -1;
    cy_en_cryptolite_status_t crt_ret = CY_CRYPTOLITE_BAD_PARAMS;

    if ((NULL != ctx) && (NULL != data))
    {
        crt_ret = Cy_Cryptolite_Sha_Update(CRYPTOLITE, data, data_len, ctx);
    }
    
    if (CY_CRYPTOLITE_SUCCESS == crt_ret) {
        ret = 0;
    }

    return ret;
}

static inline int bootutil_sha_finish(bootutil_sha_context *ctx,
                                         uint8_t *output)
{
    int ret = -1;
    cy_en_cryptolite_status_t crt_ret = CY_CRYPTOLITE_BAD_PARAMS;

    if ((NULL != ctx ) && (NULL != output))
    {
        crt_ret = Cy_Cryptolite_Sha_Finish(CRYPTOLITE, (uint8_t *)output, ctx);
        if (CY_CRYPTOLITE_SUCCESS == crt_ret)
        {
            crt_ret = Cy_Cryptolite_Sha_Free(CRYPTOLITE, ctx);
        }
    }

    if (CY_CRYPTOLITE_SUCCESS == crt_ret) {
        ret = 0;
    }

    return ret;
}

/*
 * SHA-256 specific wrappers - always use SHA-256 regardless of the
 * compile-time hash algorithm selection (USE_SHA256/384/512).
 */
static inline int bootutil_sha256_init(bootutil_sha_context *ctx)
{
    int ret = -1;
    cy_en_cryptolite_status_t crt_ret = CY_CRYPTOLITE_BAD_PARAMS;

    if (NULL != ctx)
    {
        crt_ret = Cy_Cryptolite_Sha_Init(CRYPTOLITE, CY_CRYPTOLITE_MODE_SHA256, ctx);

        if (CY_CRYPTOLITE_SUCCESS == crt_ret)
        {
            crt_ret = Cy_Cryptolite_Sha_Start(CRYPTOLITE, ctx);
        }
    }

    if (CY_CRYPTOLITE_SUCCESS == crt_ret) {
        ret = 0;
    }

    return ret;
}

#define bootutil_sha256_update  bootutil_sha_update
#define bootutil_sha256_finish  bootutil_sha_finish
#define bootutil_sha256_drop    bootutil_sha_drop
