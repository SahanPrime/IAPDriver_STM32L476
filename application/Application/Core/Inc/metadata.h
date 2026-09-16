/*
 * metadata.h
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */

#ifndef INC_METADATA_H_
#define INC_METADATA_H_

#include <stdint.h>
#include "flash_map.h"
#include "stm32l4xx_hal.h"

#define METADATA_MAGIC 0xDEADBEEFUL
#define METADATA_PAGE 496U

void metadata_init_if_needed(void);

typedef struct{
	uint32_t magic;
	uint32_t staging_valid;
	uint32_t staging_size;
	uint32_t staging_crc;
	uint32_t apply_requested;
	uint32_t app_version;
}boot_metadata_t;

void metadata_read(boot_metadata_t *meta);
HAL_StatusTypeDef metadata_write(const boot_metadata_t *meta);

#endif /* INC_METADATA_H_ */
