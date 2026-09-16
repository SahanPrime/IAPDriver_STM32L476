/*
 * crc_util.c
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */


#include "crc_util.h"

static CRC_HandleTypeDef hcrc;

void CRC_Init_Zlib_Compatible(void)
{
    hcrc.Instance = CRC;

    hcrc.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;   /* 0x04C11DB7 */
    hcrc.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_DISABLE;
    hcrc.Init.InitValue               = 0xFFFFFFFF;
    hcrc.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_BYTE;
    hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_ENABLE;
    hcrc.InputDataFormat              = CRC_INPUTDATA_FORMAT_BYTES;

    if (HAL_CRC_Init(&hcrc) != HAL_OK) {
        /* CRC peripheral clock likely not enabled - check __HAL_RCC_CRC_CLK_ENABLE() was called */
        Error_Handler();
    }
}

uint32_t crc32_zlib_compatible(const uint8_t *data, uint32_t length)
{
    uint32_t result = HAL_CRC_Calculate(&hcrc, (uint32_t *)data, length);
    return result ^ 0xFFFFFFFF;   /* final XOR, matching zlib.crc32() */
}
