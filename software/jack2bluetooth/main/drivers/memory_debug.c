#include "esp_heap_caps.h"
#include "memory_debug.h"
#include "esp_log.h"

#define MEMORY_DEBUG_TAG "MEMORY_DEBUG"

void log_internal_heap(const char *where)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

    ESP_LOGI(MEMORY_DEBUG_TAG "MEMORY_DEBUG",
             "%s: free=%u, low-water=%u, largest-block=%u",
             where,
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_minimum_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));

    heap_caps_print_heap_info(caps);
}