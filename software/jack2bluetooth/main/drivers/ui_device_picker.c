#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "bt_app_gap.h"
#include "ssd1306.h"
#include "ui_device_picker.h"

#define UI_DEVICE_PICKER_TAG "DEVICE_PICKER"

static SemaphoreHandle_t picker_mutex;
static bt_app_gap_discovered_device_t picker_devices[BT_APP_GAP_MAX_DISCOVERED_DEVICES];
static const char *picker_names[BT_APP_GAP_MAX_DISCOVERED_DEVICES];
static size_t picker_device_count;
static size_t picker_selected_index;
static bool picker_active;

static const char *device_name_for_address_locked(const uint8_t address[6])
{
    for (size_t i = 0; i < picker_device_count; ++i)
    {
        if (memcmp(picker_devices[i].address, address, sizeof(picker_devices[i].address)) == 0)
        {
            return picker_devices[i].name[0] != '\0' ? picker_devices[i].name : "<unknown>";
        }
    }
    return "<unknown>";
}

static void show_connection_state(const char *state, const uint8_t address[6])
{
    if (picker_mutex == NULL || address == NULL)
    {
        return;
    }

    xSemaphoreTake(picker_mutex, portMAX_DELAY);
    if (picker_active)
    {
        const char *name = device_name_for_address_locked(address);
        esp_err_t err = ssd1306_set_text_lines("Jack -> Bluetooth", state, name);
        if (err != ESP_OK)
        {
            ESP_LOGW(UI_DEVICE_PICKER_TAG, "Could not show connection state: %s", esp_err_to_name(err));
        }
    }
    xSemaphoreGive(picker_mutex);
}

static void render_picker_locked(void)
{
    esp_err_t err = ssd1306_show_string_list(picker_names, picker_device_count);
    if (err != ESP_OK)
    {
        ESP_LOGW(UI_DEVICE_PICKER_TAG, "Could not render device list: %s", esp_err_to_name(err));
        return;
    }

    // The SSD1306 list uses its visible first item as its list index. Move it
    // to the picker selection so line 2 is always the selected device.
    for (size_t i = 0; i < picker_selected_index; ++i)
    {
        (void)ssd1306_list_next();
    }
}

static void log_selected_device_locked(void)
{
    if (picker_device_count == 0)
    {
        return;
    }

    const bt_app_gap_discovered_device_t *device = &picker_devices[picker_selected_index];
    ESP_LOGI(UI_DEVICE_PICKER_TAG, "Selected (%u/%u): %s",
             (unsigned)(picker_selected_index + 1), (unsigned)picker_device_count,
             device->name[0] != '\0' ? device->name : "<unknown>");
}

static void refresh_picker(void)
{
    bt_app_gap_discovered_device_t updated_devices[BT_APP_GAP_MAX_DISCOVERED_DEVICES] = {0};
    size_t updated_count = bt_app_gap_get_discovered_device_count();
    if (updated_count > BT_APP_GAP_MAX_DISCOVERED_DEVICES)
    {
        updated_count = BT_APP_GAP_MAX_DISCOVERED_DEVICES;
    }

    for (size_t i = 0; i < updated_count; ++i)
    {
        if (bt_app_gap_get_discovered_device(i, &updated_devices[i]) != ESP_OK)
        {
            updated_count = i;
            break;
        }
    }

    xSemaphoreTake(picker_mutex, portMAX_DELAY);
    uint8_t selected_address[sizeof(picker_devices[0].address)] = {0};
    bool had_selection = picker_device_count > 0;
    if (had_selection)
    {
        memcpy(selected_address, picker_devices[picker_selected_index].address,
               sizeof(selected_address));
    }

    memcpy(picker_devices, updated_devices, sizeof(picker_devices));
    picker_device_count = updated_count;
    for (size_t i = 0; i < picker_device_count; ++i)
    {
        picker_names[i] = picker_devices[i].name[0] != '\0' ? picker_devices[i].name : "<unknown>";
    }

    if (picker_device_count == 0)
    {
        picker_selected_index = 0;
    }
    else if (had_selection)
    {
        bool selection_preserved = false;
        for (size_t i = 0; i < picker_device_count; ++i)
        {
            if (memcmp(picker_devices[i].address, selected_address, sizeof(selected_address)) == 0)
            {
                picker_selected_index = i;
                selection_preserved = true;
                break;
            }
        }
        if (!selection_preserved && picker_selected_index >= picker_device_count)
        {
            picker_selected_index = picker_device_count - 1;
        }
    }

    if (picker_active)
    {
        render_picker_locked();
    }
    xSemaphoreGive(picker_mutex);
}

esp_err_t ui_device_picker_start(void)
{
    if (picker_mutex == NULL)
    {
        picker_mutex = xSemaphoreCreateMutex();
        if (picker_mutex == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(picker_mutex, portMAX_DELAY);
    picker_active = true;
    xSemaphoreGive(picker_mutex);

    bt_app_gap_set_device_list_changed_callback(refresh_picker);
    refresh_picker();
    ESP_LOGI(UI_DEVICE_PICKER_TAG, "Device picker started");
    return ESP_OK;
}

esp_err_t ui_device_picker_next(void)
{
    if (picker_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(picker_mutex, portMAX_DELAY);
    if (!picker_active || picker_device_count == 0)
    {
        xSemaphoreGive(picker_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (picker_selected_index + 1 < picker_device_count)
    {
        ++picker_selected_index;
        render_picker_locked();
        log_selected_device_locked();
    }
    xSemaphoreGive(picker_mutex);
    return ESP_OK;
}

esp_err_t ui_device_picker_back(void)
{
    if (picker_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(picker_mutex, portMAX_DELAY);
    if (!picker_active || picker_device_count == 0)
    {
        xSemaphoreGive(picker_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (picker_selected_index > 0)
    {
        --picker_selected_index;
        render_picker_locked();
        log_selected_device_locked();
    }
    xSemaphoreGive(picker_mutex);
    return ESP_OK;
}

esp_err_t ui_device_picker_enter(uint8_t address[6])
{
    if (address == NULL || picker_mutex == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(picker_mutex, portMAX_DELAY);
    if (!picker_active || picker_device_count == 0)
    {
        xSemaphoreGive(picker_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(address, picker_devices[picker_selected_index].address, 6);
    log_selected_device_locked();
    xSemaphoreGive(picker_mutex);
    return ESP_OK;
}

void ui_device_picker_show_connecting(const uint8_t address[6])
{
    show_connection_state("Connecting to...", address);
}

void ui_device_picker_show_connected(const uint8_t address[6])
{
    show_connection_state("Connected to", address);
}

void ui_device_picker_show_connection_failed(const uint8_t address[6])
{
    show_connection_state("Connection failed", address);
}

void ui_device_picker_show_disconnected(const uint8_t address[6])
{
    show_connection_state("Disconnected from", address);
}
