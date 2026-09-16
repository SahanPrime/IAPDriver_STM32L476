/*
 * uart_protocol.c
 *
 *  Created on: Sep 16, 2026
 *      Author: Lenovo
 */
#include "uart_protocol.h"
#include "flash_map.h"
#include "metadata.h"
#include "crc_util.h"
#include <string.h>

/* ---- UART handle ---- */
static UART_HandleTypeDef *s_huart = NULL;
static uint8_t rx_byte;

/* ---- Packet parser state ---- */
static rx_state_t    rx_state = RX_WAIT_START;
static uart_packet_t rx_packet;
static uint16_t      rx_payload_idx = 0;
static uint8_t       rx_crc_bytes[4];
static uint8_t       rx_crc_idx = 0;
static uint32_t      rx_last_byte_tick = 0;

/* ---- Update transfer state ---- */
static update_state_t update_state = UPDATE_IDLE;
static uint32_t staging_write_offset = 0;
static uint32_t expected_total_size  = 0;
static uint32_t expected_final_crc   = 0;

/* ---- Forward declarations (internal) ---- */
static void handle_packet(const uart_packet_t *pkt);
static void handle_start_update(const uart_packet_t *pkt);
static void handle_write_chunk(const uart_packet_t *pkt);
static void handle_end_update(const uart_packet_t *pkt);
static HAL_StatusTypeDef erase_staging_region(uint32_t size);
static HAL_StatusTypeDef staging_write(uint32_t offset, const uint8_t *data, uint16_t len);
static void send_ack(void);
static void send_nack(void);

/* =================== Public API =================== */

void uart_protocol_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    HAL_UART_Receive_IT(s_huart, &rx_byte, 1);
}

void uart_rx_byte_handler(uint8_t byte)
{
    rx_last_byte_tick = HAL_GetTick();

    switch (rx_state) {

    case RX_WAIT_START:
        if (byte == UART_START_BYTE) {
            rx_state = RX_WAIT_CMD;
        }
        break;

    case RX_WAIT_CMD:
        rx_packet.cmd = byte;
        rx_state = RX_WAIT_LEN_HI;
        break;

    case RX_WAIT_LEN_HI:
        rx_packet.length = ((uint16_t)byte) << 8;
        rx_state = RX_WAIT_LEN_LO;
        break;

    case RX_WAIT_LEN_LO:
        rx_packet.length |= byte;
        if (rx_packet.length > MAX_PAYLOAD_SIZE) {
            rx_state = RX_WAIT_START;
            break;
        }
        rx_payload_idx = 0;
        rx_state = (rx_packet.length > 0) ? RX_WAIT_PAYLOAD : RX_WAIT_CRC;
        rx_crc_idx = 0;
        break;

    case RX_WAIT_PAYLOAD:
        rx_packet.payload[rx_payload_idx++] = byte;
        if (rx_payload_idx >= rx_packet.length) {
            rx_state = RX_WAIT_CRC;
            rx_crc_idx = 0;
        }
        break;

    case RX_WAIT_CRC:
        rx_crc_bytes[rx_crc_idx++] = byte;
        if (rx_crc_idx >= 4) {
            rx_packet.crc32 = ((uint32_t)rx_crc_bytes[0] << 24) |
                               ((uint32_t)rx_crc_bytes[1] << 16) |
                               ((uint32_t)rx_crc_bytes[2] << 8)  |
                               ((uint32_t)rx_crc_bytes[3]);
            rx_state = RX_PACKET_READY;
        }
        break;

    case RX_PACKET_READY:
        /* main loop hasn't consumed the previous packet yet - drop this byte */
        break;
    }
}

void main_loop_process_uart(void)
{
    if (rx_state != RX_WAIT_START && rx_state != RX_PACKET_READY) {
        if ((HAL_GetTick() - rx_last_byte_tick) > RX_PACKET_TIMEOUT_MS) {
            rx_state = RX_WAIT_START;
        }
    }

    if (rx_state == RX_PACKET_READY) {
        uint32_t computed_crc = crc32_zlib_compatible(rx_packet.payload, rx_packet.length);

        if (computed_crc == rx_packet.crc32) {
            handle_packet(&rx_packet);
            send_ack();
        } else {
            send_nack();
        }

        rx_state = RX_WAIT_START;
    }
}

