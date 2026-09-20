/***************************************************************************//**
* \file cyboot_result.h
* \version 1.0
*
* Basic function result handling. Defines a simple type for conveying
* information about whether something succeeded or details about any issues
* that were detected.
********************************************************************************
* \copyright
* Copyright(c) 2023-2026, Infineon Technologies AG, or an affiliate of
* Infineon Technologies AG. All rights reserved.
*
* This software, associated documentation and materials ("Software") is owned
* by Infineon Technologies AG or one of its affiliates ("Infineon") and is
* protected by and subject to worldwide patent protection, worldwide copyright
* laws, and international treaty provisions. Therefore, you may use this
* Software only as provided in the license agreement accompanying the software
* package from which you obtained this Software. If no license agreement
* applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE IS
* PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING,
* BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF THIRD-PARTY RIGHTS
* AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A SPECIFIC
* USE/PURPOSE OR MERCHANTABILITY. Infineon reserves the right to make changes
* to the Software without notice. You are responsible for properly designing,
* programming, and testing the functionality and safety of your intended
* application of the Software, as well as complying with any legal requirements
* related to its use. Infineon does not guarantee that the Software will be
* free from intrusion, data theft or loss, or other breaches ("Security
* Breaches"), and Infineon shall have no liability arising out of any Security
* Breaches. Unless otherwise explicitly approved by Infineon, the Software may
* not be used in any application where a failure of the Product or any
* consequences of the use thereof can reasonably be expected to result in
* personal injury.
*******************************************************************************/


/**
* \addtogroup cyboot_result Result Codes
* \{
*   \defgroup cyboot_result_basic Basic Codes
*   \defgroup cyboot_result_smif SMIF Status Codes
*   \defgroup cyboot_result_protection Protection Status Codes
*   \defgroup cyboot_result_debug Debug Status Codes
*   \defgroup cyboot_result_other Other Codes
*   \defgroup cyboot_result_image Image Verification Codes
*/

#ifndef CYBOOT_RESULT_H
#define CYBOOT_RESULT_H

#include <stdint.h>


typedef uint32_t cyboot_result_t;

#define CYBOOT_RESULT_CODE_Pos          (0UL)
#define CYBOOT_RESULT_CODE_Msk          (0x0000FFFFUL)
#define CYBOOT_ID_Pos                   (20UL)
#define CYBOOT_ID_Msk                   (0xFFF00000UL)

/* BootROM module ID values */
#define CYBOOT_ID_SUCCESS               (0x0D500000UL)
#define CYBOOT_ID_FAIL                  (0xBAF00000UL)
#define CYBOOT_ID_CORRUPT               (0xBAC00000UL)

#define CYBOOT_CREATE_SUCCESS(status)   ((CYBOOT_ID_SUCCESS) | ((status) & CYBOOT_RESULT_CODE_Msk))
#define CYBOOT_CREATE_FAIL(status)      ((CYBOOT_ID_FAIL)    | ((status) & CYBOOT_RESULT_CODE_Msk))
#define CYBOOT_CREATE_WFA(status)       CYBOOT_CREATE_SUCCESS(status)
#define CYBOOT_CREATE_CORRUPTED(status) ((CYBOOT_ID_CORRUPT) | ((status) & CYBOOT_RESULT_CODE_Msk))


/*******************************************************************************
*                          Result codes
*******************************************************************************/

/** \addtogroup cyboot_result_basic
*   \{
*/

