# Author : Paul Capgras
# Date   : Sep 19, 2026

idf_component_register(SRCS "main.c"
        "./drivers/bt_app_core.c"
        "./drivers/bt_app_a2dp_source.c"
        "./drivers/decode_task.c"
        PRIV_REQUIRES bt nvs_flash
        INCLUDE_DIRS "." "./drivers/")