/* =================== Packet dispatch =================== */

static void handle_packet(const uart_packet_t *pkt)
{
    switch (pkt->cmd) {
    case CMD_START_UPDATE:
        handle_start_update(pkt);
        break;
    case CMD_WRITE_CHUNK:
        handle_write_chunk(pkt);
        break;
    case CMD_END_UPDATE:
        handle_end_update(pkt);
        break;
    default:
        break;
    }
}

static void handle_start_update(const uart_packet_t *pkt)
{
    if (pkt->length != 8) {
        return;
    }

    expected_total_size = ((uint32_t)pkt->payload[0] << 24) |
                           ((uint32_t)pkt->payload[1] << 16) |
                           ((uint32_t)pkt->payload[2] << 8)  |
                           ((uint32_t)pkt->payload[3]);

    expected_final_crc  = ((uint32_t)pkt->payload[4] << 24) |
                           ((uint32_t)pkt->payload[5] << 16) |
                           ((uint32_t)pkt->payload[6] << 8)  |
                           ((uint32_t)pkt->payload[7]);

    if (expected_total_size > STAGING_SIZE) {
        return;
    }

    if (erase_staging_region(expected_total_size) != HAL_OK) {
        return;
    }

    staging_write_offset = 0;
    update_state = UPDATE_IN_PROGRESS;
}

static void handle_write_chunk(const uart_packet_t *pkt)
{
    if (update_state != UPDATE_IN_PROGRESS) {
        return;
    }

    if (staging_write_offset + pkt->length > expected_total_size + 7U) {
        /* +7 tolerance for the final chunk's padding up to a double-word */
        return;
    }

    if (staging_write(staging_write_offset, pkt->payload, pkt->length) == HAL_OK) {
        staging_write_offset += pkt->length;
    }
}

static void handle_end_update(const uart_packet_t *pkt)
{
    (void)pkt;

    if (update_state != UPDATE_IN_PROGRESS) {
        return;
    }

    /* staging_write_offset may be slightly beyond expected_total_size due to
       final-chunk padding - only the real size matters for CRC/copy */
    if (staging_write_offset < expected_total_size) {
        update_state = UPDATE_IDLE;
        return;
    }

    uint32_t actual_crc = crc32_zlib_compatible((const uint8_t *)STAGING_ADDR, expected_total_size);

    if (actual_crc == expected_final_crc) {
        boot_metadata_t meta;
        metadata_read(&meta);
        meta.staging_valid = 1;
        meta.staging_size  = expected_total_size;
        meta.staging_crc   = expected_final_crc;
        metadata_write(&meta);
    }

    update_state = UPDATE_IDLE;
}

/* =================== Staging flash access =================== */

static HAL_StatusTypeDef erase_staging_region(uint32_t size)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0;

    uint32_t staging_start_page = (STAGING_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE;
    uint32_t num_pages_needed   = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks     = FLASH_BANK_1;
    erase_init.Page      = staging_start_page;
    erase_init.NbPages   = num_pages_needed;

    status = HAL_FLASHEx_Erase(&erase_init, &page_error);

    HAL_FLASH_Lock();
    return status;
}

static HAL_StatusTypeDef staging_write(uint32_t offset, const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status = HAL_OK;

    if (len % 8U != 0U) {
        return HAL_ERROR;
    }

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    const uint64_t *src = (const uint64_t *)data;
    uint32_t addr = STAGING_ADDR + offset;
    uint32_t num_dwords = len / 8U;

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

/* =================== ACK / NACK =================== */

static void send_ack(void)
{
    uint8_t packet[] = { UART_START_BYTE, CMD_ACK, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    HAL_UART_Transmit(s_huart, packet, sizeof(packet), HAL_MAX_DELAY);
}

static void send_nack(void)
{
    uint8_t packet[] = { UART_START_BYTE, CMD_NACK, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    HAL_UART_Transmit(s_huart, packet, sizeof(packet), HAL_MAX_DELAY);
}

/* =================== HAL callback wiring =================== */
/* If stm32l4xx_it.c doesn't already implement this, add it there instead -
   HAL only allows one definition of this callback in the whole project. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == s_huart->Instance) {
        uart_rx_byte_handler(rx_byte);
        HAL_UART_Receive_IT(s_huart, &rx_byte, 1);
    }
}

