VIA_ENABLE = yes
RGB_MATRIX_CUSTOM_USER = yes
CAPS_WORD_ENABLE = yes

SRC += layer_rgb.c

# Открывает в lemokey_raw_hid.c приём команд 0xA8 — раздела подсветки Keychron.
OPT_DEFS += -DLAYER_RGB_ENABLE
