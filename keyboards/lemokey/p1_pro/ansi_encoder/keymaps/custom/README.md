# Per-layer RGB for Lemokey P1 Pro

**English** · [Русский](README.ru.md)

Per-key RGB lighting that is stored **per layer** and edited from the browser — no
recompilation, no flashing. Neither Keychron Launcher nor VIA can do this: they expose only
brightness, effect, speed and one global colour.

Keychron Launcher keeps working exactly as before — keymap, layers, macros, effects and its
own per-key painting on the base layer. The firmware speaks its protocol byte for byte.

---

## What it does

Switch to a layer and it lights up the way you painted it. Layers you have not touched keep
using whatever effect is set in the Launcher, so nothing changes until you ask for it.

A typical use: on the Fn layer only the keys that actually do something are lit, coloured by
what they do — media in green, Bluetooth and battery in cyan, RGB controls in blue, the rest
in white, everything unassigned dark.

The web tool reads the keymap straight from the keyboard, so after you move a key in the
Launcher the highlight follows it — press one button and the picture is rebuilt.

## Hardware

| | |
|---|---|
| Keyboard | Lemokey P1 Pro, ANSI, knob version |
| MCU | WB32F3G71RCT6, ARM Cortex-M3 |
| Flash | 256 KB, ~75 KB used |
| RAM | 36 KB, ~28 KB used |
| EEPROM | 2 KB external over I2C |
| LEDs | 81 |
| Matrix | 6 rows × 15 columns |

## Requirements

- QMK build environment with an ARM toolchain (`gcc-arm-none-eabi`)
- [`wb32-dfu-updater_cli`](https://github.com/WestberryTech/wb32-dfu-updater) — not packaged
  in most distributions, build it from source
- Chrome or another Chromium-based browser for the paint tool (WebHID is not available in
  Firefox)

## Build

```bash
qmk compile -kb lemokey/p1_pro/ansi_encoder -km custom
```

## Flash

Unplug the cable, hold **Esc**, plug the cable back in while still holding it. The side
switch must be in the **Cable** position. There is also a reset button on the bottom, under
the space bar.

The keyboard stops being a keyboard while in the bootloader, so the helper script waits for
the device on its own and needs no input once started:

```bash
bash tools/flash_custom.sh
```

It flashes the firmware, restores the stock image if the write fails, and then checks the
kernel log to confirm that all three USB interfaces came back — a returning VID:PID alone
does not prove the keyboard is healthy.

**Flashing wipes the EEPROM.** The keymap resets to stock and the painting is lost. Export
your keymap from the Launcher first; the painting can be rebuilt with one click afterwards.

## Paint tool

```bash
cd tools
python3 -m http.server 8777 --bind 127.0.0.1
```

Open `http://127.0.0.1:8777/paint.html` in Chrome. A local server is required — WebHID
refuses to work from a file opened directly.

- pick a layer, left click paints, right click turns a key off
- **Only assigned** reads the keymap and lights every key that does something on this layer
- the per-layer checkbox is the master switch: unchecked, the layer goes back to the regular
  effect
- nothing reaches the keyboard until **Write and save**

Close the Keychron Launcher tab first — whoever connects first owns the device.

## How it is stored

416 bytes of EEPROM, laid out as:

| field | bytes | purpose |
|---|---|---|
| `custom` | 1 | bit N set — layer N has its own painting |
| `used` | 1 | palette slots taken |
| `palette` | 48 | 16 HSV colours |
| `base` | 243 | layer 0, full colour |
| `packed` | 123 | layers 1–3, palette indices, half a byte per LED |

Layer 0 keeps full colours because the Launcher writes arbitrary values there. Layers 1–3
are packed: a layer rarely uses more than five or six colours, and an index fits in four
bits. That is 416 bytes instead of 973, and the 557 bytes saved go to macros.

This is invisible from the outside — both the Launcher and the paint tool send ordinary HSV.
The firmware looks the colour up in the palette, adds a new one while slots remain, and
falls back to the nearest match once all 16 are taken. Before writing to EEPROM the palette
is rebuilt from the colours actually in use, otherwise leftovers from earlier edits would
fill every slot.

## Protocol

Section `0xA8`. Commands `0x01`–`0x0A` are Keychron's own, captured from the Launcher over
WebHID and reproduced exactly; `0x40`–`0x43` are the additions that carry a layer number.

```
a8 01                                 protocol version
a8 02                                 save to EEPROM
a8 03                                 indicator config
a8 04 <disable> <h> <s> <v>           set indicator config
a8 05                                 LED count -> 0x51
a8 06 <row> ff ff ff                  matrix row -> 15 LED indices, ff where no key
a8 07 / a8 08 <type>                  per-key mode
a8 09 <from> <count>                  read layer 0 colours
a8 0a <from> <count> <h s v>...       write layer 0 colours

a8 40 / a8 41 <mask>                  which layers have their own painting
a8 42 <layer> <from> <count>          read layer colours
a8 43 <layer> <from> <count> <hsv>... write layer colours
```

Replies are `a8 <command> <00 ok / 01 rejected> <data...>`. At most nine colours fit in one
32-byte packet.

The keymap is read through the standard VIA command `0x12`, at offset `layer × 6 × 15 × 2`.

## Upstream changes

One, six lines in `keyboards/lemokey/common/lemokey_raw_hid.c`, guarded by
`#ifdef LAYER_RGB_ENABLE`: Lemokey boards do not handle section `0xA8` at all, so the command
would be dropped silently. Everything else lives inside this keymap.

## Notes

Sources for firmware V1.1.0 were never published. P1 Pro exists only in the
`wireless_playground` branch, where the code predates it — `device_version` reads `1.0.1` and
per-key RGB is absent entirely. Builds from this tree therefore report themselves as
`v1.0.1`; tell yours apart by the **build date** in the reply to `0xA1`, not by the version
number.

QMK's stock `eeconfig_init_user_datablock()` allocates a buffer the size of the whole user
block **on the stack**. With a block this large it overflows and the firmware dies on the
first boot with an empty EEPROM — the third USB interface never comes up and the encoder
loses volume control. The function is `weak` and is replaced here.

## License

GPL-2.0-or-later, same as QMK.
