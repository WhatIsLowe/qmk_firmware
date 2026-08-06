/* Эффект «свой цвет каждой клавише» для базового слоя.
 * Идёт следом за двадцатью тремя штатными, то есть получает номер 23 — тот самый,
 * который лаунчер Keychron включает командой «поставить эффект 23». */

RGB_MATRIX_EFFECT(PER_KEY_RGB)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#    include "layer_rgb.h"

static bool PER_KEY_RGB(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        HSV hsv = layer_rgb_get(0, i);
        hsv.v   = rgb_matrix_config.hsv.v;
        RGB rgb = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }

    return rgb_matrix_check_finished_leds(led_max);
}

#endif
