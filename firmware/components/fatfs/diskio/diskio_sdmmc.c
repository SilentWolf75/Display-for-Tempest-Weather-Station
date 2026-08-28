/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "diskio_impl.h"
#include "ffconf.h"
#include "ff.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "esp_compiler.h"

static sdmmc_card_t* s_cards[FF_VOLUMES] = { NULL };
static bool s_disk_status_check_en[FF_VOLUMES] = { };

static const char* TAG = "diskio_sdmmc";

//Check if SD/MMC card is present
static DSTATUS ff_sdmmc_card_available(BYTE pdrv)
{
    sdmmc_card_t* card = s_cards[pdrv];
    assert(card);
    esp_err_t err = sdmmc_get_status(card);
    if (unlikely(err != ESP_OK)) {
        ESP_LOGE(TAG, "Check status failed (0x%x)", err);
        return STA_NOINIT;
    }
    return 0;
}

/**
*   ff_sdmmc_status() and ff_sdmmc_initialize() return STA_NOINIT when sdmmc_get_status()
*   fails. This error value is checked throughout the FATFS code.
*   Both functions return 0 on success.
*/
static DSTATUS ff_sdmmc_initialize (BYTE pdrv)
{
    return ff_sdmmc_card_available(pdrv);
}

static DSTATUS ff_sdmmc_status(BYTE pdrv)
{
    if (s_disk_status_check_en[pdrv]) {
        return ff_sdmmc_card_available(pdrv);
    }
    return 0;
}

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

static DRESULT ff_sdmmc_read (BYTE pdrv, BYTE* buff, DWORD sector, UINT count)
{
    sdmmc_card_t* card = s_cards[pdrv];
    assert(card);
    esp_err_t err;
    if (!esp_ptr_internal(buff) || ((uintptr_t)buff % 4 != 0)) {
        size_t size = count * card->csd.sector_size;
        BYTE *dma_buf = heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (dma_buf) {
            err = sdmmc_read_sectors(card, dma_buf, sector, count);
            if (err == ESP_OK) {
                memcpy(buff, dma_buf, size);
            }
            free(dma_buf);
        } else {
            BYTE *single_buf = heap_caps_malloc(card->csd.sector_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!single_buf) return RES_ERROR;
            err = ESP_OK;
            for (UINT i = 0; i < count; i++) {
                err = sdmmc_read_sectors(card, single_buf, sector + i, 1);
                if (err != ESP_OK) break;
                memcpy(buff + i * card->csd.sector_size, single_buf, card->csd.sector_size);
            }
            free(single_buf);
        }
    } else {
        err = sdmmc_read_sectors(card, buff, sector, count);
    }
    if (unlikely(err != ESP_OK)) {
        ESP_LOGE(TAG, "sdmmc_read_blocks failed (0x%x)", err);
        return RES_ERROR;
    }
    return RES_OK;
}

static DRESULT ff_sdmmc_write (BYTE pdrv, const BYTE* buff, DWORD sector, UINT count)
{
    sdmmc_card_t* card = s_cards[pdrv];
    assert(card);
    esp_err_t err;
    if (!esp_ptr_internal(buff) || ((uintptr_t)buff % 4 != 0)) {
        size_t size = count * card->csd.sector_size;
        BYTE *dma_buf = heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (dma_buf) {
            memcpy(dma_buf, buff, size);
            err = sdmmc_write_sectors(card, dma_buf, sector, count);
            free(dma_buf);
        } else {
            BYTE *single_buf = heap_caps_malloc(card->csd.sector_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!single_buf) return RES_ERROR;
            err = ESP_OK;
            for (UINT i = 0; i < count; i++) {
                memcpy(single_buf, buff + i * card->csd.sector_size, card->csd.sector_size);
                err = sdmmc_write_sectors(card, single_buf, sector + i, 1);
                if (err != ESP_OK) break;
            }
            free(single_buf);
        }
    } else {
        err = sdmmc_write_sectors(card, buff, sector, count);
    }
    if (unlikely(err != ESP_OK)) {
        ESP_LOGE(TAG, "sdmmc_write_blocks failed (0x%x)", err);
        return RES_ERROR;
    }
    return RES_OK;
}

#if FF_USE_TRIM
static DRESULT ff_sdmmc_trim (BYTE pdrv, DWORD start_sector, DWORD sector_count)
{
    sdmmc_card_t* card = s_cards[pdrv];
    assert(card);
    sdmmc_erase_arg_t arg;

    arg = sdmmc_can_discard(card) == ESP_OK ? SDMMC_DISCARD_ARG : SDMMC_ERASE_ARG;
    esp_err_t err = sdmmc_erase_sectors(card, start_sector, sector_count, arg);
    if (unlikely(err != ESP_OK)) {
        ESP_LOGE(TAG, "sdmmc_erase_sectors failed (%d)", err);
        return RES_ERROR;
    }
    return RES_OK;
}
#endif //FF_USE_TRIM

static DRESULT ff_sdmmc_ioctl (BYTE pdrv, BYTE cmd, void* buff)
{
    sdmmc_card_t* card = s_cards[pdrv];
    assert(card);
    switch(cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *((DWORD*) buff) = card->csd.capacity;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *((WORD*) buff) = card->csd.sector_size;
            return RES_OK;
        case GET_BLOCK_SIZE:
            return RES_ERROR;
#if FF_USE_TRIM
        case CTRL_TRIM:
            if (sdmmc_can_trim(card) != ESP_OK) {
                return RES_PARERR;
            }
            return ff_sdmmc_trim (pdrv, *((DWORD*)buff), //start_sector
                    (*((DWORD*)buff + 1) - *((DWORD*)buff) + 1)); //sector_count
#endif //FF_USE_TRIM
    }
    return RES_ERROR;
}

void ff_sdmmc_set_disk_status_check(BYTE pdrv, bool enable)
{
    s_disk_status_check_en[pdrv] = enable;
}

void ff_diskio_register_sdmmc(BYTE pdrv, sdmmc_card_t* card)
{
    static const ff_diskio_impl_t sdmmc_impl = {
        .init = &ff_sdmmc_initialize,
        .status = &ff_sdmmc_status,
        .read = &ff_sdmmc_read,
        .write = &ff_sdmmc_write,
        .ioctl = &ff_sdmmc_ioctl
    };
    s_cards[pdrv] = card;
    s_disk_status_check_en[pdrv] = false;
    ff_diskio_register(pdrv, &sdmmc_impl);
}

BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t* card)
{
    for (int i = 0; i < FF_VOLUMES; i++) {
        if (card == s_cards[i]) {
            return i;
        }
    }
    return 0xff;
}
