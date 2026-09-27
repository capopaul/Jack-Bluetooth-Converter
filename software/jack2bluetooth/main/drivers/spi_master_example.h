#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_system.h"
#include "driver/spi_master.h"

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void lcd_cmd(spi_device_handle_t spi, const uint8_t cmd, bool keep_cs_active);
void lcd_data(spi_device_handle_t spi, const uint8_t *data, int len);
void lcd_spi_pre_transfer_callback(spi_transaction_t *t);
// void lcd_init(spi_device_handle_t spi);
void spi_init(void);