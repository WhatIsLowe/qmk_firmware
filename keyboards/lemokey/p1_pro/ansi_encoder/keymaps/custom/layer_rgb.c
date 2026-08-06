#include "layer_rgb.h"
#include "eeconfig.h"
#include <lib/lib8tion/lib8tion.h>

_Static_assert(sizeof(layer_rgb_t) == EECONFIG_USER_DATA_SIZE, "структура не совпала с блоком EEPROM");

layer_rgb_t layer_rgb;
uint8_t     per_key_rgb_type;

/* Команды раздела 0xA8. Первые десять — протокол Keychron, лаунчер шлёт именно их
 * и про слои ничего не знает, поэтому работает со слоем 0. Дальше наши, для красилки. */
enum {
    RGB_PROTOCOL_VER = 0x01,
    RGB_SAVE,
    INDICATORS_GET,
    INDICATORS_SET,
    LED_COUNT_GET,
    LED_ROW_GET,
    PER_KEY_TYPE_GET,
    PER_KEY_TYPE_SET,
    PER_KEY_COLOR_GET,
    PER_KEY_COLOR_SET,

    LAYER_MASK_GET = 0x40,
    LAYER_MASK_SET,
    LAYER_COLOR_GET,
    LAYER_COLOR_SET,
};

/* Лаунчер запрашивает цвета пачками по девять светодиодов — больше в 32-байтный пакет не влезает. */
#define COLORS_PER_PACKET 9

/* Caps Lock — единственный индикатор платы, отдаём лаунчеру ту же конфигурацию,
 * что и стоковая прошивка, иначе он считает клавиатуру неполноценной. */
#define INDICATOR_CAPS_LOCK 0x02

static HSV     indicator_hsv = {.h = 0xFF, .s = 0x00, .v = 0xFF};
static uint8_t indicator_disabled;

/* Нулевой цвет всегда «погашено»: гашение клавиши не должно зависеть от того,
 * сколько места осталось в палитре. */
static void palette_reset(void) {
    memset(layer_rgb.palette, 0, sizeof(layer_rgb.palette));
    layer_rgb.used = 1;
}

static bool same_color(HSV a, HSV b) {
    return a.h == b.h && a.s == b.s && a.v == b.v;
}

static uint16_t color_distance(HSV a, HSV b) {
    uint8_t dh = a.h > b.h ? a.h - b.h : b.h - a.h;
    if (dh > 128) dh = 255 - dh;            // оттенок замкнут в круг
    uint8_t ds = a.s > b.s ? a.s - b.s : b.s - a.s;
    uint8_t dv = a.v > b.v ? a.v - b.v : b.v - a.v;
    return (uint16_t)dh + ds + dv;
}

/* Точное совпадение, иначе новый слот, иначе ближайший из уже набранных. */
static uint8_t palette_index(HSV hsv) {
    if (hsv.v == 0) {
        return 0;
    }
    for (uint8_t i = 0; i < layer_rgb.used; i++) {
        if (same_color(layer_rgb.palette[i], hsv)) {
            return i;
        }
    }
    if (layer_rgb.used < LRGB_PALETTE) {
        layer_rgb.palette[layer_rgb.used] = hsv;
        return layer_rgb.used++;
    }

    uint8_t  best     = 1;
    uint16_t best_gap = 0xFFFF;
    for (uint8_t i = 1; i < layer_rgb.used; i++) {
        uint16_t gap = color_distance(layer_rgb.palette[i], hsv);
        if (gap < best_gap) {
            best_gap = gap;
            best     = i;
        }
    }
    return best;
}

static uint8_t nibble_get(uint8_t layer, uint8_t led) {
    uint8_t byte = layer_rgb.packed[layer - 1][led / 2];
    return (led & 1) ? (byte >> 4) : (byte & 0x0F);
}

static void nibble_set(uint8_t layer, uint8_t led, uint8_t value) {
    uint8_t *byte = &layer_rgb.packed[layer - 1][led / 2];
    if (led & 1) {
        *byte = (*byte & 0x0F) | (value << 4);
    } else {
        *byte = (*byte & 0xF0) | (value & 0x0F);
    }
}

/* Перекрашивание оставляет в палитре цвета, которыми уже никто не пользуется.
 * Перед записью в EEPROM пересобираем её из тех, что реально встречаются. */
static void palette_compact(void) {
    HSV     fresh[LRGB_PALETTE] = {0};
    uint8_t remap[LRGB_PALETTE] = {0};
    uint8_t count               = 1;

    for (uint8_t layer = 1; layer < LRGB_LAYERS; layer++) {
        for (uint8_t led = 0; led < LRGB_LEDS; led++) {
            uint8_t old = nibble_get(layer, led);
            if (old == 0 || remap[old]) {
                continue;
            }
            if (count >= LRGB_PALETTE) {
                return;                     // мест ровно столько же, сжимать нечего
            }
            fresh[count] = layer_rgb.palette[old];
            remap[old]   = count++;
        }
    }

    for (uint8_t layer = 1; layer < LRGB_LAYERS; layer++) {
        for (uint8_t led = 0; led < LRGB_LEDS; led++) {
            nibble_set(layer, led, remap[nibble_get(layer, led)]);
        }
    }
    memcpy(layer_rgb.palette, fresh, sizeof(fresh));
    layer_rgb.used = count;
}

HSV layer_rgb_get(uint8_t layer, uint8_t led) {
    if (led >= LRGB_LEDS) {
        return (HSV){0, 0, 0};
    }
    return layer == 0 ? layer_rgb.base[led] : layer_rgb.palette[nibble_get(layer, led)];
}