/** Success */
#define CYBOOT_SUCCESS                          CYBOOT_CREATE_SUCCESS(0xB002UL)
/* Split CYBOOT_SUCCESS into two parts masked by 0b0101... and 0b1010... */
/** Success 1 */
#define CYBOOT_SUCCESS_1                        (CYBOOT_SUCCESS & 0x55555555UL)
/** Success 2 */
#define CYBOOT_SUCCESS_2                        (CYBOOT_SUCCESS & 0xAAAAAAAAUL)
/** One or more invalid parameters */
#define CYBOOT_BAD_PARAM                        CYBOOT_CREATE_FAIL(0x0001UL)
/** Resource lock failure */
#define CYBOOT_LOCKED                           CYBOOT_CREATE_FAIL(0x0002UL)
/** Operation started but not necessarily completed yet */
#define CYBOOT_STARTED                          CYBOOT_CREATE_FAIL(0x0003UL)
/** Operation completed */
#define CYBOOT_FINISHED                         CYBOOT_CREATE_FAIL(0x0004UL)
/** Operation canceled */
#define CYBOOT_CANCELED                         CYBOOT_CREATE_FAIL(0x0005UL)
/** Operation timed out */
#define CYBOOT_TIMEOUT                          CYBOOT_CREATE_FAIL(0x0006UL)
/** Invalid SMIF configuration */
#define CYBOOT_NOT_IMPLEMENTED                  CYBOOT_CREATE_FAIL(0x0007UL)
/** A generic failure */
#define CYBOOT_FAILED                           CYBOOT_CREATE_FAIL(0x0008UL)
/** A flash recover failure */
#define CYBOOT_FLASH_RECOVER_ERR                CYBOOT_CREATE_FAIL(0x0009UL)
/** Code detected a possible fault injection attack.
 * Use generic error code as per MXSV2BOOT-6188 recommendation */
#define CYBOOT_FI_DETECTED                      CYBOOT_FAILED

/* Status codes must follow the naming convention of
* CYBOOT_<OBJECT_OR_ACTION>_<STATUS>.
* Status codes must be created using CYBOOT_CREATE_SUCCESS() or
* CYBOOT_CREATE_FAIL() macros as appropriate.
*/
/** Invalid TOC2 object */
#define CYBOOT_TOC2_OBJ_INVALID                 CYBOOT_CREATE_FAIL(0x0010UL)
/** Invalid L1 application descriptor. The boostrap size (inside L1 application
*   descriptor) is greater than the image */
#define CYBOOT_L1_APP_DESCR_INVALID             CYBOOT_CREATE_FAIL(0x0011UL)
/** The TOC2 debug certificate address is empty */
#define CYBOOT_TOC2_DEBUG_CERT_ADDR_EMPTY       CYBOOT_CREATE_FAIL(0x0012UL)
/** Invalid verification application descriptor content */
#define CYBOOT_SERV_APP_DESCR_INVALID           CYBOOT_CREATE_FAIL(0x0013UL)

/** Read the bootrow value from the OTP failed */
#define CYBOOT_BOOTROW_READ_FAILED              CYBOOT_CREATE_FAIL(0x0020UL)
/** BOOTROW corrupted */
#define CYBOOT_BOOTROW_CORRUPTED                CYBOOT_CREATE_FAIL(0x0021UL)
/** OTP initialization failed */
#define CYBOOT_OTP_INIT_FAILED                  CYBOOT_CREATE_FAIL(0x0022UL)
/** OTP read operation failed */
#define CYBOOT_OTP_READ_FAILED                  CYBOOT_CREATE_FAIL(0x0023UL)
/** OTP write operation failed */
#define CYBOOT_OTP_WRITE_FAILED                 CYBOOT_CREATE_FAIL(0x0024UL)
/** Invalid SMIF configuration from OTP data structure */
#define CYBOOT_OTP_SMIF_CFG_INVALID             CYBOOT_CREATE_FAIL(0x0025UL)
/** ASSET/FACTORY hash validation failed */
#define CYBOOT_ASSET_FACTORY_HASH_INVALID       CYBOOT_CREATE_FAIL(0x0026UL)
/** PROT_FW hash validation failed */
#define CYBOOT_PROT_FW_HASH_INVALID             CYBOOT_CREATE_FAIL(0x0027UL)
/** SECURE hash validation failed */
#define CYBOOT_SECURE_HASH_INVALID              CYBOOT_CREATE_FAIL(0x0028UL)

