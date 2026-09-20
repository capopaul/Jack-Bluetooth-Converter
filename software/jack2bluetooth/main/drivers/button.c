
#include "button.h"

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

void on_direction_changed(int new_state)
{
    ESP_LOGI(BUTTON_TAG,
             "Button DIRECTION: %d -> %d",
             new_state ? 0 : 1, new_state);
}
