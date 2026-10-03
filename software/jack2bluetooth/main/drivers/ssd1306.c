#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/lock.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "ssd1306.h"
#include "i2c.h"

#define SSD1306_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define LVGL_TICK_PERIOD_MS 5
#define LVGL_PALETTE_SIZE 8

static uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];
static uint8_t lvgl_buffer[OLED_WIDTH * OLED_HEIGHT / 8 + LVGL_PALETTE_SIZE];
static _lock_t lvgl_lock;

static bool oled_flush_complete(esp_lcd_panel_io_handle_t io_panel,
                                esp_lcd_panel_io_event_data_t *event_data,
                                void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void oled_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(display);
    pixel_map += LVGL_PALETTE_SIZE; // LVGL reserves an I1 palette at the buffer start.

    const int width = lv_display_get_physical_horizontal_resolution(display);
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            bool pixel_on = pixel_map[(width / 8) * y + (x / 8)] & (1U << (7 - x % 8));
            uint8_t *oled_pixel = &oled_buffer[width * (y / 8) + x];
            if (pixel_on) {
                *oled_pixel &= ~(1U << (y % 8));
            } else {
                *oled_pixel |= 1U << (y % 8);
            }
        }
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                                               area->x2 + 1, area->y2 + 1, oled_buffer));
}

static void lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    while (true) {
        _lock_acquire(&lvgl_lock);
        uint32_t delay_ms = lv_timer_handler();
        _lock_release(&lvgl_lock);
        if (delay_ms < 5) {
            delay_ms = 5;
        } else if (delay_ms > 500) {
            delay_ms = 500;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t ssd1306_show_name(void)
{
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = SSD1306_ADDRESS,
        .scl_speed_hz = 400 * 1000,
        .transaction_timeout_ms = 1000,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_get_bus(), &io_config, &io), "OLED", "create I2C panel IO");

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {.height = OLED_HEIGHT};
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config = &ssd1306_config,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io, &panel_config, &panel), "OLED", "create SSD1306 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), "OLED", "reset panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), "OLED", "initialize panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), "OLED", "turn on panel");

    lv_init();
    lv_display_t *display = lv_display_create(OLED_WIDTH, OLED_HEIGHT);
    assert(display);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(display, lvgl_buffer, NULL, sizeof(lvgl_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, oled_flush);

    const esp_lcd_panel_io_callbacks_t callbacks = {.on_color_trans_done = oled_flush_complete};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io, &callbacks, display), "OLED", "register flush callback");

    esp_timer_handle_t tick_timer = NULL;
    const esp_timer_create_args_t timer_config = {
        .callback = lvgl_tick,
        .name = "lvgl_tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_config, &tick_timer), "OLED", "create LVGL timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000), "OLED", "start LVGL timer");

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Paul");
    lv_obj_center(label);

    BaseType_t task_created = xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 2, NULL);
    return task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
