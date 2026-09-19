/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

// Non volatile storage drivers
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_log.h"

// Bluetooth
#include "bt_app_core.h"
#include "bt_app_a2dp_source.h"

/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/

static void init_non_volatile_storage()
{
    /* initialize NVS (Non-volatile storage) — it is used to store PHY calibration data */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
};

void app_main(void)
{
    printf("Hello world!\n");

    ///////////////////////
    //     Bluetooth     //
    ///////////////////////

    init_non_volatile_storage();

    bt_app_init();

    bt_app_task_start_up();

    ESP_ERROR_CHECK(bt_app_a2dp_source_start());
}
