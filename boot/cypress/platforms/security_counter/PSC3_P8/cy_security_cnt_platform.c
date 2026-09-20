/*
 * Copyright (c) 2020 Arm Limited.
 * Copyright (c) 2021 Infineon Technologies AG
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#if defined MCUBOOT_HW_ROLLBACK_PROT

#include <stdint.h>
#include <string.h>

#include "cy_security_cnt_platform.h"
#include "rollback_counter.h"
#include "sha_port.h"
#include "cy_flash.h"
#include "cy_cryptolite_common.h"

/**
 * Reads a data corresponding to security counter which is stored in
 * efuses of chip and converts it actual value of security counter
 *
 * @param image_id         Image id counter assisiated with
 * @param security_cnt     Pointer to a variable, where security counter value would be stored
 *
 * @return                 FIH_SUCCESS on success; FIH_FAILURE on failure.
 */
fih_int platform_security_counter_get(uint32_t image_id, fih_uint *security_cnt)
{
    (void)image_id;

    fih_int fih_rc = FIH_SUCCESS;
    uint32_t result;

    cy_rbc_recover_rollback_counter();

    result = cy_rbc_read_rollback_counter(image_id);

    *security_cnt = fih_uint_encode(result);

    FIH_RET(fih_rc);
}

/**
 * Updates the stored value of a given image's security counter with a new
 * security counter value if the new one is greater.
 *
 * @param image_id          Image id counter assisiated with
 * @param img_security_cnt  Counter value
 * @param custom_data       Pointer to supplemental data (API), not used in this implementation
 *
 * @return                  0 on success; nonzero on failure.
 */
int32_t platform_security_counter_update(uint32_t image_id, fih_uint img_security_cnt, uint8_t *custom_data)
{
    (void) custom_data;

    int32_t rc = -1;

    if (cy_rbc_update_rollback_counter(image_id, fih_uint_decode(img_security_cnt)) == CY_RBC_SUCCESS)
    {
    	rc = 0;
    }

    return rc;
}

cy_rbc_result_t cy_rbc_calc_check_sum(const uint32_t *data, uint32_t dataSize, uint8_t *checkSum)
{
    cy_rbc_result_t rc = CY_RBC_SUCCESS;

    /*
     * Always use SHA-256 for rollback counter checksum.
     */
    do {
        bootutil_sha_context sha_ctx;
        uint8_t hash[CY_CRYPTOLITE_SHA256_HASH_SIZE];

        if (bootutil_sha256_init(&sha_ctx) != 0) {
            rc = CY_RBC_INVALID;
            break;
        }

        if (bootutil_sha256_update(&sha_ctx, data, dataSize) != 0) {
            rc = CY_RBC_INVALID;
            break;
        }

        if (bootutil_sha256_finish(&sha_ctx, hash) != 0) {
            rc = CY_RBC_INVALID;
            break;
        }

        memcpy(checkSum, hash, CY_CRYPTOLITE_SHA256_HASH_SIZE);

    } while (false);

    return rc;
}

cy_rbc_result_t cy_rbc_program(const uint8_t *src, uintptr_t offset)
{
    uint32_t buf[(MEMORY_ALIGN + 16) / 4U];

    memcpy(buf, src, MEMORY_ALIGN);

    if (Cy_Flash_EraseRow(offset) == CY_FLASH_DRV_SUCCESS) {
        if (Cy_Flash_ProgramRow(offset, buf) == CY_FLASH_DRV_SUCCESS) {
            return CY_RBC_SUCCESS;
        }
    }

    return CY_RBC_INVALID;
}

cy_rbc_result_t cy_rbc_erase(uintptr_t offset)
{
    if (Cy_Flash_EraseRow(offset) == CY_FLASH_DRV_SUCCESS) {
        return CY_RBC_SUCCESS;
    }

    return CY_RBC_INVALID;
}

bool cy_rbc_is_erased(const uint8_t *data)
{
    for (uint32_t i = 0U; i < MEMORY_ALIGN; i++) {
        if (data[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

#endif /* defined MCUBOOT_HW_ROLLBACK_PROT */