/** Public key hash request failed */
#define CYBOOT_PUBKEY_REQUEST_FAILED            CYBOOT_CREATE_FAIL(0x0030UL)
/** Incorrect the input key data */
#define CYBOOT_PUBKEY_FORMAT_INVALID            CYBOOT_CREATE_FAIL(0x0031UL)
/** Calculated hash does not match */
#define CYBOOT_PUBKEY_VALIDATION_FAILED         CYBOOT_CREATE_FAIL(0x0032UL)
/** Invalid PTE public key hash */
#define CYBOOT_PUBKEY_HASH_PTE_INVALID          CYBOOT_CREATE_FAIL(0x0033UL)
/** Invalid ICV public key hash */
#define CYBOOT_PUBKEY_HASH_ICV_INVALID          CYBOOT_CREATE_FAIL(0x0034UL)
/** Invalid OEM public key hash */
#define CYBOOT_PUBKEY_HASH_OEM_INVALID          CYBOOT_CREATE_FAIL(0x0035UL)
/** RAM application validation failed */
#define CYBOOT_APP_VALIDATION_FAILED            CYBOOT_CREATE_FAIL(0x0036UL)
/** Vector Table address is zero (IDLE loop) */
#define CYBOOT_APP_LAUNCH_ADDR_INVALID          CYBOOT_CREATE_FAIL(0x0037UL)
/** Verification of the digital signature with the RSASSA-PKCS1-v2.1 algorithm failed */
#define CYBOOT_SIGNATURE_INVALID                CYBOOT_CREATE_FAIL(0x0038UL)
/** MCUBOOT internal assert */
#define CYBOOT_MCUBOOT_INTERNAL_ASSERT          CYBOOT_CREATE_FAIL(0x0039UL)
/** Invalid RAM application format */
#define CYBOOT_RAM_APP_FORMAT_INVALID           CYBOOT_CREATE_FAIL(0x003AUL)
/** \} cyboot_result_basic */

/** \addtogroup cyboot_result_smif
*   \{
*/

/** SMIF initialization failed */
#define CYBOOT_SMIF_INIT_FAILED                 CYBOOT_CREATE_FAIL(0x0040UL)
/** SMIF deinitialization failed */
#define CYBOOT_SMIF_DEINIT_FAILED               CYBOOT_CREATE_FAIL(0x0041UL)
/** SMIF read operation failed*/
#define CYBOOT_SMIF_READ_FAILED                 CYBOOT_CREATE_FAIL(0x0042UL)
/** SMIF write operation failed */
#define CYBOOT_SMIF_WRITE_FAILED                CYBOOT_CREATE_FAIL(0x0043UL)
/** SMIF erase operation failed */
#define CYBOOT_SMIF_ERASE_FAILED                CYBOOT_CREATE_FAIL(0x0044UL)
/** SMIF getting-sector-size operation failed */
#define CYBOOT_SMIF_GET_SECTOR_SIZE_FAILED      CYBOOT_CREATE_FAIL(0x0045UL)
/** SMIF is not used */
#define CYBOOT_SMIF_UNUSED                      CYBOOT_CREATE_FAIL(0x0046UL)
/** SMIF configuration data is empty */
#define CYBOOT_SMIF_CONFIG_EMPTY                CYBOOT_CREATE_FAIL(0x0047UL)
/** Invalid SMIF application descriptor */
#define CYBOOT_SMIF_APP_DESCR_INVALID           CYBOOT_CREATE_FAIL(0x0048UL)

/** Error in Flash trims (SFLASH row 0) */
#define CYBOOT_FLASH_TRIM_INVALID               CYBOOT_CREATE_FAIL(0x004AUL)
/** Flash init error */
#define CYBOOT_FLASH_INIT_FAILED                CYBOOT_CREATE_FAIL(0x004BUL)
/** Flash address is invalid */
#define CYBOOT_FLASH_ADDR_INVALID               CYBOOT_CREATE_FAIL(0x004CUL)
/** A parameter value for Flash API is invalid */
#define CYBOOT_FLASH_PARAM_INVALID              CYBOOT_CREATE_FAIL(0x004DUL)
/** A parameter value for Flash ECC NC  fault is unexpected */
#define CYBOOT_FAULT_UNEXPECTED                 CYBOOT_CREATE_FAIL(0x004EUL)
/** SFLASH write operation failed */
#define CYBOOT_SFLASH_WRITE_FAILED              CYBOOT_CREATE_FAIL(0x004FUL)
/** A fail status for read and compare flash row after a flash write or erase operation */
#define CYBOOT_FLASH_WRITE_DATA_CHECK_FAILED    CYBOOT_CREATE_FAIL(0x0049UL)

/** \} cyboot_result_smif */

