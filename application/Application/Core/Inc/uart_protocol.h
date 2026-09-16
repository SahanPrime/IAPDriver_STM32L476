/*
 * uart_protocol.h
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */

#ifndef INC_UART_PROTOCOL_H_
#define INC_UART_PROTOCOL_H_

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include "stm32l4xx_hal.h"

/* ---- Protocol constants ---- */
#define UART_START_BYTE     0xAA

#define CMD_START_UPDATE    0x01
#define CMD_WRITE_CHUNK     0x02
#define CMD_END_UPDATE      0x03
#define CMD_ACK             0x04
#define CMD_NACK            0x05

#define MAX_PAYLOAD_SIZE        256U
#define RX_PACKET_TIMEOUT_MS    500U

/* ---- Packet receive state machine ---- */
typedef enum {
    RX_WAIT_START,
    RX_WAIT_CMD,
    RX_WAIT_LEN_HI,
    RX_WAIT_LEN_LO,
    RX_WAIT_PAYLOAD,
    RX_WAIT_CRC,
    RX_PACKET_READY
} rx_state_t;

typedef struct {
    uint8_t  cmd;
    uint16_t length;
    uint8_t  payload[MAX_PAYLOAD_SIZE];
    uint32_t crc32;
} uart_packet_t;

/* ---- Update-in-progress tracking ---- */
typedef enum {
    UPDATE_IDLE,
    UPDATE_IN_PROGRESS
} update_state_t;

/* ---- Public API ---- */
void uart_protocol_init(UART_HandleTypeDef *huart);        /* stores huart handle, arms first RX */
void uart_rx_byte_handler(uint8_t byte);                    /* call from HAL_UART_RxCpltCallback */
void main_loop_process_uart(void);                          /* call every iteration of the superloop */

#endif /* UART_PROTOCOL_H */

#endif /* INC_UART_PROTOCOL_H_ */
