/*******************************************************************************
* \file cs_encrypted.c
* \version 1.0
*
* This file provides CS-PSC3X8 hardware-accelerated encryption implementation
* for MCUBoot using KDF-CTR with CMAC on PSC3_P8 platform.
*
* This implementation is specific to USE_ENCRYPTION_KDF_CTR and leverages
* CS-PSC3X8 hardware crypto entities (AES-128, CMAC, KDF-CTR).
*
* For standard MCUBoot encryption schemes (EC256, RSA, X25519), use the
* generic encrypted.c implementation in boot/bootutil/src/encrypted.c
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

#include "mcuboot_config/mcuboot_config.h"

#if defined(USE_ENCRYPTION_KDF_CTR)
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "bootutil/bootutil_log.h"
#include "bootutil/image.h"

#include "bootutil/enc_key.h"
#include "bootutil/enc_key_public.h"
#include "bootutil/sign_key.h"
#include "bootutil/crypto/common.h"
#include "bootutil_priv.h"
#include <assert.h>

/* Hardware implementation: Include CS-PSC3X8 crypto headers */
#include "Cs_StdApi.h"
#include "Cs_XBlob_Api.h"
#include "Cs_Sym_Kdf_KdfCtr.h"
#include "Cs_Sym_Mac_Ciphermac_Cmac.h"
#include "Cs_Sym_Cipher_Aes.h"

/* Include TRNG support from platform */
#include "cy_cryptolite_trng.h"

/* KDF-CMAC TLV structure indexes - EdgeProtectTools format (Updated v.61)
 * TLV 0x102 now contains: Salt (16 bytes) + IV (16 bytes) = 32 bytes total
 */
#define KDF_CMAC_SALT_INDEX         0   /* Salt starts at offset 0 */
#define KDF_CMAC_SALT_SIZE          16  /* Salt is 16 bytes */
#define KDF_CMAC_IV_INDEX           16  /* IV starts at offset 16 */
#define KDF_CMAC_IV_SIZE            16  /* IV is 16 bytes */
#define EXPECTED_KDF_CMAC_LEN       32  /* Total: Salt(16) + IV(16) */

/* KDF-CMAC Error Codes (shared by both software and hardware implementations) */
#define KDF_ERR_INVALID_PARAM       -1  /* NULL pointer or invalid parameter */
#define KDF_ERR_ZERO_LENGTH         -2  /* Zero length input or output */
#define KDF_ERR_INIT_FAILED         -3  /* Crypto entity initialization failed */
#define KDF_ERR_AES_CFG_FAILED      -4  /* AES configuration failed */
#define KDF_ERR_CMAC_CFG_FAILED     -5  /* CMAC configuration failed */
#define KDF_ERR_KDF_CFG_FAILED      -6  /* KDF configuration failed */
#define KDF_ERR_DERIVATION_FAILED   -7  /* Key derivation operation failed */
#define KDF_ERR_DECRYPT_FAILED      -8  /* Decryption operation failed */
#define KDF_ERR_AUTH_FAILED         -9  /* Authentication tag verification failed */
#define KDF_ERR_HW_NOT_AVAILABLE    -10 /* Hardware crypto not available (software-only build) */

/* Memory allocation for CS-PSC3X8 entities */
#define CS_KDF_CTR_HEAP_SIZE    CS_ENTITY_KDFCTR_SEC_HEAPSIZE
#define CS_CMAC_HEAP_SIZE       CS_ENTITY_CMAC_SEC_HEAPSIZE
#define CS_AES_HEAP_SIZE        CS_ENTITY_AES128_SEC_HEAPSIZE

/* Hardware implementation: Static memory buffers for CS-PSC3X8 entities */
static uint8_t cs_kdf_heap_memory[CS_KDF_CTR_HEAP_SIZE] __attribute__((aligned(8)));
static uint8_t cs_cmac_heap_memory[CS_CMAC_HEAP_SIZE] __attribute__((aligned(8)));
static uint8_t cs_aes_heap_memory[CS_AES_HEAP_SIZE] __attribute__((aligned(8)));

/* Hardware implementation: Temporary memory for operations */
#define CS_TEMP_MEM_SIZE    1536
static uint8_t cs_temp_memory[CS_TEMP_MEM_SIZE] __attribute__((aligned(8)));

/* CS-PSC3X8 entity handles (v6.0.9: now struct values, not pointers) */
static Cs_StdApi_HandleType cs_kdf_handle = {NULL, NULL};
static Cs_StdApi_HandleType cs_cmac_handle = {NULL, NULL};
static Cs_StdApi_HandleType cs_aes_handle = {NULL, NULL};

static Cs_StdApi_OpenType aes_open_params;
static Cs_StdApi_OpenType cmac_open_params;
static Cs_StdApi_OpenType kdf_open_params;

/**
 * @brief Generate RNG seeds for CS-PSC3X8 crypto entities using hardware TRNG
 * 
 * Generates a random seed buffer of the specified size using the PSC3 hardware
 * True Random Number Generator. This function provides cryptographically secure
 * random seeds for CS-PSC3X8 crypto operations, ensuring DPA/SPA side-channel
 * attack resistance.
 * 
 * @param seed      Output buffer for RNG seed (must be at least `size` bytes)
 * @param seed_inv  Output buffer for inverted seed (must be at least `size` bytes, seed XOR 0xFF)
 * @param size      Number of bytes to generate (8 for AES/CMAC seeds, 16 for KDF seed)
 * 
 * Implementation:
 *   - Uses Cy_Cryptolite_Trng hardware random number generator
 *   - Generates `size` bytes via successive 32-bit TRNG reads
 *   - Automatically creates inverted seed for CS library integrity check
 * 
 * @return 0 on success, -1 on TRNG failure
 */
int kbkdf_cmac_trng_generate(uint8_t *seed, uint8_t *seed_inv, uint8_t size)
{
    uint32_t random;
    cy_en_cryptolite_status_t result;
    size_t num_words;

    /* Calculate number of 32-bit words needed */
    num_words = (size + 3) / 4;  /* Round up to nearest word */
    
    result = CY_CRYPTOLITE_HW_ERROR;

    result = Cy_Cryptolite_Trng_Init(CRYPTOLITE, NULL);
    if (result != CY_CRYPTOLITE_SUCCESS) {
        BOOT_LOG_ERR("TRNG Init failed: 0x%lx", (unsigned long)result);
        return -1;
    }

    result = Cy_Cryptolite_Trng_Enable(CRYPTOLITE);
    if (result != CY_CRYPTOLITE_SUCCESS) {
        BOOT_LOG_ERR("TRNG Enable failed: 0x%lx", (unsigned long)result);
        return -1;
    }

    /* Generate requested bytes of random data */
    for (size_t i = 0; i < num_words; i++) {
        result = Cy_Cryptolite_Trng_ReadData(CRYPTOLITE, &random);
        if (result != CY_CRYPTOLITE_SUCCESS) {
            BOOT_LOG_ERR("TRNG ReadData failed: 0x%lx", (unsigned long)result);
            (void)Cy_Cryptolite_Trng_Disable(CRYPTOLITE);
            (void)Cy_Cryptolite_Trng_DeInit(CRYPTOLITE);
            return -1;
        }

        /* Copy up to 4 bytes to seed buffer (handle partial last word) */
        for (size_t byte = 0; byte < 4 && (i*4 + byte) < size; byte++) {
            seed[i*4 + byte] = (uint8_t)(random >> (byte * 8));
        }
    }

    (void)Cy_Cryptolite_Trng_Disable(CRYPTOLITE);
    (void)Cy_Cryptolite_Trng_DeInit(CRYPTOLITE);

    /* Generate inverted seed (integrity check for CS library) */
    for (size_t j = 0; j < size; j++) {
        seed_inv[j] = seed[j] ^ 0xFF;
    }

    return 0;
}

