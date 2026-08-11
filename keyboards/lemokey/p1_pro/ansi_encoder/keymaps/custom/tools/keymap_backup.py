"""Сохраняет и восстанавливает раскладку клавиатуры.

Прошивка стирает EEPROM, а вместе с ней раскладку, энкодер и подсветку.
Скрипт снимает копию до прошивки и заливает обратно после.

    ./keymap_backup.py save   [файл]
    ./keymap_backup.py load   [файл]
    ./keymap_backup.py show   [файл]
"""

import json
import sys
import time
from pathlib import Path

import hid

VID, PID = 0x362D, 0x0303
ROWS, COLS, LAYERS = 6, 15, 4
ENCODERS = 1
LEDS = 81

GET_BUFFER, SET_BUFFER = 0x12, 0x13
GET_ENCODER, SET_ENCODER = 0x14, 0x15
CHUNK = 28

DEFAULT_FILE = Path(__file__).with_name('keymap_backup.json')


def open_dev():
    for d in hid.enumerate(VID, PID):
        if d['usage_page'] == 0xFF60:
            return hid.Device(path=d['path'])
    sys.exit('клавиатура не найдена')


def ask(dev, payload):
    dev.write(b'\x00' + bytes(payload) + bytes(32 - len(payload)))
    time.sleep(0.02)
    return bytes(dev.read(32, 700))


def read_keymap(dev):
    size = LAYERS * ROWS * COLS * 2
    raw = b''
    while len(raw) < size:
        n = min(CHUNK, size - len(raw))
        at = len(raw)
        raw += ask(dev, [GET_BUFFER, (at >> 8) & 0xFF, at & 0xFF, n])[4:4 + n]
    return list(raw)


def write_keymap(dev, raw):
    for at in range(0, len(raw), CHUNK):
        part = raw[at:at + CHUNK]
        answer = ask(dev, [SET_BUFFER, (at >> 8) & 0xFF, at & 0xFF, len(part)] + list(part))
        if answer[0] != SET_BUFFER:
            sys.exit(f'клавиатура отклонила запись со смещения {at}')


def read_encoders(dev):
    out = []
    for layer in range(LAYERS):
        for enc in range(ENCODERS):
            for direction in (0, 1):
                a = ask(dev, [GET_ENCODER, layer, enc, direction])
                out.append([a[4], a[5]])
    return out


def write_encoders(dev, pairs):
    i = 0
    for layer in range(LAYERS):
        for enc in range(ENCODERS):
            for direction in (0, 1):
                hi, lo = pairs[i]
                ask(dev, [SET_ENCODER, layer, enc, direction, hi, lo])
                i += 1


def read_colors(dev):
    out = {'mask': ask(dev, [0xA8, 0x40])[3], 'layers': {}}
    for layer in range(LAYERS):
        colors = []
        for start in range(0, LEDS, 9):
            count = min(9, LEDS - start)
            head = [0xA8, 0x09, start, count] if layer == 0 else [0xA8, 0x42, layer, start, count]
            a = ask(dev, head)
            for i in range(count):
                colors.append([a[3 + i * 3], a[4 + i * 3], a[5 + i * 3]])
        out['layers'][str(layer)] = colors
    return out


def write_colors(dev, data):
    for layer in range(LAYERS):
        colors = data['layers'].get(str(layer))
        if not colors:
            continue
        for start in range(0, LEDS, 9):
            count = min(9, LEDS - start)
            flat = []
            for i in range(count):
                flat += colors[start + i]
            head = [0xA8, 0x0A, start, count] if layer == 0 else [0xA8, 0x43, layer, start, count]
            ask(dev, head + flat)
    ask(dev, [0xA8, 0x41, data['mask']])
    ask(dev, [0xA8, 0x02])


def describe(raw):
    lit = 0
    for layer in range(LAYERS):
        base = layer * ROWS * COLS * 2
        keys = [(raw[base + i * 2] << 8) | raw[base + i * 2 + 1] for i in range(ROWS * COLS)]
        used = sum(1 for k in keys if k > 1)
        lit += used
        print(f'  слой {layer}: назначено клавиш {used}')
    return lit


def main():
    action = sys.argv[1] if len(sys.argv) > 1 else 'show'
    path = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_FILE

    if action == 'save':
        dev = open_dev()
        data = {
            'keymap': read_keymap(dev),
            'encoders': read_encoders(dev),
            'colors': read_colors(dev),
        }
        dev.close()
        path.write_text(json.dumps(data))
        print(f'сохранено в {path}')
        describe(data['keymap'])

    elif action == 'load':
        data = json.loads(path.read_text())
        dev = open_dev()
        write_keymap(dev, data['keymap'])
        write_encoders(dev, data['encoders'])
        write_colors(dev, data['colors'])
        dev.close()
        print(f'раскладка, энкодер и подсветка восстановлены из {path}')
        describe(data['keymap'])

    elif action == 'show':
        data = json.loads(path.read_text())
        print(f'{path}:')
        describe(data['keymap'])

    else:
        sys.exit(__doc__)


main()
