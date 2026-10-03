/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once
#include <stdint.h>
#include "esp_err.h"

// Decode the 320x240 JPEG at quarter width/height to conserve internal RAM.
#define IMAGE_W 80
#define IMAGE_H 60

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode the jpeg ``image.jpg`` embedded into the program file into pixel data.
 *
 * @param pixels Receives a flat RGB565 buffer; access pixels[y * IMAGE_W + x].
 *               The caller owns the buffer. Set to NULL on failure.
 * @return - ESP_ERR_NOT_SUPPORTED if image is malformed or a progressive jpeg file
 *         - ESP_ERR_NO_MEM if out of memory
 *         - ESP_OK on succesful decode
 */
esp_err_t decode_image(uint16_t **pixels);

#ifdef __cplusplus
}
#endif