/** \addtogroup cyboot_result_other
* \{
*/

/** Invalid status of OTP zeros check 0 */
#define CYBOOT_NUM_ZEROS_0_INVALID              CYBOOT_CREATE_FAIL(0x0050UL)
/** Invalid status of OTP zeros check 1 */
#define CYBOOT_NUM_ZEROS_1_INVALID              CYBOOT_CREATE_FAIL(0x0051UL)
/** Invalid status of OTP zeros check 2 */
#define CYBOOT_NUM_ZEROS_2_INVALID              CYBOOT_CREATE_FAIL(0x0052UL)
/** Invalid status of OTP zeros check 3 */
#define CYBOOT_NUM_ZEROS_3_INVALID              CYBOOT_CREATE_FAIL(0x0053UL)
/** Invalid calculation number of zeros for num_zeros_key_0_hash assets */
#define CYBOOT_NUM_ZEROS_KEY_0_HASH_INVALID     CYBOOT_CREATE_FAIL(0x0054UL)
/** Invalid calculation number of zeros for num_zeros_key_1_hash assets */
#define CYBOOT_NUM_ZEROS_KEY_1_HASH_INVALID     CYBOOT_CREATE_FAIL(0x0055UL)
/** Invalid calculation number of zeros for num_zeros_asset_hash assets */
#define CYBOOT_NUM_ZEROS_ASSET_HASH_INVALID     CYBOOT_CREATE_FAIL(0x0056UL)
/** Invalid calculation number of zeros for num_zeros_factory_hash assets */
#define CYBOOT_NUM_ZEROS_FACTORY_HASH_INVALID   CYBOOT_CREATE_FAIL(0x0057UL)
/** Invalid calculation number of zeros for num_zeros_secure_hash assets */
#define CYBOOT_NUM_ZEROS_SECURE_HASH_INVALID    CYBOOT_CREATE_FAIL(0x0058UL)
/** Invalid calculation number of zeros for num_zeros_prot_fw_hash assets */
#define CYBOOT_NUM_ZEROS_PROT_FW_HASH_INVALID   CYBOOT_CREATE_FAIL(0x0059UL)

/** Built-In Self Test of RAM memory failed */
#define CYBOOT_BIST_FAILED                      CYBOOT_CREATE_FAIL(0x0060UL)
/** The TEST_KEY_SAFE_MODE signal is asserted in NORMAL LCS or higher */
#define CYBOOT_SAFE_MODE_REACHED                CYBOOT_CREATE_FAIL(0x0061UL)
/** The TEST_KEY_DFT_EN signal is asserted in NORMAL LCS or higher */
#define CYBOOT_DFT_EN_REACHED                   CYBOOT_CREATE_FAIL(0x0062UL)
/** Deinitialization resources failed */
#define CYBOOT_RESOURCES_DEINIT_FAILED          CYBOOT_CREATE_FAIL(0x0063UL)
/** Invalid DS address */
#define CYBOOT_DS_ADDR_INVALID                  CYBOOT_CREATE_FAIL(0x0064UL)
/** When get_debug_restrict() for RMA reaches unexpected RMA configuration */
#define CYBOOT_RMA_RESTRICT_INVALID             CYBOOT_CREATE_FAIL(0x0065UL)
/** Next application launched */
#define CYBOOT_NEXT_APP_LAUNCHED                CYBOOT_CREATE_SUCCESS(0x0067UL)
/** Idle branch reached */
#define CYBOOT_IDLE_BRANCH_REACHED              CYBOOT_CREATE_SUCCESS(0x0068UL)
/** Basic trim write failed */
#define CYBOOT_BASIC_TRIM_WRITE_FAILED          CYBOOT_CREATE_FAIL(0x006AUL)
/** Full trim write failed */
#define CYBOOT_FULL_TRIM_WRITE_FAILED           CYBOOT_CREATE_FAIL(0x006BUL)
/** Unexpected life cycle */
#define CYBOOT_LCS_UNEXPECTED                   CYBOOT_CREATE_FAIL(0x006CUL)
/** Warm boot is not supported */
#define CYBOOT_WARM_BOOT_NOT_SUPPORTED          CYBOOT_CREATE_FAIL(0x006DUL)
/** DS RAM init failed */
#define CYBOOT_DS_RAM_INIT_FAILED               CYBOOT_CREATE_FAIL(0x006EUL)
/** Clock init failed */
#define CYBOOT_CLOCK_INIT_FAILED                CYBOOT_CREATE_FAIL(0x006FUL)

