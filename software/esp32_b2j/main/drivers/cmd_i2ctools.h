// Author : Paul Capgras
// Date   : Oct 10, 2025

static struct
{
    struct arg_int *port;
    struct arg_int *freq;
    struct arg_int *sda;
    struct arg_int *scl;
    struct arg_end *end;
} i2c_config_args;

int i2c_detect();
uint8_t i2c_get(int chip_address, int register_address);
int i2c_set(int chip_address, int register_address, uint8_t data);
int i2c_dump(int chip_addr, int size);

extern i2c_master_bus_handle_t tool_bus_handle;
