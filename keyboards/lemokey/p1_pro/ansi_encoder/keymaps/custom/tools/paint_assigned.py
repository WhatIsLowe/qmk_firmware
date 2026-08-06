"""Красит слой так, чтобы горели только назначенные клавиши, цветом по типу действия.

Раскладку читает из самой клавиатуры (ту, что правится в лаунчере), поэтому
после любой перестановки клавиш достаточно запустить скрипт заново.
"""

import sys
import time

import hid

VID, PID = 0x362D, 0x0303
ROWS, COLS = 6, 15
LEDS = 81
BATCH = 9
NO_LED = 0xFF

KC_NO, KC_TRNS = 0x0000, 0x0001

# Оттенок в HSV: 0 красный, 85 зелёный, 170 синий
NUMPAD = (0, 255, 255)
MEDIA = (85, 255, 255)
RGB_CTRL = (170, 255, 255)
WIRELESS = (128, 255, 255)
OTHER = (0, 0, 255)
OFF = (0, 0, 0)


def group(kc):
    if 0x0054 <= kc <= 0x0063:
        return NUMPAD
    if 0x00A8 <= kc <= 0x00B7:
        return MEDIA
    if 0x7820 <= kc <= 0x7840:
        return RGB_CTRL
    if 0x7E00 <= kc <= 0x7E3F:
        return WIRELESS
    return OTHER


def open_dev():
    for d in hid.enumerate(VID, PID):
        if d["usage_page"] == 0xFF60:
            return hid.Device(path=d["path"])
    sys.exit("клавиатура не найдена")


def send(dev, payload):
    dev.write(b"\x00" + bytes(payload) + bytes(32 - len(payload)))
    time.sleep(0.02)
    return bytes(dev.read(32, 500))


def read_led_map(dev):
    """Какой светодиод какой позиции матрицы соответствует."""
    grid = []
    for row in range(ROWS):
        answer = send(dev, [0xA8, 0x06, row, 0xFF, 0xFF, 0xFF])
        grid.append(list(answer[3 : 3 + COLS]))
    return grid


def read_keymap(dev, layer):
    """Раскладка слоя из EEPROM — та самая, что редактируется в лаунчере."""
    offset = layer * ROWS * COLS * 2
    size = ROWS * COLS * 2
    raw = b""
    while len(raw) < size:
        chunk = min(28, size - len(raw))
        at = offset + len(raw)
        answer = send(dev, [0x12, (at >> 8) & 0xFF, at & 0xFF, chunk])
        raw += answer[4 : 4 + chunk]
    return [(raw[i] << 8) | raw[i + 1] for i in range(0, size, 2)]


def main(layer):
    dev = open_dev()
    led_of = read_led_map(dev)
    keys = read_keymap(dev, layer)

    colors = [OFF] * LEDS
    lit = 0
    for row in range(ROWS):
        for col in range(COLS):
            led = led_of[row][col]
            if led == NO_LED or led >= LEDS:
                continue
            kc = keys[row * COLS + col]
            if kc > KC_TRNS:
                colors[led] = group(kc)
                lit += 1

    for start in range(0, LEDS, BATCH):
        count = min(BATCH, LEDS - start)
        flat = []
        for i in range(count):
            flat += list(colors[start + i])
        answer = send(dev, [0xA8, 0x43, layer, start, count] + flat)
        if answer[2] != 0:
            sys.exit(f"отказ на светодиоде {start}: {answer[:6].hex(' ')}")

    send(dev, [0xA8, 0x41, 1 << layer])
    send(dev, [0xA8, 0x02])
    dev.close()

    print(f"слой {layer}: горит {lit} клавиш, остальные погашены")


main(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
