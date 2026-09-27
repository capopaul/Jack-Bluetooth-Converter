
#include "button.h"

#include <esp_system.h>

// For print
#include "esp_log.h"

#define BUTTON_TAG "BUTTON"

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void on_enter_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button ENTER pushed");
}

void on_back_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button BACK pushed");
}

void on_next_pressed(void)
{
    ESP_LOGI(BUTTON_TAG, "Button NEXT pushed");
}

void on_direction_changed(direction_state new_state)
{

    // Restart is the easiest to clean everything and make sure there is no memory leak.
    // It is much easier than to deallocate all the ressources
    esp_restart();
}
