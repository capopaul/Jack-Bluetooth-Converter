# Author : Paul Capgras
# Date   : Sep 20, 2026

idf_component_register(SRCS "main.c"
        "./drivers/audio_codec.c"
        "./drivers/bt_app_a2dp_source.c"
        "./drivers/bt_app_core.c"
        "./drivers/cmd_i2ctools.c"
        "./drivers/decode_task.c"
        "./drivers/i2s.c"
        "./drivers/io_expander.c"
        "./drivers/utils.c"
        PRIV_REQUIRES bt nvs_flash esp_driver_i2c esp_driver_gpio esp_driver_i2s console
        INCLUDE_DIRS "." "./drivers/")