/** \} cyboot_result_other */

/** \addtogroup cyboot_result_protection
 *  \{
 */

/* Protection specific */
/** \cond INTERNAL */
#define CYBOOT_PROT_APPLY_PROT_FAILURE          CYBOOT_CREATE_FAIL(0x0070UL)
/** Protection configuration skipped */
#define CYBOOT_PROT_CFG_SKIPPED                 CYBOOT_CREATE_FAIL(0x0071UL)
/* Invalid cfg_id for cyboot_get_debug_policy() */
#define CYBOOT_PROT_ID_DEBUG_INVALID            CYBOOT_CREATE_FAIL(0x0072UL)
/* Invalid cfg_id for cyboot_prot_apply_system_protection() */
#define CYBOOT_PROT_ID_APPLY_INVALID            CYBOOT_CREATE_FAIL(0x0073UL)
/* Invalid cfg_id for cyboot_finish_boot() */
#define CYBOOT_PROT_ID_FINISH_INVALID           CYBOOT_CREATE_FAIL(0x0074UL)
/** \endcond */
/** Invalid Access Restriction value for CM33-AP */
#define CYBOOT_CM33_RESTRICT_INVALID            CYBOOT_CREATE_FAIL(0x0075UL)
/** Invalid Access Restriction value for SYS-AP */
#define CYBOOT_SYS_RESTRICT_INVALID             CYBOOT_CREATE_FAIL(0x0076UL)
/** Verification of application sections to setup protection failed.
* The application sections have incorrect size or intersect.
*/
#define CYBOOT_PROT_APP_SECT_VERIF_FAILED       CYBOOT_CREATE_FAIL(0x0077UL)
/** Unexpected error during MPC configuration */
#define CYBOOT_PROT_MPC_CFG_FAILED              CYBOOT_CREATE_FAIL(0x0078UL)
/** Unexpected error during PPC configuration */
#define CYBOOT_PROT_PPC_CFG_FAILED              CYBOOT_CREATE_FAIL(0x0079UL)
/** Unexpected error during wounding */
#define CYBOOT_PROT_WOUNDING_CFG_FAILED         CYBOOT_CREATE_FAIL(0x007AUL)
/** Unexpected error during MSC configuration */
#define CYBOOT_PROT_MS_CTL_CFG_FAILED           CYBOOT_CREATE_FAIL(0x007BUL)
/** Debug certificate verification failed */
#define CYBOOT_DEBUG_TOKEN_VERIF_FAILED          CYBOOT_CREATE_FAIL(0x007CUL)

/** \} cyboot_result_protection */

/*
* WFA status codes are the status codes which are caused by (TST_DEBUG_CTL.REQUEST != 0).
* These values are written to TST_DEBUG_STATUS:
*
* TST_DEBUG_STATUS may hold either CORRUPTED status or WFA status.
* A different module ID is used do distinguish between them.
* - CORRUPTED status codes use CYBOOT_CREATE_FAIL() macro.
* - WFA status codes use CYBOOT_CREATE_WFA() macro.
*/
/** \addtogroup cyboot_result_debug
* \{
*/
/** Report status WFA polling */
#define CYBOOT_WFA_POLLING                      CYBOOT_CREATE_WFA(0x0080UL)
/** Service aplication launched */
#define CYBOOT_SERVICE_APP_LAUNCHED             CYBOOT_CREATE_WFA(0x0081UL)
/** Indicate application launch failed */
#define CYBOOT_SERVICE_APP_NOT_LAUNCHED         CYBOOT_CREATE_WFA(0x0082UL)
/** Invalid WFA reqest */
#define CYBOOT_WFA_REQUEST_INVALID              CYBOOT_CREATE_WFA(0x0083UL)
/** Debug certificate passed */
#define CYBOOT_DEBUG_TOKEN_PASSED               CYBOOT_CREATE_WFA(0x0084UL)
/** Debug certificate failed */
#define CYBOOT_DEBUG_TOKEN_FAILED               CYBOOT_CREATE_WFA(0x0085UL)
/** Debug policy invalid */
#define CYBOOT_DEBUG_POLICY_INVALID             CYBOOT_CREATE_FAIL(0x0086UL)

