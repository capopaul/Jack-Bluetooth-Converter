// Author : Paul Capgras
// Date   : Oct 10, 2025

#include <stdio.h>
#include <string.h>
#include "argtable3/argtable3.h"
#include "driver/i2c_master.h"
#include "esp_console.h"
#include "esp_log.h"
#include "cmd_i2ctools.h"

static const char *TAG = "cmd_i2ctools";

#define I2C_TOOL_TIMEOUT_VALUE_MS (50)
static uint32_t i2c_frequency = 100 * 1000;
i2c_master_bus_handle_t tool_bus_handle;

static esp_err_t i2c_get_port(int port, i2c_port_t *i2c_port)
{
    if (port >= I2C_NUM_MAX)
    {
        ESP_LOGE(TAG, "Wrong port number: %d", port);
        return ESP_FAIL;
    }
    *i2c_port = port;
    return ESP_OK;
}

int i2c_config(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&i2c_config_args);
    i2c_port_t i2c_port = I2C_NUM_0;
    int i2c_gpio_sda = 0;
    int i2c_gpio_scl = 0;
    if (nerrors != 0)
    {
        arg_print_errors(stderr, i2c_config_args.end, argv[0]);
        return 0;
    }

    /* Check "--port" option */
    if (i2c_config_args.port->count)
    {
        if (i2c_get_port(i2c_config_args.port->ival[0], &i2c_port) != ESP_OK)
        {
            return 1;
        }
    }
    /* Check "--freq" option */
    if (i2c_config_args.freq->count)
    {
        i2c_frequency = i2c_config_args.freq->ival[0];
    }
    /* Check "--sda" option */
    i2c_gpio_sda = i2c_config_args.sda->ival[0];
    /* Check "--scl" option */
    i2c_gpio_scl = i2c_config_args.scl->ival[0];

    // re-init the bus
    if (i2c_del_master_bus(tool_bus_handle) != ESP_OK)
    {
        return 1;
    }

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = i2c_port,
        .scl_io_num = i2c_gpio_scl,
        .sda_io_num = i2c_gpio_sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    if (i2c_new_master_bus(&i2c_bus_config, &tool_bus_handle) != ESP_OK)
    {
        return 1;
    }

    return 0;
}

int i2c_detect()
{
    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16)
    {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++)
        {
            fflush(stdout);
            address = i + j;
            esp_err_t ret = i2c_master_probe(tool_bus_handle, address, I2C_TOOL_TIMEOUT_VALUE_MS);
            if (ret == ESP_OK)
            {
                printf("%02x ", address);
            }
            else if (ret == ESP_ERR_TIMEOUT)
            {
                printf("UU ");
            }
            else
            {
                printf("-- ");
            }
        }
        printf("\r\n");
    }

    return 0;
}

uint8_t i2c_get(int chip_address, int register_address)
{
    uint8_t data = 0; // store the single byte

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = i2c_frequency,
        .device_address = chip_address,
    };
    i2c_master_dev_handle_t dev_handle;

    if (i2c_master_bus_add_device(tool_bus_handle, &i2c_dev_conf, &dev_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device");
        return -1; // indicate error
    }

    esp_err_t ret = i2c_master_transmit_receive(
        dev_handle,
        (uint8_t *)&register_address, 1,
        &data, 1,
        I2C_TOOL_TIMEOUT_VALUE_MS);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "I2C read failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev_handle);
        return -1;
    }

    printf("I2C_GET - Reg %d -> 0x%02x\n", register_address, data);

    if (i2c_master_bus_rm_device(dev_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to remove I2C device");
        return -1;
    }

    return data; // return the byte read
}

int i2c_set(int chip_address, int register_address, uint8_t data)
{
    int data_len = 1;
    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = i2c_frequency,
        .device_address = chip_address,
    };
    i2c_master_dev_handle_t dev_handle;
    if (i2c_master_bus_add_device(tool_bus_handle, &i2c_dev_conf, &dev_handle) != ESP_OK)
    {
        return 1;
    }

    uint8_t *i2c_data = malloc(data_len + 1);
    i2c_data[0] = register_address;
    i2c_data[1] = data;

    esp_err_t ret = i2c_master_transmit(dev_handle, i2c_data, data_len + 1, I2C_TOOL_TIMEOUT_VALUE_MS);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Write OK");
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
    }
    else
    {
        ESP_LOGW(TAG, "Write Failed");
    }

    free(i2c_data);
    if (i2c_master_bus_rm_device(dev_handle) != ESP_OK)
    {
        return 1;
    }
    return 0;
}

int i2c_dump(int chip_addr, int size)
{

    // check read size is correct
    if (size != 1 && size != 2 && size != 4)
    {
        ESP_LOGE(TAG, "Wrong read size. Only support 1,2,4");
        return 1;
    }

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = i2c_frequency,
        .device_address = chip_addr,
    };
    i2c_master_dev_handle_t dev_handle;
    if (i2c_master_bus_add_device(tool_bus_handle, &i2c_dev_conf, &dev_handle) != ESP_OK)
    {
        return 1;
    }

    uint8_t data_addr;
    uint8_t data[4];
    int32_t block[16];
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f"
           "    0123456789abcdef\r\n");
    for (int i = 0; i < 128; i += 16)
    {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j += size)
        {
            fflush(stdout);
            data_addr = i + j;
            esp_err_t ret = i2c_master_transmit_receive(dev_handle, &data_addr, 1, data, size, I2C_TOOL_TIMEOUT_VALUE_MS);
            if (ret == ESP_OK)
            {
                for (int k = 0; k < size; k++)
                {
                    printf("%02x ", data[k]);
                    block[j + k] = data[k];
                }
            }
            else
            {
                for (int k = 0; k < size; k++)
                {
                    printf("XX ");
                    block[j + k] = -1;
                }
            }
        }
        printf("   ");
        for (int k = 0; k < 16; k++)
        {
            if (block[k] < 0)
            {
                printf("X");
            }
            if ((block[k] & 0xff) == 0x00 || (block[k] & 0xff) == 0xff)
            {
                printf(".");
            }
            else if ((block[k] & 0xff) < 32 || (block[k] & 0xff) >= 127)
            {
                printf("?");
            }
            else
            {
                printf("%c", (char)(block[k] & 0xff));
            }
        }
        printf("\r\n");
    }
    if (i2c_master_bus_rm_device(dev_handle) != ESP_OK)
    {
        return 1;
    }
    return 0;
}
