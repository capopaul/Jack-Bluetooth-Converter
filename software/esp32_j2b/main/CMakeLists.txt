# Author : Paul Capgras
# Date   : Sep 19, 2026

idf_component_register(SRCS "main.c"
        "./drivers/bt_app_core.c"
        "./drivers/decode_task.c"
        PRIV_REQUIRES bt nvs_flash
        INCLUDE_DIRS "." "./drivers/")
