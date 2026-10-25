#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/lock.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

#include "ssh1106.h"
#include "i2c.h"

#define SH1106_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define SH1106_RAM_COLUMN_OFFSET 2
#define LVGL_TICK_PERIOD_MS 5
#define LVGL_PALETTE_SIZE 8

static uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];
static uint8_t lvgl_buffer[OLED_WIDTH * OLED_HEIGHT / 8 + LVGL_PALETTE_SIZE];
static _lock_t lvgl_lock;
static i2c_master_dev_handle_t oled_device;
static lv_obj_t *line_labels[3];
static bool lvgl_ready;
static const char *const *list_items;
static size_t list_item_count;
static size_t list_index;
static bool list_active;

static void set_text_lines_locked(const char *first_line,
                                  const char *second_line,
                                  const char *third_line)
{
    const char *lines[] = {first_line, second_line, third_line};
    for (int line = 0; line < 3; ++line)
    {
        lv_label_set_text(line_labels[line], lines[line] ? lines[line] : "");
    }
}

static void render_list_locked(void)
{
    const char *first_visible = "No devices";
    const char *second_visible = "";

    if (list_item_count > 0)
    {
        first_visible = list_items[list_index] ? list_items[list_index] : "";
        if (list_index + 1 < list_item_count)
        {
            second_visible = list_items[list_index + 1] ? list_items[list_index + 1] : "";
        }
    }

    set_text_lines_locked("Jack -> Bluetooth", first_visible, second_visible);
}

/*
 * SH1106 displays use a 132-column RAM, while the visible 128-column area is
 * normally connected to columns 2 through 129.  Unlike the SSD1306 panel
 * driver, write each display page explicitly with the required column offset.
 */
static esp_err_t sh1106_write_commands(const uint8_t *commands, size_t command_count)
{
    uint8_t packet[32];
    if (command_count + 1 > sizeof(packet))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    packet[0] = 0x00; // I2C control byte: command stream
    memcpy(&packet[1], commands, command_count);
    return i2c_master_transmit(oled_device, packet, command_count + 1, 1000);
}

static esp_err_t sh1106_flush(void)
{
    uint8_t packet[OLED_WIDTH + 1];
    packet[0] = 0x40; // I2C control byte: display data stream

    for (uint8_t page = 0; page < OLED_HEIGHT / 8; ++page)
    {
        const uint8_t column = SH1106_RAM_COLUMN_OFFSET;
        const uint8_t commands[] = {
            (uint8_t)(0xB0 | page),
            (uint8_t)(0x00 | (column & 0x0F)),
            (uint8_t)(0x10 | (column >> 4)),
        };
        esp_err_t ret = sh1106_write_commands(commands, sizeof(commands));
        if (ret != ESP_OK)
        {
            return ret;
        }

        memcpy(&packet[1], &oled_buffer[page * OLED_WIDTH], OLED_WIDTH);
        ret = i2c_master_transmit(oled_device, packet, sizeof(packet), 1000);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return ESP_OK;
}

static void oled_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map)
{
    pixel_map += LVGL_PALETTE_SIZE; // LVGL reserves an I1 palette at the buffer start.

    const int width = lv_display_get_physical_horizontal_resolution(display);
    for (int y = area->y1; y <= area->y2; ++y)
    {
        for (int x = area->x1; x <= area->x2; ++x)
        {
            bool pixel_on = pixel_map[(width / 8) * y + (x / 8)] & (1U << (7 - x % 8));
            uint8_t *oled_pixel = &oled_buffer[width * (y / 8) + x];
            if (pixel_on)
            {
                *oled_pixel &= ~(1U << (y % 8));
            }
            else
            {
                *oled_pixel |= 1U << (y % 8);
            }
        }
    }
    i2c_lock();
    esp_err_t ret = sh1106_flush();
    i2c_unlock();
    if (ret != ESP_OK)
    {
        ESP_LOGE("OLED", "SH1106 update failed: %s", esp_err_to_name(ret));
    }
    lv_display_flush_ready(display);
}