static void layer_rgb_set(uint8_t layer, uint8_t led, HSV hsv) {
    if (led >= LRGB_LEDS) {
        return;
    }
    if (layer == 0) {
        layer_rgb.base[led] = hsv;
    } else {
        nibble_set(layer, led, palette_index(hsv));
    }
}

/* Штатная реализация заводит временный буфер размером со весь блок прямо на стеке,
 * а это сотни байт — стек главного потока столько не держит и прошивка падает
 * на старте, не успев поднять третий USB-интерфейс (мышь, мультимедиа, системные
 * клавиши). Пишем из готовой структуры, стек не трогаем. */
void eeconfig_init_user_datablock(void) {
    memset(&layer_rgb, 0, sizeof(layer_rgb));
    palette_reset();
    for (uint8_t i = 0; i < LRGB_LEDS; i++) {
        layer_rgb.base[i] = (HSV){.h = 0, .s = 0, .v = 255};
    }
    eeconfig_update_user_datablock(&layer_rgb);
}

void keyboard_post_init_user(void) {
    eeconfig_read_user_datablock(&layer_rgb);
    if (layer_rgb.used == 0 || layer_rgb.used > LRGB_PALETTE) {
        palette_reset();
    }
}

/* Слой 0 отрисовывает эффект из rgb_matrix_user.inc — его включает лаунчер.
 * Здесь только слои выше: если у слоя своя раскраска, она ложится поверх любого эффекта. */
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    uint8_t layer = get_highest_layer(layer_state);

    if (layer == 0 || layer >= LRGB_LAYERS) {
        return false;
    }
    if (!(layer_rgb.custom & (1 << layer))) {
        return false;
    }

    for (uint8_t i = led_min; i < led_max; i++) {
        HSV hsv = layer_rgb_get(layer, i);
        hsv.v   = scale8(hsv.v, rgb_matrix_config.hsv.v);
        RGB rgb = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return false;
}

static bool colors_get(uint8_t layer, uint8_t *out, uint8_t start, uint8_t count) {
    if (layer >= LRGB_LAYERS || count > COLORS_PER_PACKET || start + count > LRGB_LEDS) {
        return false;
    }
    for (uint8_t i = 0; i < count; i++) {
        HSV hsv        = layer_rgb_get(layer, start + i);
        out[i * 3]     = hsv.h;
        out[i * 3 + 1] = hsv.s;
        out[i * 3 + 2] = hsv.v;
    }
    return true;
}

static bool colors_set(uint8_t layer, const uint8_t *in, uint8_t start, uint8_t count) {
    if (layer >= LRGB_LAYERS || count > COLORS_PER_PACKET || start + count > LRGB_LEDS) {
        return false;
    }
    for (uint8_t i = 0; i < count; i++) {
        HSV hsv = {.h = in[i * 3], .s = in[i * 3 + 1], .v = in[i * 3 + 2]};
        layer_rgb_set(layer, start + i, hsv);
    }
    return true;
}

void layer_rgb_hid_rx(uint8_t *data, uint8_t length) {
    bool    ok  = true;
    uint8_t cmd = data[1];

    switch (cmd) {
        case RGB_PROTOCOL_VER:
            data[3] = 1;
            break;

        case RGB_SAVE:
            palette_compact();
            eeconfig_update_user_datablock(&layer_rgb);
            break;

        case INDICATORS_GET:
            data[3] = INDICATOR_CAPS_LOCK;
            data[4] = indicator_disabled;
            data[5] = indicator_hsv.h;
            data[6] = indicator_hsv.s;
            data[7] = indicator_hsv.v;
            break;

        case INDICATORS_SET:
            indicator_disabled = data[3];
            indicator_hsv.h    = data[4];
            indicator_hsv.s    = data[5];
            indicator_hsv.v    = data[6];
            break;

        case LED_COUNT_GET:
            data[3] = LRGB_LEDS;
            break;

        /* Лаунчер строит схему клавиатуры по строкам матрицы: спрашивает строку,
         * получает пятнадцать номеров светодиодов, NO_LED там, где клавиши нет. */
        case LED_ROW_GET: {
            uint8_t row = data[2];
            if (row >= MATRIX_ROWS) {
                ok = false;
                break;
            }
            for (uint8_t col = 0; col < MATRIX_COLS; col++) {
                data[3 + col] = g_led_config.matrix_co[row][col];
            }
        } break;

        case PER_KEY_TYPE_GET:
            data[3] = per_key_rgb_type;
            break;

        case PER_KEY_TYPE_SET:
            per_key_rgb_type = data[2];
            break;

        case PER_KEY_COLOR_GET: {
            uint8_t start = data[2];
            uint8_t count = data[3];
            ok            = colors_get(0, &data[3], start, count);
        } break;

        case PER_KEY_COLOR_SET:
            ok = colors_set(0, &data[4], data[2], data[3]);
            break;

        case LAYER_MASK_GET:
            data[3] = layer_rgb.custom;
            break;

        case LAYER_MASK_SET:
            layer_rgb.custom = data[2];
            break;

        case LAYER_COLOR_GET: {
            uint8_t layer = data[2];
            uint8_t start = data[3];
            uint8_t count = data[4];
            ok            = colors_get(layer, &data[3], start, count);
        } break;

        case LAYER_COLOR_SET:
            ok = colors_set(data[2], &data[5], data[3], data[4]);
            break;

        default:
            data[0] = 0xFF;
            ok      = false;
            break;
    }

    data[2] = ok ? 0 : 1;
}
