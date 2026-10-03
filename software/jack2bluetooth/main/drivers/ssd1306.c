#include <stdint.h>
#include <string.h>

#include "ssd1306.h"
#include "i2c.h"

#define SSD1306_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)

static uint8_t framebuffer[OLED_WIDTH * OLED_PAGES];

static esp_err_t send_commands(const uint8_t *commands, size_t length)
{
    // The full SSD1306 initialization sequence is 25 bytes long.
    uint8_t message[32] = {0x00}; // 0x00 means following bytes are commands.
    if (length > sizeof(message) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&message[1], commands, length);
    return i2c_write_raw(SSD1306_ADDRESS, message, length + 1);
}

static void set_pixel(int x, int y)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    framebuffer[x + (y / 8) * OLED_WIDTH] |= 1U << (y % 8);
}

static void draw_glyph(int x, int y, const uint8_t glyph[5], int scale)
{
    for (int column = 0; column < 5; ++column) {
        for (int row = 0; row < 7; ++row) {
            if ((glyph[column] & (1U << row)) == 0) {
                continue;
            }
            for (int dx = 0; dx < scale; ++dx) {
                for (int dy = 0; dy < scale; ++dy) {
                    set_pixel(x + column * scale + dx, y + row * scale + dy);
                }
            }
        }
    }
}

static esp_err_t flush(void)
{
    const uint8_t address_window[] = {
        0x21, 0x00, OLED_WIDTH - 1, // column range
        0x22, 0x00, OLED_PAGES - 1, // page range
    };
    esp_err_t ret = send_commands(address_window, sizeof(address_window));
    if (ret != ESP_OK) {
        return ret;
    }

    // Send one page at a time. The leading 0x40 denotes display data.
    uint8_t packet[OLED_WIDTH + 1];
    packet[0] = 0x40;
    for (int page = 0; page < OLED_PAGES; ++page) {
        memcpy(&packet[1], &framebuffer[page * OLED_WIDTH], OLED_WIDTH);
        ret = i2c_write_raw(SSD1306_ADDRESS, packet, sizeof(packet));
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t ssd1306_show_name(void)
{
    const uint8_t init_commands[] = {
        0xAE,       // display off
        0x20, 0x00, // horizontal addressing mode
        0x40,       // display start line
        0x81, 0x7F, // contrast
        0xA1,       // segment remap
        0xA6,       // normal (non-inverted) pixels
        0xA8, 0x3F, // multiplex ratio: 64 rows
        0xC8,       // scan from COM63 to COM0
        0xD3, 0x00, // no display offset
        0xDA, 0x12, // COM pin configuration for 128x64 panels
        0xD5, 0x80, // display clock
        0xD9, 0xF1, // precharge period
        0xDB, 0x40, // VCOMH deselect level
        0x8D, 0x14, // enable charge pump
        0xAF,       // display on
    };
    esp_err_t ret = send_commands(init_commands, sizeof(init_commands));
    if (ret != ESP_OK) {
        return ret;
    }

    memset(framebuffer, 0, sizeof(framebuffer));
    static const uint8_t letter_p[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    static const uint8_t letter_a[5] = {0x20, 0x54, 0x54, 0x54, 0x78};
    static const uint8_t letter_u[5] = {0x3C, 0x40, 0x40, 0x20, 0x7C};
    static const uint8_t letter_l[5] = {0x00, 0x41, 0x7F, 0x40, 0x00};
    const int scale = 4;
    const int y = 18;
    draw_glyph(14, y, letter_p, scale);
    draw_glyph(38, y, letter_a, scale);
    draw_glyph(62, y, letter_u, scale);
    draw_glyph(86, y, letter_l, scale);
    return flush();
}
