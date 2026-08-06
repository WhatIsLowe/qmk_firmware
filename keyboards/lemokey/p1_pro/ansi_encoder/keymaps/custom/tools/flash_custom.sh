#!/usr/bin/env bash
# Ждёт DFU, заливает нашу сборку и проверяет, что поднялись все три USB-интерфейса.
# При сбое заливки сразу возвращает сток.

REPO=$(cd "$(dirname "$0")/../../../../../.." && pwd)
CUSTOM=$REPO/lemokey_p1_pro_ansi_encoder_custom.bin
STOCK=$REPO/reference/p1_pro_ansi_encoder_v1.1.0_2507241646.bin
DFU=$HOME/.local/bin/wb32-dfu-updater_cli
WAIT_DFU=600
WAIT_BACK=90

say() { echo "[$(date +%H:%M:%S)] $*"; }

[ -f "$CUSTOM" ] || { say "ОШИБКА: нет файла $CUSTOM"; exit 1; }
START_MARK=$(date "+%Y-%m-%d %H:%M:%S")

say "жду бутлоадер: вынуть кабель, зажать Esc, воткнуть кабель не отпуская Esc"
for ((i = 0; i < WAIT_DFU; i++)); do
    "$DFU" -l 2>/dev/null | grep -q "Found DFU" && break
    sleep 1
done

if ! "$DFU" -l 2>/dev/null | grep -q "Found DFU"; then
    say "ОШИБКА: за $WAIT_DFU с бутлоадер не появился. Клавиатура не тронута."
    exit 1
fi

say "заливаю сборку ($(stat -c %s "$CUSTOM") байт)"
if ! timeout 180 "$DFU" -D "$CUSTOM" 2>&1 | tail -4; then
    say "ЗАЛИВКА НЕ ПРОШЛА — возвращаю сток"
    timeout 180 "$DFU" -D "$STOCK" 2>&1 | tail -3
    "$DFU" -R 2>&1 | tail -1
    exit 1
fi

"$DFU" -R 2>&1 | tail -1

for ((i = 0; i < WAIT_BACK; i++)); do
    lsusb | grep -q "362d:0303" && break
    sleep 1
done

if ! lsusb | grep -q "362d:0303"; then
    say "клавиатура не появилась — выдерни и воткни кабель"
    exit 1
fi
say "клавиатура вернулась: $(lsusb | grep 362d:0303)"

# Третий интерфейс поднимается последним, ему нужно время
sleep 15

say "--- проверка USB-интерфейсов ---"
if journalctl -k --since "$START_MARK" --no-pager 2>/dev/null | grep -q "can't add hid device"; then
    say "ПЛОХО: интерфейс не поднялся, ошибка та же"
    journalctl -k --since "$START_MARK" --no-pager | grep -E "can't add|probe with driver" | tail -3
else
    say "ошибок добавления устройств нет"
fi

for what in "Consumer Control" "System Control" "Mouse"; do
    if journalctl -k --since "$START_MARK" --no-pager 2>/dev/null | grep -q "$what"; then
        say "  есть: $what"
    else
        say "  НЕТ: $what"
    fi
done

say "hidraw-узлы клавиатуры:"
journalctl -k --since "$START_MARK" --no-pager 2>/dev/null | grep -oE "hidraw[0-9]+" | sort -u | tr '\n' ' '
echo
