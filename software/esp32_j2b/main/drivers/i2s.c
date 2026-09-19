// Author : Paul Capgras
// Date   : Sept 19, 2026

#include "i2s.h"
#include "audio_codec.h"
#include "driver/i2s_std.h"

#define BUFF_SIZE 512

i2s_chan_handle_t rx_chan = NULL;

i2s_std_clk_config_t clk_cfg = {
    .sample_rate_hz = 44100,
    .clk_src = I2S_CLK_SRC_DEFAULT,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    .bclk_div = 8};

// So MCLK = 11.2896 MHz

i2s_std_slot_config_t slot_cfg = {
    // Original ESP32 requires matching data and slot widths.
    .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
    .slot_mode = I2S_SLOT_MODE_STEREO,
    .slot_mask = I2S_STD_SLOT_BOTH,
    .ws_width = 32,
    .ws_pol = false,
    .bit_shift = true,
    .msb_right = false};

// So BCLK = 2.822 MHz

i2s_std_gpio_config_t gpio_cfg = {
    .mclk = I2S_GPIO_MCLK,
    .bclk = I2S_GPIO_BCLK,
    .ws = I2S_GPIO_WCLK,
    .dout = I2S_GPIO_DOUT,
    .din = I2S_GPIO_DIN,
    .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
    }};

static void task__i2s_read(void *arg)
{
    uint32_t samples[BUFF_SIZE];
    size_t bytes_read = 0;

    while (1)
    {
        esp_err_t err = i2s_channel_read(
            rx_chan,
            samples,
            sizeof(samples),
            &bytes_read,
            1000 // Timeout in milliseconds
        );

        if (err == ESP_OK)
        {
            size_t sample_count = bytes_read / sizeof(samples[0]);
            // Process sample_count received 32-bit words.
        }
    }

    vTaskDelete(NULL);
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

void esp_i2s_driver_install(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg};

    /* enable I2S */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
    xTaskCreate(task__i2s_read, "i2s_example_read_task", 4096, NULL, 5, NULL);
    // audio_codec_check_power_ready();
}

void esp_i2s_driver_uninstall(void)
{
    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
    ESP_ERROR_CHECK(i2s_del_channel(rx_chan));
    rx_chan = NULL;
}