/** The hash hardware initialization failed */
#define CYBOOT_SHA_INIT_FAILED                  CYBOOT_CREATE_FAIL(0x0090UL)
/** The image hash calculation failed */
#define CYBOOT_SHA_CALC_FAILED                  CYBOOT_CREATE_FAIL(0x0091UL)


/** \} cyboot_result_debug */


/** \addtogroup cyboot_result_image
*  \{
*/

/* Image verification codes (mcuboot) */
/** The image validation failed */
#define CYBOOT_IMG_INVALID                      CYBOOT_CREATE_FAIL(0x00A0UL)
/** Image validation successful */
#define CYBOOT_IMG_VALID                        CYBOOT_CREATE_SUCCESS(0x00A5UL)
/* Split CYBOOT_IMG_VALID into two parts masked by 0b0101... and 0b1010... */
/** Img valid 1 */
#define CYBOOT_IMG_VALID_1                      (CYBOOT_IMG_VALID & 0x55555555UL)
/** Img valid 2 */
#define CYBOOT_IMG_VALID_2                      (CYBOOT_IMG_VALID & 0xAAAAAAAAUL)
/* Codes for internal use only */
/** The access to flash area failed */
#define CYBOOT_FLASH_AREA_INVALID               CYBOOT_CREATE_FAIL(0x00ADUL)
/** Invalid TLV iterator */
#define CYBOOT_TLV_INVALID                      CYBOOT_CREATE_FAIL(0x00AEUL)
/** No more TLVs with the matching type are available */
#define CYBOOT_TLV_NO_MORE_FOUND                CYBOOT_CREATE_FAIL(0x00AFUL)
/** No boot image found */
#define CYBOOT_NO_BOOT_IMAGE_FOUND              CYBOOT_CREATE_FAIL(0x00A1UL)
/** No rollback counter found in TLV */
#define CYBOOT_TLV_SEC_COUNTER_NOT_FOUND        CYBOOT_CREATE_FAIL(0x00A2UL)
/** SEC_CTR too small */
#define CYBOOT_TLV_SEC_COUNTER_INVALID          CYBOOT_CREATE_FAIL(0x00A3UL)
/** Flash image has an ECC NC fault */
#define CYBOOT_IMG_FLASH_ECC_NC_FAULT           CYBOOT_CREATE_FAIL(0x00A4UL)
/** \} cyboot_result_image */


/* DFU error codes */
/* DFU init pins are not configured properly */
#define CYBOOT_INVALID_DFU_PINS_CONFIGURATION   CYBOOT_CREATE_FAIL(0x00B0UL)
/* DFU init flow failed */
#define CYBOOT_DFU_INIT_FAILED                  CYBOOT_CREATE_FAIL(0x00B1UL)
/* DFU enters unexpected state */
#define CYBOOT_DFU_UNEXPECTED_STATE             CYBOOT_CREATE_FAIL(0x00B2UL)
/* DFU received unexpected failure from CYLIB */
#define CYBOOT_DFU_CYLIB_UNEXPECTED_FAILURE     CYBOOT_CREATE_FAIL(0x00B3UL)
/* DFU is launched not in PC1 */
#define CYBOOT_DFU_INVALID_PC                   CYBOOT_CREATE_FAIL(0x00B4UL)
/* DFU launched RAM application */
#define CYBOOT_DFU_RAM_APP_LAUNCHED             CYBOOT_CREATE_SUCCESS(0x00B5UL)

/* Misc error codes, 0xC0-0xFF */
/** Full trims, invalid size */
#define CYBOOT_FULL_TRIMS_SIZE_INVALID          CYBOOT_CREATE_FAIL(0x00C0UL)

/* Failed signature validation conter */
#define CYBOOT_SIGNATURE_COUNTER_REACHED_ZERO   CYBOOT_CREATE_FAIL(0x00C1UL)


/**
* \} cyboot_result
*/

#endif /* CYBOOT_RESULT_H */

/* [] END OF FILE */