/**
 * @brief Initialize CS-PSC3X8 crypto entities for KDF-CMAC encryption
 * 
 * KDF-CMAC Flow Role:
 * This function initializes the hardware crypto entities required for the complete
 * encryption flow shown in the scheme:
 * 
 * Step 1: Open AES-128 entity
 *   - Used by CMAC as the underlying block cipher
 *   - Later used for AES-CTR decryption of firmware
 * 
 * Step 2: Open CMAC entity
 *   - Used as PRF (Pseudo-Random Function) in KDF-CTR
 *   - Computes: CMAC(Master_Key, Counter || Label || 0x00 || Salt || Length)
 * 
 * Step 3: Open KDF-CTR entity
 *   - Implements NIST SP 800-108 key derivation
 *   - Derives DEK (Data Encryption Key) from Master Key + Salt
 * 
 * The opened entities are used in this sequence:
 *   1. boot_enc_load() → calls boot_enc_decrypt() → KDF derives DEK
 *   2. boot_enc_set_key() → configures AES with derived DEK
 *   3. boot_encrypt() → decrypts firmware using DEK + IV
 * 
 * Memory Management:
 * - Uses static heap buffers (aligned to 8 bytes) for entity state
 * - OpenType structures MUST be static to persist after Cs_Open() returns
 * 
 * @return 0 on success, KDF_ERR_INIT_FAILED on failure
 */
int kbkdf_cmac_crypto_init(void)
{
    /* Open AES entity */
    memset(&aes_open_params, 0, sizeof(aes_open_params));
    aes_open_params.Super.OID = CS_ENTITY_AES128_SEC_OID;
    aes_open_params.EntityFnPtr = Cs_Entity_Aes128_Sec;
    aes_open_params.HeapMemPtr = cs_aes_heap_memory;
    aes_open_params.HeapMemSize = CS_AES_HEAP_SIZE;
    aes_open_params.OsHwEntryCallbackFnPtr = NULL;
    aes_open_params.OsHwExitCallbackFnPtr = NULL;

    cs_aes_handle = Cs_Open(&aes_open_params);
    if (cs_aes_handle.FirstPtr == NULL) {
        BOOT_LOG_ERR("AES Cs_Open failed");
        return KDF_ERR_INIT_FAILED;
    }

    /* Open CMAC entity */
    memset(&cmac_open_params, 0, sizeof(cmac_open_params));
    cmac_open_params.Super.OID = CS_ENTITY_CMAC_SEC_OID;
    cmac_open_params.EntityFnPtr = Cs_Entity_Cmac_Sec;
    cmac_open_params.HeapMemPtr = cs_cmac_heap_memory;
    cmac_open_params.HeapMemSize = CS_CMAC_HEAP_SIZE;
    cmac_open_params.OsHwEntryCallbackFnPtr = NULL;
    cmac_open_params.OsHwExitCallbackFnPtr = NULL;

    cs_cmac_handle = Cs_Open(&cmac_open_params);
    if (cs_cmac_handle.FirstPtr == NULL) {
        BOOT_LOG_ERR("CMAC Cs_Open failed");
        Cs_StdApi_CloseType close_cfg;
        memset(&close_cfg, 0, sizeof(close_cfg));
        close_cfg.Super.OID = CS_ENTITY_AES128_SEC_OID;
#if defined(USE_FUNC_ID)
        uint32_t close_func_id = CS_STDAPI_CLOSE_INPUT_FUNC_ID;
        close_cfg.FuncIDPtr = &close_func_id;
#endif /* USE_FUNC_ID */
        Cs_Close(cs_aes_handle, &close_cfg);
        return KDF_ERR_INIT_FAILED;
    }

    /* Open KDF-CTR entity */
    memset(&kdf_open_params, 0, sizeof(kdf_open_params));
    kdf_open_params.Super.OID = CS_ENTITY_KDFCTR_SEC_OID;
    kdf_open_params.EntityFnPtr = Cs_Entity_KdfCtr_Sec;
    kdf_open_params.HeapMemPtr = cs_kdf_heap_memory;
    kdf_open_params.HeapMemSize = CS_KDF_CTR_HEAP_SIZE;
    kdf_open_params.OsHwEntryCallbackFnPtr = NULL;
    kdf_open_params.OsHwExitCallbackFnPtr = NULL;

    cs_kdf_handle = Cs_Open(&kdf_open_params);
    if (cs_kdf_handle.FirstPtr == NULL) {
        BOOT_LOG_ERR("KDF Cs_Open failed");
        Cs_StdApi_CloseType close_cfg;
        memset(&close_cfg, 0, sizeof(close_cfg));
#if defined(USE_FUNC_ID)
        uint32_t close_func_id = CS_STDAPI_CLOSE_INPUT_FUNC_ID;
        close_cfg.FuncIDPtr = &close_func_id;
#endif /* USE_FUNC_ID */
        close_cfg.Super.OID = CS_ENTITY_CMAC_SEC_OID;
        Cs_Close(cs_cmac_handle, &close_cfg);
#if defined(USE_FUNC_ID)
        close_func_id = CS_STDAPI_CLOSE_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */
        close_cfg.Super.OID = CS_ENTITY_AES128_SEC_OID;
        Cs_Close(cs_aes_handle, &close_cfg);
        return KDF_ERR_INIT_FAILED;
    }

    return 0;
}

/**
 * @brief Deinitialize CS-PSC3X8 crypto entities
 * 
 * KDF-CMAC Flow Role:
 * Releases all crypto hardware resources after firmware decryption is complete.
 * Called by boot_enc_drop() to clean up the crypto pipeline.
 * 
 * Cleanup Order:
 * 1. Close KDF-CTR entity
 * 2. Close CMAC entity  
 * 3. Close AES entity
 * 
 * @note Safe to call multiple times - checks for NULL handles
 */
void kbkdf_cmac_crypto_deinit(void)
{
    Cs_StdApi_CloseType close_cfg;
    memset(&close_cfg, 0, sizeof(close_cfg));
#if defined(USE_FUNC_ID)
    uint32_t close_func_id = CS_STDAPI_CLOSE_INPUT_FUNC_ID;
    close_cfg.FuncIDPtr = &close_func_id;
#endif /* USE_FUNC_ID */

    /* Close KDF entity with specific OID */
    if (cs_kdf_handle.FirstPtr != NULL) {
        close_cfg.Super.OID = CS_ENTITY_KDFCTR_SEC_OID;
#if defined(USE_FUNC_ID)
        close_func_id = CS_STDAPI_CLOSE_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */
        Cs_Close(cs_kdf_handle, &close_cfg);
        cs_kdf_handle.FirstPtr = NULL;
        cs_kdf_handle.SecondPtr = NULL;
    }
    /* Close CMAC entity with specific OID */
    if (cs_cmac_handle.FirstPtr != NULL) {
        close_cfg.Super.OID = CS_ENTITY_CMAC_SEC_OID;
#if defined(USE_FUNC_ID)
        close_func_id = CS_STDAPI_CLOSE_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */
        Cs_Close(cs_cmac_handle, &close_cfg);
        cs_cmac_handle.FirstPtr = NULL;
        cs_cmac_handle.SecondPtr = NULL;
    }
    /* Close AES entity with specific OID */
    if (cs_aes_handle.FirstPtr != NULL) {
        close_cfg.Super.OID = CS_ENTITY_AES128_SEC_OID;
#if defined(USE_FUNC_ID)
        close_func_id = CS_STDAPI_CLOSE_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */
        Cs_Close(cs_aes_handle, &close_cfg);
        cs_aes_handle.FirstPtr = NULL;
        cs_aes_handle.SecondPtr = NULL;
    }
}

