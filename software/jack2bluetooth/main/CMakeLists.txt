# Author : Paul Capgras
# Date   : Sep 20, 2026

idf_component_register(SRCS "main.c"
        "./drivers/audio_codec.c"
        "./drivers/bt_app_a2dp_source.c"
        "./drivers/bt_app_core.c"
        "./drivers/decode_task.c"
        "./drivers/i2c.c"
        "./drivers/i2s.c"
        "./drivers/io_expander.c"
        PRIV_REQUIRES bt nvs_flash esp_driver_i2c esp_driver_gpio esp_driver_i2s console
        INCLUDE_DIRS "." "./drivers/")
