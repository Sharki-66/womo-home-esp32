#!/usr/bin/env bash
# gasbee-monitor.sh – serieller Monitor für GasBee nRF (XIAO nRF52840, USB-CDC)
#
# Findet alle angesteckten GasBees über die udev-Symlinks /dev/ttyGASBEE_<Seriennummer>
# (Regel: tools/udev/99-esp32-womo.rules). Eine GasBee → Monitor startet direkt,
# mehrere → Auswahl per Nummer, keine → Hinweis.
#
# Aufruf:  tools/gasbee-monitor.sh [Seriennummer-Teil]
#          Optionaler Filter wählt die GasBee, deren Seriennummer den Text enthält.
# Beenden: Strg+]

set -u

ports=()
for p in /dev/ttyGASBEE_*; do
    [ -e "$p" ] && ports+=("$p")
done

if [ "$#" -ge 1 ] && [ -n "$1" ]; then
    filtered=()
    for p in "${ports[@]}"; do
        [[ "$p" == *"$1"* ]] && filtered+=("$p")
    done
    ports=("${filtered[@]}")
fi

if [ "${#ports[@]}" -eq 0 ]; then
    echo "Keine GasBee gefunden (/dev/ttyGASBEE_* fehlt)."
    echo "- XIAO angesteckt und versorgt? Im UF2-Bootloader (XIAO-SENSE-Laufwerk) gibt es keinen Port."
    echo "- udev-Regel installiert? sudo cp tools/udev/99-esp32-womo.rules /etc/udev/rules.d/ &&"
    echo "  sudo udevadm control --reload-rules && sudo udevadm trigger"
    exit 1
fi

if [ "${#ports[@]}" -eq 1 ]; then
    port="${ports[0]}"
else
    echo "Mehrere GasBees angesteckt:"
    PS3="Nummer wählen: "
    select port in "${ports[@]}"; do
        [ -n "${port:-}" ] && break
        echo "Ungültige Auswahl."
    done
fi

echo "Monitor: $port -> $(readlink -f "$port")   (Beenden: Strg+])"

# miniterm aus pyserial: System-Python, sonst die ESP-IDF-Umgebung
if python3 -c "import serial" 2>/dev/null; then
    exec python3 -m serial.tools.miniterm "$port" 115200
fi
for py in "$HOME"/.espressif/python_env/*/bin/python; do
    if [ -x "$py" ] && "$py" -c "import serial" 2>/dev/null; then
        exec "$py" -m serial.tools.miniterm "$port" 115200
    fi
done
echo "pyserial nicht gefunden (python3 -m pip install pyserial)."
exit 1