/**
 * @brief Derive encryption key using NIST SP 800-108 KDF in Counter Mode with CMAC
 * 
 * KDF-CMAC Flow Role:
 * This is the core key derivation function that transforms the Master Key into DEK.
 * 
 * Scheme Position:
 *   Input:  Master Key (from SFLASH) + Salt (from TLV 0x102[0:15])
 *   Output: DEK (128-bit derived key for AES-CTR decryption)
 * 
 * Algorithm: NIST SP 800-108 Counter Mode
 * ==========================================
 * PRF Input Format:
 *   Counter(4 BE) || Label(16) || 0x00 || Context(16) || Length(4 BE)
 * 
 * Where:
 *   - Counter: 32-bit big-endian counter starting at 0x00000001
 *   - Label: "KBKDF_AESCMAC_v1" (16 bytes, EdgeProtectTools fixed label)
 *   - 0x00: Fixed separator byte
 *   - Context: Salt from encrypted image (16 bytes from TLV 0x102)
 *   - Length: Output length in bits, big-endian (0x00000080 = 128 bits)
 * 
 * Derivation:
 *   DEK = CMAC-AES-128(Master_Key, PRF_Input)
 * 
 * CS-PSC3X8 Configuration:
 * - K0 Extension: OFF (0x8F5A) - ensures NIST SP 800-108 compliance
 * - Counter Size (R): 32 bits
 * - Output Length: 128 bits (16 bytes)
 * 
 * @param ikm        Input Key Material (Master Key from SFLASH)
 * @param ikm_len    Length of Master Key (16 bytes for AES-128)
 * @param label      KDF label ("KBKDF_AESCMAC_v1", 16 bytes)
 * @param label_len  Length of label (16 bytes)
 * @param context    KDF context (Salt from TLV, 16 bytes)
 * @param context_len Length of context (16 bytes)
 * @param okm        Output Key Material (derived DEK buffer)
 * @param okm_len    Pointer to output length (input: 16, output: actual derived length)
 * 
 * @return 0 on success, negative error code on failure
 */
