/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

void register_i2ctools(void);
int do_i2cdump_cmd(int chip_addr, int size);

extern i2c_master_bus_handle_t tool_bus_handle;
