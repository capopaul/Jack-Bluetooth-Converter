#include "utils.h"

#include "esp_log.h"

void is_expected(const char *tag, int register_address, uint8_t read_value, uint8_t expected_value)
{
    if (read_value != expected_value)
    {
        ESP_LOGW(tag, "Reg [%d] Read : 0x%02x vs Expected 0x%02x", register_address, read_value, expected_value);
    }
}