int kbkdf_cmac_derive_key(const uint8_t *const ikm, const uint16_t ikm_len,
                          const uint8_t *const label, const uint16_t label_len,
                          const uint8_t *const context, const uint16_t context_len,
                          uint8_t *okm, uint16_t *okm_len)
{
    Cs_StdApi_StatusType status;
    Cs_XBlobType ikm_xblob;
    uint8_t aes_rng_seed[8];
    uint8_t aes_rng_seed_inv[8];
    Cs_Aes_CfgType aes_cfg;
    static uint8_t aes_tempmem[512];
    uint8_t rng_seed[8];
    uint8_t rng_seed_inv[8];
    Cs_Cmac_CfgType cmac_cfg;
    static uint8_t kdf_tempmem[2048];
    uint8_t kdf_rng_seed[16];      /* KDF requires 16-byte RNG seed */
    uint8_t kdf_rng_seed_inv[16];  /* KDF requires 16-byte RNG seed inverse */
    Cs_KdfCtr_CfgType kdf_cfg;
    Cs_XBlobType label_xblob;
    Cs_XBlobType context_xblob;
    Cs_XBlobType output_xblob;
    Cs_KdfCtr_CtrlType kdf_ctrl;

#if defined(USE_FUNC_ID)
    /* FuncID must NOT be static - CS library modifies these during configuration.
     * Static values cause integrity alerts on second image in multi-image boot. */
    uint32_t aes_func_id = CS_CIPHER_CFG_INPUT_FUNC_ID;
    uint32_t cmac_func_id = CS_MAC_CFG_INPUT_FUNC_ID;
    uint32_t kdf_func_id = CS_KDF_CFG_INPUT_FUNC_ID;
    uint32_t kdf_ctrl_func_id = CS_KDF_CTRL_DERIVE_KEY_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */

    status = CS_STATUS_ERROR_GENERIC;

    /* Validate input parameters */
    if (ikm == NULL || label == NULL || context == NULL || okm == NULL || okm_len == NULL) {
        BOOT_LOG_ERR("KDF: Invalid NULL parameter: ikm=%p, label=%p, context=%p, okm=%p, okm_len=%p",
                     (void *)ikm, (void *)label, (void *)context, (void *)okm, (void *)okm_len);
        return KDF_ERR_INVALID_PARAM;
    }

    if (*okm_len == 0 || ikm_len == 0) {
        BOOT_LOG_ERR("KDF: Zero length input or output: ikm_len=%u, okm_len=%u",
                     (unsigned)ikm_len, (unsigned)(*okm_len));
        return KDF_ERR_ZERO_LENGTH;
    }

    /* Prepare XBlob for input key material */
    memset(&ikm_xblob, 0, sizeof(ikm_xblob));
    ikm_xblob.Data1Ptr = (uint8_t *)ikm;
    ikm_xblob.ByteLen = ikm_len;
    ikm_xblob.Properties = (uint16_t)((CS_XBLOB_TYPE_BYTE_ARRAY << CS_XBLOB_TYPE_Pos)
                           | (CS_XBLOB_INTEGRITY_SHORT << CS_XBLOB_INTEGRITY_Pos));
    ikm_xblob.Checksum = Cs_XBlob_CalculateChecksum(&ikm_xblob,
                                                    CS_XBLOB_ID_DEFAULT,
                                                    CS_XBLOB_INTEGRITY_SHORT,
                                                    NULL);

    /* Prepare RNG seeds for AES configuration using hardware TRNG */
    if (kbkdf_cmac_trng_generate(aes_rng_seed, aes_rng_seed_inv, 8) != 0) {
        BOOT_LOG_ERR("Failed to generate AES RNG seed");
        return KDF_ERR_INIT_FAILED;
    }

    /* Configure AES entity */
    memset(&aes_cfg, 0, sizeof(aes_cfg));

    aes_cfg.Super.Super.Super.OID = CS_ENTITY_AES128_SEC_OID;
    aes_cfg.Super.Super.TempMemPtr = aes_tempmem;
    aes_cfg.Super.Super.TempMemSize = sizeof(aes_tempmem);
    aes_cfg.Super.Super.ClearTempMemOnExit = 0xFF;  /* Any Non-zero value */
    aes_cfg.Super.Super.CfgSelector.RngSeed = CS_STDAPI_CFG_RNG_SEED_ENABLE;
    aes_cfg.Super.Super.UnprivilegedCallbackFnPtr = NULL;
    aes_cfg.Super.Super.RngSeedPtr = aes_rng_seed;
    aes_cfg.Super.Super.RngSeedInvPtr = aes_rng_seed_inv;
#if defined(USE_FUNC_ID)
    aes_cfg.Super.Super.FuncIDPtr = &aes_func_id;
#endif /* USE_FUNC_ID */
    aes_cfg.Super.CipherCfgSelector.Key = CS_CIPHER_CFG_KEY_ENABLE;
    aes_cfg.Super.CipherCfgSelector.UseIKS = CS_CIPHER_CFG_USE_IKS_DISABLE;
    aes_cfg.Super.CipherCfgSelector.SecControl = CS_CIPHER_CFG_SEC_CONTROL_DISABLE;
    aes_cfg.Super.CipherCfgSelector.BlockRepetition = CS_CIPHER_CFG_BLOCK_REPETITION_DISABLE;
    aes_cfg.Super.KeyPtr = &ikm_xblob;

    status = Cs_Cfg(cs_aes_handle, (Cs_StdApi_CfgType *)&aes_cfg);
    if (status != CS_STATUS_SUCCESS) {
        BOOT_LOG_ERR("AES Cs_Cfg failed: %lu", (unsigned long)status);
        return KDF_ERR_AES_CFG_FAILED;
    }

    /* Prepare RNG seeds for CMAC configuration using hardware TRNG */
    if (kbkdf_cmac_trng_generate(rng_seed, rng_seed_inv, 8) != 0) {
        BOOT_LOG_ERR("Failed to generate CMAC RNG seed");
        return KDF_ERR_INIT_FAILED;
    }

    /* Configure CMAC entity with FULL configuration (key + AES handle)
     * This matches the crypto-suite example pattern */
    memset(&cmac_cfg, 0, sizeof(cmac_cfg));

    cmac_cfg.Super.Super.Super.Super.OID = CS_ENTITY_CMAC_SEC_OID;
    cmac_cfg.Super.Super.Super.TempMemPtr = NULL;
    cmac_cfg.Super.Super.Super.TempMemSize = 0;
    cmac_cfg.Super.Super.Super.ClearTempMemOnExit = 0;
    cmac_cfg.Super.Super.Super.CfgSelector.RngSeed = CS_STDAPI_CFG_RNG_SEED_ENABLE;
    cmac_cfg.Super.Super.Super.UnprivilegedCallbackFnPtr = NULL;
    cmac_cfg.Super.Super.Super.RngSeedPtr = rng_seed;
    cmac_cfg.Super.Super.Super.RngSeedInvPtr = rng_seed_inv;
#if defined(USE_FUNC_ID)
    cmac_cfg.Super.Super.Super.FuncIDPtr = &cmac_func_id;
#endif /* USE_FUNC_ID */
    cmac_cfg.Super.Super.MacCfgSelector.Key = CS_MAC_CFG_KEY_ENABLE;
    cmac_cfg.Super.Super.MacCfgSelector.CryptoHdl = CS_MAC_CFG_CRYPTO_HDL_ENABLE;
    cmac_cfg.Super.Super.KeyPtr = &ikm_xblob;
    cmac_cfg.Super.Super.CryptoHdl = cs_aes_handle;  /* Provide AES handle to CMAC */

    status = Cs_Cfg(cs_cmac_handle, (Cs_StdApi_CfgType *)&cmac_cfg);
    if (status != CS_STATUS_SUCCESS) {
        BOOT_LOG_ERR("CMAC Cs_Cfg failed: %lu", (unsigned long)status);
        return KDF_ERR_CMAC_CFG_FAILED;
    }

    if (kbkdf_cmac_trng_generate(kdf_rng_seed, kdf_rng_seed_inv, 16) != 0) {
        BOOT_LOG_ERR("Failed to generate KDF RNG seed");
        return KDF_ERR_INIT_FAILED;
    }

    /* Configure KDF-CTR entity using the standard structure */
    memset(&kdf_cfg, 0, sizeof(kdf_cfg));

    kdf_cfg.Super.Super.Super.OID = CS_ENTITY_KDFCTR_SEC_OID;
    kdf_cfg.Super.Super.TempMemPtr = kdf_tempmem;
    kdf_cfg.Super.Super.TempMemSize = sizeof(kdf_tempmem);
    kdf_cfg.Super.Super.ClearTempMemOnExit = 0xFF;  /* Any Non-zero value */
    kdf_cfg.Super.Super.CfgSelector.RngSeed = CS_STDAPI_CFG_RNG_SEED_ENABLE;
    kdf_cfg.Super.Super.UnprivilegedCallbackFnPtr = NULL;
    kdf_cfg.Super.Super.RngSeedPtr = kdf_rng_seed;
    kdf_cfg.Super.Super.RngSeedInvPtr = kdf_rng_seed_inv;
#if defined(USE_FUNC_ID)
    kdf_cfg.Super.Super.FuncIDPtr = &kdf_func_id;
#endif /* USE_FUNC_ID */
    kdf_cfg.Super.KdfCfgSelector.Key = 0xE4;                  /* ENABLE key input */
    kdf_cfg.Super.KdfCfgSelector.TotalOutputKeyLen = 0xE3;    /* ENABLE output length */
    kdf_cfg.Super.KdfCfgSelector.CryptoPrfHdl = 0xE2;         /* ENABLE PRF handle */
    kdf_cfg.Super.KdfCfgSelector.KdfCfgSelRfu4 = 0x1D;      /* RESERVED - must be 0x1D */

    /* Set parent Cs_Kdf_CfgType fields */
    kdf_cfg.Super.KeyPtr = &ikm_xblob;  /* KDF needs master key for derivation */
    kdf_cfg.Super.TotalOutputKeyLen = *okm_len;
    kdf_cfg.Super.CryptoPrfHdl = cs_cmac_handle;  /* Use CMAC as PRF (v6.0.9 may have fixed Workaround #2) */

    /* Configure KDF-CTR specific selectors (CS-PSC3X8-specific magic values) */
    
    /* K0 Extension Configuration
     * K0 Extension is a CS-PSC3X8 proprietary mechanism that uses different input format:
     *   K(0) = CMAC(K_IN, Label || 0x00 || Context || [L]₂)  (no counter)
     * 
     * For NIST SP 800-108 compliance, we DISABLE K0 extension:
     *   K(i) = CMAC(K_IN, Counter || Label || 0x00 || Context || [L]₂)  (with counter)
     * 
     * This ensures compatibility with EdgeProtectTools encrypted images and
     * standard NIST SP 800-108 test vectors.
     * 
     * Configuration values:
     *   CmacK0Extension selector: 0xE3 (ENABLE selector - allows setting K0 value)
     *   CmacK0Extension value: CS_KDFCTR_CMAC_K0_EXT_OFF (0x8F5A) - Disable K0 extension
     */
    kdf_cfg.KdfCtrCfgSelector.Format = CS_KDFCTR_CFG_CMAC_SELECTOR_FORMAT_ENABLE;
    kdf_cfg.KdfCtrCfgSelector.CmacK0Extension = 0xE3;  /* Enable K0 extension selector */
    kdf_cfg.CmacK0Extension = CS_KDFCTR_CMAC_K0_EXT_OFF;          /* Disable K0 extension (0x8F5A) - NIST compliance */
    
    kdf_cfg.KdfCtrCfgSelector.KdfCtrCfgSelRfu3 = 0x2D;  /* RESERVED - must be 0x2D */
    kdf_cfg.KdfCtrCfgSelector.KdfCtrCfgSelRfu4 = 0x1D;  /* RESERVED - must be 0x1D (added in v6.0.9) */

    kdf_cfg.R = CS_KDFCTR_CMAC_R_32;  /* 32-bit counter */
    kdf_cfg.Delim0 = CS_KDFCTR_CMAC_DELIM0_ON;
    kdf_cfg.L = CS_KDFCTR_CMAC_L_ON;

#if defined(USE_FUNC_ID)
    kdf_func_id = CS_KDF_CFG_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */

    status = Cs_Cfg(cs_kdf_handle, (Cs_StdApi_CfgType *)&kdf_cfg);
    if (status != CS_STATUS_SUCCESS) {
        BOOT_LOG_ERR("KDF Cs_Cfg failed: %lu", (unsigned long)status);
        return KDF_ERR_KDF_CFG_FAILED;
    }

    /* Prepare XBlobs for label and context */
    memset(&label_xblob, 0, sizeof(label_xblob));
    label_xblob.Data1Ptr = (uint8_t *)label;
    label_xblob.ByteLen = label_len;
    label_xblob.Properties = (uint16_t)((CS_XBLOB_TYPE_BYTE_ARRAY << CS_XBLOB_TYPE_Pos)
                             | (CS_XBLOB_INTEGRITY_SHORT << CS_XBLOB_INTEGRITY_Pos));
    label_xblob.Checksum = Cs_XBlob_CalculateChecksum(&label_xblob,
                                                      CS_XBLOB_ID_DEFAULT,
                                                      CS_XBLOB_INTEGRITY_SHORT,
                                                      NULL);

    memset(&context_xblob, 0, sizeof(context_xblob));
    context_xblob.Data1Ptr = (uint8_t *)context;
    context_xblob.ByteLen = context_len;
    context_xblob.Properties = (uint16_t)((CS_XBLOB_TYPE_BYTE_ARRAY << CS_XBLOB_TYPE_Pos)
                               | (CS_XBLOB_INTEGRITY_SHORT << CS_XBLOB_INTEGRITY_Pos));
    context_xblob.Checksum = Cs_XBlob_CalculateChecksum(&context_xblob,
                                                        CS_XBLOB_ID_DEFAULT,
                                                        CS_XBLOB_INTEGRITY_SHORT,
                                                        NULL);

    /* Prepare output XBlob */
    memset(&output_xblob, 0, sizeof(output_xblob));
    output_xblob.Data1Ptr = okm;
    output_xblob.ByteLen = *okm_len;
    output_xblob.Properties = (uint16_t)((CS_XBLOB_TYPE_BYTE_ARRAY << CS_XBLOB_TYPE_Pos)
                              | (CS_XBLOB_INTEGRITY_SHORT << CS_XBLOB_INTEGRITY_Pos));
    output_xblob.Checksum = Cs_XBlob_CalculateChecksum(&output_xblob,
                                                       CS_XBLOB_ID_DEFAULT,
                                                       CS_XBLOB_INTEGRITY_SHORT,
                                                       NULL);

    /* Perform KDF derivation */
    memset(&kdf_ctrl, 0, sizeof(kdf_ctrl));

    /* Counter(4 BE) || Label(16) || 0x00 || Salt(16) || Length(4 BE)
     * Label: "KBKDF_AESCMAC_v1", Salt from TLV, Length=128 bits for AES-128 */
    kdf_ctrl.Super.Super.Super.OID = CS_ENTITY_KDFCTR_SEC_OID;
    kdf_ctrl.Super.Super.TempMemPtr = cs_temp_memory;
    kdf_ctrl.Super.Super.TempMemSize = CS_TEMP_MEM_SIZE;
    kdf_ctrl.Super.Super.ClearTempMemOnExit = 1;  /* Clear temp memory */
    kdf_ctrl.Super.Super.CccFnPtr = Cs_Ccc_Kdf_DeriveKey;
#if defined(USE_FUNC_ID)
    kdf_ctrl.Super.Super.FuncIDPtr = &kdf_ctrl_func_id;
    kdf_ctrl_func_id = CS_KDF_CTRL_DERIVE_KEY_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */

    kdf_ctrl.Super.OutKeyingDataPtr = &output_xblob;
    
    /* Provide label and context in Cs_Ctrl, not Cs_Cfg */
    kdf_ctrl.InLabelDataPtr = &label_xblob;
    kdf_ctrl.InContextDataPtr = &context_xblob;

    status = Cs_Ctrl(cs_kdf_handle, (Cs_StdApi_CtrlType *)&kdf_ctrl);
    if (status != CS_STATUS_SUCCESS) {
        BOOT_LOG_ERR("KDF Cs_Ctrl failed: %lu", (unsigned long)status);
        return KDF_ERR_DERIVATION_FAILED;
    }

    /* Update output length */
    *okm_len = output_xblob.ByteLen;

    return 0;
}

