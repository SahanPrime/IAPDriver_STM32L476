/*
 * iap_apply.c
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */

#include "iap_apply.h"
#include "flash_map.h"
#include "metadata.h"
#include "crc_util.h"
#include <string.h>

static HAL_StatusTypeDef erase_active_region(uint32_t size);
static HAL_StatusTypeDef copy_staging_to_active(uint32_t size);

void iap_check_and_apply_update(void)
{
    boot_metadata_t meta;
    metadata_read(&meta);

    if (meta.apply_requested == 0) {
        return;   /* nothing to do */
    }

    /* re-verify: never trust the Application's earlier CRC claim -
       compute fresh, directly over what's actually sitting in flash now */
    uint32_t actual_crc = crc32_zlib_compatible((const uint8_t *)STAGING_ADDR, meta.staging_size);

    if (actual_crc != meta.staging_crc) {
        /* corrupted or tampered staging image - discard it entirely,
           leave Active untouched, don't retry on next boot */
        meta.staging_valid   = 0;
        meta.apply_requested = 0;
        metadata_write(&meta);
        return;
    }

    if (erase_active_region(meta.staging_size) != HAL_OK) {
        /* erase failed - leave flags as-is so it can be retried;
           Active may now be partially erased, which is a serious state,
           but there's nothing safer to do here with this design */
        return;
    }

    if (copy_staging_to_active(meta.staging_size) != HAL_OK) {
        /* copy failed partway - Active is now in an inconsistent state.
           Clear apply_requested so it doesn't loop forever attempting
           the same failing copy; staging_valid stays so the image
           itself is still known-good for a future retry mechanism. */
        meta.apply_requested = 0;
        metadata_write(&meta);
        return;
    }

    /* success - clear both flags, update is now installed */
    meta.staging_valid   = 0;
    meta.apply_requested = 0;
    metadata_write(&meta);
}

static HAL_StatusTypeDef erase_active_region(uint32_t size)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0;

    uint32_t active_start_page = (ACTIVE_APP_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE;
    uint32_t num_pages_needed  = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks     = FLASH_BANK_1;
    erase_init.Page      = active_start_page;
    erase_init.NbPages   = num_pages_needed;

    status = HAL_FLASHEx_Erase(&erase_init, &page_error);

    HAL_FLASH_Lock();
    return status;
}

static HAL_StatusTypeDef copy_staging_to_active(uint32_t size)
{
    HAL_StatusTypeDef status = HAL_OK;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    const uint64_t *src = (const uint64_t *)STAGING_ADDR;
    uint32_t dst_addr = ACTIVE_APP_ADDR;
    uint32_t num_dwords = (size + 7U) / 8U;   /* round up - staging was padded to a double-word too */

    for (uint32_t i = 0; i < num_dwords; i++) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, dst_addr, src[i]);
        if (status != HAL_OK) {
            break;
        }
        dst_addr += 8U;
    }

    HAL_FLASH_Lock();
    return status;
}
