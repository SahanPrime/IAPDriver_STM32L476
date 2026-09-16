/*
 * metadata.c
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */


#include "metadata.h"
#include <string.h>

void metadata_read(boot_metadata_t *meta)
{
    /* metadata sector is memory-mapped, just read it directly */
    memcpy(meta, (void *)METADATA_ADDR, sizeof(boot_metadata_t));
}

HAL_StatusTypeDef metadata_write(const boot_metadata_t *meta)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0;

    HAL_FLASH_Unlock();

    /* clear any leftover error flags before starting - good practice on L4 */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    /* erase the metadata page first - STM32L4 requires erase before write */
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks     = FLASH_BANK_1;
    erase_init.Page      = METADATA_PAGE;
    erase_init.NbPages   = 1;

    status = HAL_FLASHEx_Erase(&erase_init, &page_error);
    if (status != HAL_OK) {
        HAL_FLASH_Lock();
        return status;
    }
    /* write struct as double-words (8 bytes at a time) */
    const uint64_t *src = (const uint64_t *)meta;
    uint32_t addr = METADATA_ADDR;
    uint32_t num_dwords = sizeof(boot_metadata_t) / 8U;

    for (uint32_t i = 0; i < num_dwords; i++) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, src[i]);
        if (status != HAL_OK) {
            break;
        }
        addr += 8U;
    }

    HAL_FLASH_Lock();
    return status;
}

void metadata_init_if_needed(void)
    {
        boot_metadata_t meta;

        metadata_read(&meta);

        if (meta.magic != METADATA_MAGIC) {
            /* sector has never been initialized (fresh chip, erased flash reads as 0xFFFFFFFF) */
            boot_metadata_t default_meta = {
                .magic           = METADATA_MAGIC,
                .staging_valid   = 0,
                .staging_size    = 0,
                .staging_crc     = 0,
                .apply_requested = 0,
                .app_version     = 0
            };

            metadata_write(&default_meta);
        }
    }