/**
 * @brief Extract Salt + IV from TLV and derive DEK using KDF-CMAC
 * 
 * KDF-CMAC Scheme Position:
 * =========================
 * This function implements the left side of the encryption scheme:
 * 
 *    ──────────────            ────────────────────────────
 *   │ APP IMAGE    │          │  AES128 Key from SFLASH    │
 *   │ TLV 0x102    │          │  (Master Key)              │
 *   │              │           ───────────────────+────────
 *   │ Salt[0:15]   │─────────────────────+        │ 
 *   │ IV[16:31]    │─────────+           │        │      
 *    ──────────────          │           v        v      
 *                            │       ─────────────────
 *                            │      │   KBKDFCMAC     │
 *                            │      │(NIST SP 800-108)│
 *                            │       ────────+────────
 *                            │               │
 *                            │               v
 *                            │       ─────────────────
 *                            +─────>│   DEK Output    │
 *                                   │   (16 bytes)    │
 *                                    ─────────────────
 * 
 * EdgeProtectTools Architecture:
 * - Master Key: Stored in SFLASH at 0x034076F0 (16 bytes for AES-128)
 * - TLV 0x102: Embedded in encrypted image, contains Salt + IV (32 bytes)
 * - Salt: Used as KDF Context to derive unique DEK per image
 * - IV: Used later for AES-CTR decryption (copied unchanged)
 * 
 * Processing Steps:
 * 1. Extract Salt from TLV 0x102[0:15]   (16 bytes)
 * 2. Extract IV from TLV 0x102[16:31]    (16 bytes)
 * 3. Call kbkdf_cmac_derive_key():
 *      Input:  Master Key + Label ("KBKDF_AESCMAC_v1") + Salt
 *      Output: DEK (16 bytes)
 * 4. Return DEK and IV for subsequent AES-CTR decryption
 * 
 * KDF Formula (NIST SP 800-108):
 *   DEK = CMAC-AES-128(Master_Key, Counter || Label || 0x00 || Salt || Length)
 * 
 * @param buf     TLV 0x102 buffer: [Salt: 16 bytes][IV: 16 bytes] = 32 bytes
 * @param enckey  Output: Derived DEK (16 bytes) for firmware decryption
 * @param sz      Size of TLV buffer (must be 32 bytes)
 * @param enciv   Output: IV copied from TLV[16:31] (16 bytes) for AES-CTR
 * 
 * @return 0 on success, negative error code on failure
 * 
 * @note Function name is misleading - this does KEY DERIVATION, not decryption.
 *       Actual firmware decryption happens later in boot_encrypt().
 */
int boot_enc_decrypt(const uint8_t *buf, uint8_t *enckey, uint32_t sz, uint8_t *enciv)
{
    /* EdgeProtectTools fixed label (16 bytes) */
    const uint8_t kdf_label[16] = "KBKDF_AESCMAC_v1";
    const uint8_t *salt_ptr;
    const uint8_t *iv_ptr;
    uint16_t derived_len;
    int rc;

    rc = -1;

    /* TLV 0x102 now contains: Salt (16 bytes) + IV (16 bytes) = 32 bytes */
    if (sz != EXPECTED_KDF_CMAC_LEN || *bootutil_enc_key.len < BOOT_ENC_KEY_SIZE) {
        return KDF_ERR_INVALID_PARAM;
    }

    /* Extract salt and IV from TLV buffer - v.70: Back to original order */
    salt_ptr = buf + KDF_CMAC_SALT_INDEX;  /* Salt at bytes [0:15] */
    iv_ptr = buf + KDF_CMAC_IV_INDEX;      /* IV at bytes [16:31] */

    /* Derive DEK using KBKDF: Master Key + Label + Salt → DEK */
    derived_len = BOOT_ENC_KEY_SIZE;
    rc = kbkdf_cmac_derive_key(bootutil_enc_key.key, *bootutil_enc_key.len,
                               kdf_label, 16, salt_ptr, KDF_CMAC_SALT_SIZE,
                               enckey, &derived_len);

    if (rc != 0 || derived_len != BOOT_ENC_KEY_SIZE) {
        BOOT_LOG_ERR("DEK derivation failed: rc=%d, derived_len=%u", rc, (unsigned)derived_len);
        return KDF_ERR_DERIVATION_FAILED;
    }

    /* Copy IV from TLV to output buffer */
    (void)memcpy(enciv, iv_ptr, KDF_CMAC_IV_SIZE);

    return rc;
}

/**
 * @brief Initialize encryption state and crypto hardware entities
 * 
 * KDF-CMAC Flow Role:
 * ===================
 * First step in the encryption pipeline - initializes CS-PSC3X8 hardware.
 * Called by boot_enc_load() before processing encrypted images.
 * 
 * Initialization Sequence:
 *   1. Opens AES-128 entity (for CMAC and later CTR decryption)
 *   2. Opens CMAC entity (for KDF PRF function)
 *   3. Opens KDF-CTR entity (for key derivation)
 * 
 * Implementation:
 * Delegates to kbkdf_cmac_crypto_init() which performs the actual
 * CS-PSC3X8 entity initialization.
 * 
 * @param enc_state  Encryption state structure (not used by this implementation)
 * @param slot       Slot index (not used by this implementation)
 * 
 * @return 0 on success, KDF_ERR_INIT_FAILED on failure
 * 
 * @see kbkdf_cmac_crypto_init() for detailed initialization logic
 */
