/*
 * iap_apply.h
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */

#ifndef INC_IAP_APPLY_H_
#define INC_IAP_APPLY_H_

#include <stdint.h>
#include "stm32l4xx_hal.h"

/* Checks apply_requested flag; if set, verifies and copies Staging -> Active.
   Always returns - caller jumps to Active app afterward regardless of outcome. */
void iap_check_and_apply_update(void);



#endif /* INC_IAP_APPLY_H_ */
