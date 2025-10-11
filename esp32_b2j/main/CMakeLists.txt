# Author : Paul Capgras
# Date   : Oct 6, 2025

# This line tells the esp-idf top level file.
idf_component_register(SRCS "main.c" 
    "./drivers/cmd_i2ctools.c" 
    "./drivers/audio_codec.c" 
    "./drivers/bt_app_a2dp.c"
    "./drivers/bt_app_core.c"
    INCLUDE_DIRS "." "./drivers/")
