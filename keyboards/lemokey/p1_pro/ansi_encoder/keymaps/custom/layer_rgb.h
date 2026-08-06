/* Подсветка, своя для каждого слоя.
 *
 * Цвета лежат в EEPROM, не в прошивке: слой 0 красит штатный лаунчер Keychron
 * своей командой 0xA8, слои 1-3 — наша страница-красилка теми же командами
 * с добавленным номером слоя.
 *
 * Слой 0 хранится полными HSV — лаунчер шлёт туда произвольные цвета и урезать
 * его нельзя. Слои 1-3 хранятся номерами из общей палитры: на слое редко бывает
 * больше пяти-шести разных цветов, а номер влезает в полбайта. Это экономит
 * почти шестьсот байт EEPROM, которые достаются макросам.
 */

#pragma once

#include "quantum.h"

#define LRGB_LEDS       RGB_MATRIX_LED_COUNT
#define LRGB_LAYERS     4
#define LRGB_PALETTE    16                      // столько разных цветов помещается в номер из четырёх бит
#define LRGB_PACKED    ((LRGB_LEDS + 1) / 2)    // два светодиода на байт

typedef struct PACKED {
    uint8_t custom;                             // бит N поднят — у слоя N своя раскраска
    uint8_t used;                               // сколько цветов палитры занято
    HSV     palette[LRGB_PALETTE];
    HSV     base[LRGB_LEDS];                    // слой 0, как прислал лаунчер
    uint8_t packed[LRGB_LAYERS - 1][LRGB_PACKED];
} layer_rgb_t;

extern layer_rgb_t layer_rgb;
extern uint8_t     per_key_rgb_type;

HSV  layer_rgb_get(uint8_t layer, uint8_t led);
void layer_rgb_hid_rx(uint8_t *data, uint8_t length);