static void lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    while (true)
    {
        _lock_acquire(&lvgl_lock);
        uint32_t delay_ms = lv_timer_handler();
        _lock_release(&lvgl_lock);
        if (delay_ms < 5)
        {
            delay_ms = 5;
        }
        else if (delay_ms > 500)
        {
            delay_ms = 500;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t ssd1306_show_name(void)
{
    const i2c_device_config_t device_config = {
        .device_address = SH1106_ADDRESS,
        .scl_speed_hz = 400 * 1000,
    };
    esp_err_t ret;

    i2c_lock();
    ret = i2c_master_bus_add_device(i2c_get_bus(), &device_config, &oled_device);
    if (ret != ESP_OK)
    {
        goto unlock_i2c;
    }

    const uint8_t init_commands[] = {
        0xAE, // Display off
        0xD5,
        0x80, // Display clock divide ratio / oscillator frequency
        0xA8,
        0x3F, // 1/64 multiplex ratio
        0xD3,
        0x00, // Display offset
        0x40, // Display start line 0
        0xAD,
        0x8B, // Enable SH1106 internal DC-DC converter
        0xA1, // Segment remap
        0xC8, // Reverse COM scan direction
        0xDA,
        0x12, // COM pins for 128x64 panel
        0x81,
        0x7F, // Contrast
        0xD9,
        0x22, // Pre-charge period
        0xDB,
        0x35, // VCOM deselect level
        0xA4, // Resume display from RAM
        0xA6, // Normal (non-inverted) display
        0xAF, // Display on
    };
    ret = sh1106_write_commands(init_commands, sizeof(init_commands));
    if (ret == ESP_OK)
    {
        memset(oled_buffer, 0x00, sizeof(oled_buffer));
        ret = sh1106_flush();
    }
    i2c_unlock();
    if (ret != ESP_OK)
    {
        ESP_LOGE("OLED", "SH1106 initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    lv_init();
    lv_display_t *display = lv_display_create(OLED_WIDTH, OLED_HEIGHT);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(display, lvgl_buffer, NULL, sizeof(lvgl_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, oled_flush);

    esp_timer_handle_t tick_timer = NULL;
    const esp_timer_create_args_t timer_config = {
        .callback = lvgl_tick,
        .name = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_config, &tick_timer), "OLED", "create LVGL timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000), "OLED", "start LVGL timer");

    for (int line = 0; line < 3; ++line)
    {
        line_labels[line] = lv_label_create(lv_screen_active());
        lv_obj_set_width(line_labels[line], OLED_WIDTH);
        lv_obj_set_y(line_labels[line], 3 + line * 20);
        lv_obj_set_style_text_align(line_labels[line], LV_TEXT_ALIGN_CENTER, 0);
        // Keep each message on one line. Text wider than 128 pixels scrolls
        // continuously from right to left instead of wrapping.
        lv_label_set_long_mode(line_labels[line], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_anim_duration(line_labels[line], 6000, 0);
    }
    lvgl_ready = true;
    ESP_RETURN_ON_ERROR(ssd1306_set_text_lines("Paul", "", ""), "OLED", "set initial text");

    BaseType_t task_created = xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 2, NULL);
    return task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;

unlock_i2c:
    i2c_unlock();
    ESP_LOGE("OLED", "create SH1106 I2C device failed: %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t ssd1306_set_text_lines(const char *first_line,
                                 const char *second_line,
                                 const char *third_line)
{
    if (!lvgl_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    _lock_acquire(&lvgl_lock);
    list_active = false;
    set_text_lines_locked(first_line, second_line, third_line);
    _lock_release(&lvgl_lock);
    return ESP_OK;
}

esp_err_t ssd1306_show_string_list(const char *const items[], size_t item_count)
{
    if (!lvgl_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (items == NULL && item_count != 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    _lock_acquire(&lvgl_lock);
    list_items = items;
    list_item_count = item_count;
    list_index = 0;
    list_active = true;
    render_list_locked();
    _lock_release(&lvgl_lock);
    return ESP_OK;
}

esp_err_t ssd1306_list_next(void)
{
    if (!lvgl_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    _lock_acquire(&lvgl_lock);
    if (!list_active)
    {
        _lock_release(&lvgl_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (list_index + 1 < list_item_count)
    {
        ++list_index;
        render_list_locked();
    }
    _lock_release(&lvgl_lock);
    return ESP_OK;
}

esp_err_t ssd1306_list_back(void)
{
    if (!lvgl_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }

    _lock_acquire(&lvgl_lock);
    if (!list_active)
    {
        _lock_release(&lvgl_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (list_index > 0)
    {
        --list_index;
        render_list_locked();
    }
    _lock_release(&lvgl_lock);
    return ESP_OK;
}
