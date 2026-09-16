/*
 * flash_map.h
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */

#ifndef INC_FLASH_MAP_H_
#define INC_FLASH_MAP_H_

/* Bootloader region */
#define BOOTLOADER_ADDR 0x08000000UL
#define BOOTLOADER_SIZE (32*1024UL)

/*active Application slot */
#define ACTIVE_APP_ADDR 0x08008000UL
#define ACTIVE_APP_SIZE (480*1024UL)

/*staging slot*/
#define STAGING_ADDR 0x08080000UL
#define STAGING_SIZE (480*1024UL)

/*metadata sector */
#define METADATA_ADDR 0x080F8000UL
#define METADATA_SIZE (32*1024 UL)

#endif /* INC_FLASH_MAP_H_ */
