/*
 * crc_util.h
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */

#ifndef INC_CRC_UTIL_H_
#define INC_CRC_UTIL_H_

#include <stdint.h>
#include "stm32l4xx_hal.h"

void CRC_Init_Zlib_Compatible(void);
uint32_t crc32_zlib_compatible(const uint8_t *data, uint32_t length);


#endif /* INC_CRC_UTIL_H_ */