int boot_enc_init(struct enc_key_data *enc_state, uint8_t slot)
{
    int rc;
    (void)enc_state;
    (void)slot;

    /* Initialize handles if not already done */
    rc = kbkdf_cmac_crypto_init();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * @brief Drop encryption state and release crypto hardware resources
 * 
 * KDF-CMAC Flow Role:
 * ===================
 * Final cleanup step - releases all CS-PSC3X8 hardware resources.
 * Called when encryption operations are complete or on error.
 * 
 * Cleanup Sequence:
 *   1. Closes KDF-CTR entity
 *   2. Closes CMAC entity
 *   3. Closes AES-128 entity
 * 
 * Implementation:
 * Delegates to kbkdf_cmac_crypto_deinit() which performs the actual
 * CS-PSC3X8 entity cleanup.
 * 
 * @param enc_state  Encryption state structure (not used by this implementation)
 * @param slot       Slot index (not used by this implementation)
 * 
 * @return 0 on success
 * 
 * @note Safe to call multiple times - checks for NULL handles
 * @see kbkdf_cmac_crypto_deinit() for detailed cleanup logic
 */
int boot_enc_drop(struct enc_key_data *enc_state, uint8_t slot)
{
    (void)enc_state;
    (void)slot;

    kbkdf_cmac_crypto_deinit();

    return 0;
}

/**
 * @brief Configure AES entity with derived DEK for firmware decryption
 * 
 * KDF-CMAC Scheme Position:
 * =========================
 * This function prepares the AES-CTR decryption stage:
 * 
 *    ─────────────────
 *   │     DEK         │  <── Derived by boot_enc_decrypt()
 *   │  (16 bytes)     │
 *    ────────+────────
 *            │
 *            v
 *    ─────────────────
 *   │  AES-128 CTR    │  <── Configured by THIS function
 *   │  Configuration  │
 *    ────────+──────── 
 *            │
 *            v
 *       Ready for boot_encrypt()
 * 
 * Critical Note:
 * This function configures AES with the DERIVED DEK (from KDF-CMAC),
 * NOT the Master Key from SFLASH. This is essential for the scheme:
 * 
 *   Master Key → [KDF-CMAC] → DEK → [AES-CTR] → Decrypted Firmware
 *                   ↑                   ↑
 *            boot_enc_decrypt()   boot_encrypt()
 * 
 * Configuration:
 * - Key: DEK from bs->enckey[slot] (already derived)
 * - IV: Will be set per-block in boot_encrypt() with counter updates
 * - Mode: AES-128-CTR (configured for CTR decryption)
 * 
 * @param enc_state  Encryption state structure (contains validity flag)
 * @param slot       Slot index (primary/secondary/scratch)
 * @param bs         Boot status structure containing derived DEK in bs->enckey[slot]
 * 
 * @return 0 on success, -1 on failure
 */
int boot_enc_set_key(struct enc_key_data *enc_state, uint8_t slot,
        const struct boot_status *bs)
{
    int rc;
    Cs_StdApi_StatusType status;
    Cs_XBlobType dek_xblob;
    uint8_t rng_seed[8];
    uint8_t rng_seed_inv[8];
    Cs_Aes_CfgType aes_cfg;
#if defined(USE_FUNC_ID)
    uint32_t aes_func_id = CS_CIPHER_CFG_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */

    rc = -1;
    status = CS_STATUS_ERROR_GENERIC;

    /* Hardware implementation: Configure CS-PSC3X8 AES with derived DEK */
    if (cs_aes_handle.FirstPtr == NULL) {
        BOOT_LOG_ERR("[KDF-ERROR] AES handle not initialized");
        (void)boot_enc_drop(enc_state, slot);
        enc_state[slot].valid = 0;
        return -1;
    }

    /* Prepare DEK XBlob */
    memset(&dek_xblob, 0, sizeof(dek_xblob));
    dek_xblob.Data1Ptr = (uint8_t *)bs->enckey[slot];  /* Use DERIVED DEK, not master key! */
    dek_xblob.ByteLen = BOOT_ENC_KEY_SIZE;
    dek_xblob.Properties = (uint16_t)((CS_XBLOB_TYPE_BYTE_ARRAY << CS_XBLOB_TYPE_Pos)
                           | (CS_XBLOB_INTEGRITY_SHORT << CS_XBLOB_INTEGRITY_Pos));
    dek_xblob.Checksum = Cs_XBlob_CalculateChecksum(&dek_xblob,
                                                    CS_XBLOB_ID_DEFAULT,
                                                    CS_XBLOB_INTEGRITY_SHORT,
                                                    NULL);

    /* Prepare RNG seeds using hardware TRNG */
    if (kbkdf_cmac_trng_generate(rng_seed, rng_seed_inv, 8) != 0) {
        BOOT_LOG_ERR("Failed to generate AES RNG seed for DEK");
        (void)boot_enc_drop(enc_state, slot);
        enc_state[slot].valid = 0;
        return -1;
    }

    /* Reconfigure AES with the DERIVED DEK (not master key) */
    memset(&aes_cfg, 0, sizeof(aes_cfg));
    aes_cfg.Super.Super.Super.OID = CS_ENTITY_AES128_SEC_OID;
    aes_cfg.Super.Super.TempMemPtr = NULL;
    aes_cfg.Super.Super.TempMemSize = 0;
    aes_cfg.Super.Super.ClearTempMemOnExit = 0;
    aes_cfg.Super.Super.CfgSelector.RngSeed = CS_STDAPI_CFG_RNG_SEED_ENABLE;  /* RngSeed must be set */
    aes_cfg.Super.Super.RngSeedPtr = rng_seed;
    aes_cfg.Super.Super.RngSeedInvPtr = rng_seed_inv;
#if defined(USE_FUNC_ID)
    aes_cfg.Super.Super.FuncIDPtr = &aes_func_id;
    aes_cfg.Super.Super.FuncIDPtr[0] = CS_CIPHER_CFG_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */
    aes_cfg.Super.CipherCfgSelector.Key = CS_CIPHER_CFG_KEY_ENABLE;
    aes_cfg.Super.CipherCfgSelector.UseIKS = CS_CIPHER_CFG_USE_IKS_DISABLE;
    aes_cfg.Super.CipherCfgSelector.SecControl = CS_CIPHER_CFG_SEC_CONTROL_DISABLE;
    aes_cfg.Super.CipherCfgSelector.BlockRepetition = CS_CIPHER_CFG_BLOCK_REPETITION_DISABLE;
    aes_cfg.Super.KeyPtr = &dek_xblob;
    aes_cfg.Super.IVPtr = NULL;  /* IV will be set per-block in boot_encrypt */

    status = Cs_Cfg(cs_aes_handle, (Cs_StdApi_CfgType *)&aes_cfg);
    if (status != CS_STATUS_SUCCESS) {
        BOOT_LOG_ERR("AES Cs_Cfg failed: %lu", (unsigned long)status);
        (void)boot_enc_drop(enc_state, slot);
        enc_state[slot].valid = 0;
        return -1;
    }

    enc_state[slot].valid = 1;
    rc = 0;

    return rc;
}

/**
 * @brief Load encryption parameters from image TLV and derive DEK
 * 
 * KDF-CMAC Flow Role:
 * ===================
 * Orchestrates the complete key derivation process from encrypted image metadata.
 * This is the main entry point that ties together the entire KDF-CMAC scheme.
 * 
 * Processing Flow:
 * ================
 * 
 *   1. Initialize Crypto Hardware
 *      └─> boot_enc_init() → kbkdf_cmac_crypto_init()
 *          Opens AES, CMAC, and KDF-CTR entities
 * 
 *   2. Locate TLV 0x102 (IMAGE_TLV_ENC_KDF_CMAC)
 *      └─> Search image for encryption metadata
 *          Expected: 32 bytes (Salt[16] + IV[16])
 * 
 *   3. Read TLV from Flash
 *      └─> flash_area_read()
 *          Loads Salt + IV into buffer
 * 
 *   4. Derive DEK
 *      └─> boot_enc_decrypt()
 *          ├─> Extracts Salt and IV from TLV buffer
 *          ├─> Calls kbkdf_cmac_derive_key()
 *          │   └─> CMAC-AES-128(Master_Key, Counter || Label || 0x00 || Salt || Length)
 *          └─> Returns derived DEK + IV
 * 
 *   5. Store Results
 *      ├─> bs->enckey[slot] = derived DEK (16 bytes)
 *      └─> enc_state[slot].aes_iv = IV (16 bytes)
 * 
 * After this function completes:
 * - DEK is ready for AES configuration (boot_enc_set_key)
 * - IV is ready for CTR mode decryption (boot_encrypt)
 * 
 * Scheme Position:
 * ================
 * This function implements the entire left side of the KDF-CMAC scheme:
 *   Master Key (SFLASH) + Salt (TLV) → [KDF-CMAC] → DEK
 * 
 * @param enc_state   Encryption state structure (stores IV and validity flag)
 * @param image_index Image index (primary/secondary)
 * @param hdr         Image header structure
 * @param fap         Flash area pointer (where image is stored)
 * @param bs          Boot status structure (stores derived DEK)
 * 
 * @return 0 on success, 1 if already loaded, negative error code on failure
 * 
 * @see boot_enc_decrypt() for key derivation implementation
 * @see boot_enc_set_key() for subsequent AES configuration
 * @see boot_encrypt() for firmware decryption using derived DEK
 */
int boot_enc_load(struct enc_key_data *enc_state, int image_index,
        const struct image_header *hdr, const struct flash_area *fap,
        struct boot_status *bs)
{
    uint32_t off;
    uint16_t len;
    struct image_tlv_iter it;
#if defined(MCUBOOT_SWAP_SAVE_ENCTLV)
    uint8_t *buf;
#else
    uint8_t buf[BOOT_ENC_TLV_SIZE];
#endif /* MCUBOOT_SWAP_SAVE_ENCTLV */
    uint8_t slot;
    int rc;

    rc = flash_area_id_to_multi_image_slot(image_index, flash_area_get_id(fap));
    if (rc < 0) {
        return rc;
    }
    slot = rc;

#if !defined(MCUBOOT_ENC_IMAGES_XIP)
    /* Already loaded... */
    if (enc_state[slot].valid) {
        return 1;
    }
#endif /* MCUBOOT_ENC_IMAGES_XIP */

    /* Initialize the AES context */
    (void)boot_enc_init(enc_state, slot);

    rc = bootutil_tlv_iter_begin(&it, hdr, fap, IMAGE_TLV_ENC_KDF_CMAC, false);
    if (rc) {
        return -1;
    }

    rc = bootutil_tlv_iter_next(&it, &off, &len, NULL);
    if (rc != 0) {
        return rc;
    }

    if (len != BOOT_ENC_TLV_SIZE) {
        return -1;
    }

#if defined(MCUBOOT_SWAP_SAVE_ENCTLV)
    buf = bs->enctlv[slot];
    (void)memset(buf, BOOT_UNINITIALIZED_TLV_FILL, BOOT_ENC_TLV_ALIGN_SIZE);
#endif /* MCUBOOT_SWAP_SAVE_ENCTLV */

    rc = flash_area_read(fap, off, buf, len);
    if (rc != 0) {
        return -1;
    }

    return boot_enc_decrypt(buf, bs->enckey[slot], len, enc_state[slot].aes_iv);
}

/**
 * @brief Check if encryption is valid for image
 * 
 * @param enc_state    Encryption state structure
 * @param image_index  Image index
 * @param fap          Flash area
 * 
 * @return true if encryption is valid, false otherwise
 */
bool boot_enc_valid(struct enc_key_data *enc_state, int image_index,
        const struct flash_area *fap)
{
    int rc;

    rc = flash_area_id_to_multi_image_slot(image_index, flash_area_get_id(fap));
    if (rc < 0) {
        /* can't get proper slot number - skip encryption, */
        /* postpone the error for a upper layer */
        return false;
    }

    return enc_state[rc].valid;
}

/**
 * @brief Decrypt firmware data using AES-128-CTR with derived DEK
 * 
 * KDF-CMAC Scheme Position:
 * =========================
 * This function implements the right side of the encryption scheme:
 * 
 *    ─────────────────     ─────────────────     ─────────────────
 *   │      DEK        │   │  IV from TLV    │   │   Encrypted     │
 *   │  (configured)   │   │  (from image)   │   │    Payload      │
 *    ─────────+───────     ─────────+───────     ────────+────────
 *             │                     │                    │
 *             v                     v                    v
 *        ────────────────────────────────────────────────── 
 *       │             AES-128-CTR Decryption               │
 *       │               (THIS FUNCTION)                    │
 *        ──────────────────────────+───────────────────────
 *                                  │
 *                                  v
 *                          ─────────────────
 *                         │   Decrypted     │
 *                         │   Firmware      │
 *                          ─────────────────
 * 
 * AES-CTR Mode Operation:
 * =======================
 * CTR mode converts AES block cipher into a stream cipher:
 * 
 * For each 16-byte block at offset 'off':
 *   1. Calculate block counter: off >> 4 (divide by 16)
 *   2. Compute nonce: IV with counter added to last 4 bytes (big-endian)
 *   3. Generate keystream: AES-128(DEK, nonce)
 *   4. Decrypt: plaintext = ciphertext ⊕ keystream
 * 
 * Nonce Format (16 bytes):
 *   [IV: 12 bytes][Counter: 4 bytes BE]
 * 
 * In-Place Decryption:
 * The function overwrites the input buffer with decrypted data:
 *   buf[in]:  Encrypted firmware
 *   buf[out]: Decrypted firmware (same buffer)
 * 
 * @param enc_state    Encryption state (contains base IV)
 * @param image_index  Image index (primary/secondary)
 * @param fap          Flash area pointer
 * @param off          Byte offset within image (used to calculate block counter)
 * @param sz           Size of data to decrypt (bytes)
 * @param blk_off      Block offset within buffer (unused)
 * @param buf          Input: Encrypted data, Output: Decrypted data (in-place)
 * 
 * @return 0 on success, negative error code on failure
 * 
 * @note Function name is misleading - in MCUBoot context, this always DECRYPTS.
 *       AES-CTR encryption/decryption are identical operations (XOR with keystream).
 */
int boot_encrypt(struct enc_key_data *enc_state, int image_index,
        const struct flash_area *fap, uint32_t off, uint32_t sz,
        uint32_t blk_off, uint8_t *buf)
{
    struct enc_key_data *enc;
    uint8_t *nonce;
    int rc;
    uint8_t local_nonce[16];
    uint32_t counter;
    Cs_XBlobType iv_xblob;
    Cs_Aes_CfgType aes_cfg;
    Cs_StdApi_StatusType status;
    Cs_XBlobType in_xblob;
    Cs_XBlobType out_xblob;
    Cs_Aes_CtrlType aes_ctrl;
#if defined(USE_FUNC_ID)
    uint32_t aes_func_id = CS_CIPHER_CFG_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */

    rc = -1;
    status = CS_STATUS_ERROR_GENERIC;

    (void)blk_off;

    /* boot_copy_region will call boot_encrypt with sz = 0 when skipping over TLVs */
    if (sz == 0) {
        return 0;
    }

#if MCUBOOT_SWAP_USING_SCRATCH
    /* In this case scratch area contains encrypted source block from secondary slot */
    if (fap->fa_id == FLASH_AREA_IMAGE_SCRATCH) {
        rc = 1;
    } else
#endif /* MCUBOOT_SWAP_USING_SCRATCH */
    {
        rc = flash_area_id_to_multi_image_slot(image_index, fap->fa_id);
        if (rc < 0) {
            assert(0);
            return rc;
        }
    }

    enc = &enc_state[rc];
    assert(enc->valid == 1);

    /* For CMAC-KDF, use local copy of IV to avoid corrupting state
     * EdgeProtectTools expects IV to be used as-is for CTR mode, not modified
     * per block. We need to preserve the base IV and compute per-block counter. */
    memcpy(local_nonce, enc->aes_iv, 16);
    nonce = local_nonce;

    off >>= 4;
    /* Add block offset to IV counter (big-endian addition to last 4 bytes) */
    counter = ((uint32_t)nonce[12] << 24) | ((uint32_t)nonce[13] << 16) | 
                       ((uint32_t)nonce[14] << 8) | (uint32_t)nonce[15];
    counter += off;
    nonce[12] = (uint8_t)(counter >> 24);
    nonce[13] = (uint8_t)(counter >> 16);
    nonce[14] = (uint8_t)(counter >> 8);
    nonce[15] = (uint8_t)counter;

    /* Hardware implementation: Use CS-PSC3X8 AES-CTR */
    if (cs_aes_handle.FirstPtr == NULL) {
        BOOT_LOG_ERR("AES handle not initialized: %d", KDF_ERR_INIT_FAILED);
        return KDF_ERR_INIT_FAILED;
    }

    /* Note: AES is already configured with DEK by boot_enc_set_key */

    /* Prepare IV XBlob with updated counter */
    memset(&iv_xblob, 0, sizeof(iv_xblob));
    iv_xblob.Data1Ptr = nonce;
    iv_xblob.ByteLen = 16;
    iv_xblob.Properties = (uint16_t)((CS_XBLOB_TYPE_BYTE_ARRAY << CS_XBLOB_TYPE_Pos)
                          | (CS_XBLOB_INTEGRITY_SHORT << CS_XBLOB_INTEGRITY_Pos));
    iv_xblob.Checksum = Cs_XBlob_CalculateChecksum(&iv_xblob,
                                                   CS_XBLOB_ID_DEFAULT,
                                                   CS_XBLOB_INTEGRITY_SHORT,
                                                   NULL);

    /* Configure AES with IV for CTR mode (key already set in boot_enc_set_key) */
    memset(&aes_cfg, 0, sizeof(aes_cfg));
    aes_cfg.Super.Super.Super.OID = CS_ENTITY_AES128_SEC_OID;
    aes_cfg.Super.Super.TempMemPtr = NULL;
    aes_cfg.Super.Super.TempMemSize = 0;
    aes_cfg.Super.Super.ClearTempMemOnExit = 0;
    aes_cfg.Super.Super.CfgSelector.RngSeed = CS_STDAPI_CFG_RNG_SEED_DISABLE;  /* RngSeed only in first Cs_Cfg */
    aes_cfg.Super.Super.RngSeedPtr = NULL;
    aes_cfg.Super.Super.RngSeedInvPtr = NULL;
#if defined(USE_FUNC_ID)
    aes_cfg.Super.Super.FuncIDPtr = &aes_func_id;
    aes_cfg.Super.Super.FuncIDPtr[0] = CS_CIPHER_CFG_INPUT_FUNC_ID;  /* FuncID must be set */
#endif /* USE_FUNC_ID */
    aes_cfg.Super.CipherCfgSelector.Key = CS_CIPHER_CFG_KEY_DISABLE;  /* Key already configured */
    aes_cfg.Super.CipherCfgSelector.IV = CS_CIPHER_CFG_IV_ENABLE;     /* Update IV only */
    aes_cfg.Super.CipherCfgSelector.UseIKS = CS_CIPHER_CFG_USE_IKS_DISABLE;
    aes_cfg.Super.CipherCfgSelector.SecControl = CS_CIPHER_CFG_SEC_CONTROL_DISABLE;
    aes_cfg.Super.CipherCfgSelector.BlockRepetition = CS_CIPHER_CFG_BLOCK_REPETITION_DISABLE;
    aes_cfg.Super.IVPtr = &iv_xblob;  /* Set IV with updated counter */
    aes_cfg.Super.KeyPtr = NULL;

    status = Cs_Cfg(cs_aes_handle, (Cs_StdApi_CfgType *)&aes_cfg);
    if (status != CS_STATUS_SUCCESS) {
        BOOT_LOG_ERR("AES Cs_Cfg (IV update) failed: %lu", (unsigned long)status);
        return KDF_ERR_AES_CFG_FAILED;
    }

    /* Prepare input/output XBlobs for native CTR decryption */
    memset(&in_xblob, 0, sizeof(in_xblob));
    in_xblob.Data1Ptr = buf;  /* Input: encrypted data */
    in_xblob.ByteLen = sz;
    in_xblob.Properties = (uint16_t)((CS_XBLOB_TYPE_BYTE_ARRAY << CS_XBLOB_TYPE_Pos)
                          | (CS_XBLOB_INTEGRITY_SHORT << CS_XBLOB_INTEGRITY_Pos));
    in_xblob.Checksum = Cs_XBlob_CalculateChecksum(&in_xblob,
                                                   CS_XBLOB_ID_DEFAULT,
                                                   CS_XBLOB_INTEGRITY_SHORT,
                                                   NULL);

    memset(&out_xblob, 0, sizeof(out_xblob));
    out_xblob.Data1Ptr = buf;  /* Output: decrypted data (in-place) */
    out_xblob.ByteLen = sz;
    out_xblob.Properties = (uint16_t)((CS_XBLOB_TYPE_BYTE_ARRAY << CS_XBLOB_TYPE_Pos)
                           | (CS_XBLOB_INTEGRITY_SHORT << CS_XBLOB_INTEGRITY_Pos));
    out_xblob.Checksum = Cs_XBlob_CalculateChecksum(&out_xblob,
                                                    CS_XBLOB_ID_DEFAULT,
                                                    CS_XBLOB_INTEGRITY_SHORT,
                                                    NULL);

    /* Perform native AES-CTR decryption */
    memset(&aes_ctrl, 0, sizeof(aes_ctrl));
    aes_ctrl.Super.Super.Super.OID = CS_ENTITY_AES128_SEC_OID;
    aes_ctrl.Super.Super.TempMemPtr = NULL;
    aes_ctrl.Super.Super.TempMemSize = 0;
    aes_ctrl.Super.Super.ClearTempMemOnExit = 0;
    aes_ctrl.Super.Super.CccFnPtr = Cs_Ccc_Cipher_CTR_Decrypt;
#if defined(USE_FUNC_ID)
    aes_ctrl.Super.Super.FuncIDPtr = &aes_func_id;
    aes_ctrl.Super.Super.FuncIDPtr[0] = CS_CCC_CIPHER_CTR_DECRYPT_INPUT_FUNC_ID;
#endif /* USE_FUNC_ID */
    aes_ctrl.Super.InDataPtr = &in_xblob;
    aes_ctrl.Super.OutDataPtr = &out_xblob;

    status = Cs_Ctrl(cs_aes_handle, (Cs_StdApi_CtrlType *)&aes_ctrl);
    if (status != CS_STATUS_SUCCESS) {
        BOOT_LOG_ERR("AES native CTR decrypt failed: %lu", (unsigned long)status);
        return KDF_ERR_DECRYPT_FAILED;
    }

    rc = 0;  /* Success */
    return rc;
}

/**
 * @brief Zeroize encryption state after use
 * 
 * @param enc_state  Encryption state structure
 */
void boot_enc_zeroize(struct enc_key_data *enc_state)
{
    uint8_t slot;
    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
        (void)boot_enc_drop(enc_state, slot);
    }
    (void)memset(enc_state, 0, sizeof(struct enc_key_data) * BOOT_NUM_SLOTS);
}

#endif /* USE_ENCRYPTION_KDF_CTR */

/*
 * Avoid warning from -pedantic. This is included
 * because ISO C forbids an empty translation unit.
 */
typedef int encrypted_iso_c_forbids_empty_translation_units;
