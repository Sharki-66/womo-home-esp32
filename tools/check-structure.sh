#!/usr/bin/env bash
# Prüft die Projektstruktur auf Konsistenz
# Aufruf: ./tools/check-structure.sh
#   oder automatisch via pre-commit hook

set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

ERRORS=0
WARNINGS=0

err()  { echo "❌ ERROR:   $1"; ((ERRORS++)); }
warn() { echo "⚠️  WARNING: $1"; ((WARNINGS++)); }
ok()   { echo "✅ OK:      $1"; }

echo "═══════════════════════════════════════════"
echo "  WoMo Home – Strukturprüfung"
echo "═══════════════════════════════════════════"
echo ""

# --- Pflicht-Verzeichnisse ---
echo "📁 Verzeichnisse..."
for dir in firmware/display firmware/sensorboard hardware/schematics \
           hardware/datasheets docs sdcard tests archive .github; do
    if [[ -d "$dir" ]]; then
        ok "$dir/"
    else
        err "$dir/ fehlt!"
    fi
done
echo ""

# --- Pflicht-Dateien ---
echo "📄 Wichtige Dateien..."
for file in README.md docs/README.md .github/copilot-instructions.md \
            .gitignore womo-sensor.code-workspace womo-display.code-workspace \
            firmware/display/CMakeLists.txt firmware/sensorboard/CMakeLists.txt; do
    if [[ -f "$file" ]]; then
        ok "$file"
    else
        err "$file fehlt!"
    fi
done
echo ""

# --- Keine Dateien am falschen Ort ---
echo "🔍 Fehlplatzierte Dateien..."

# Firmware-Ordner dürfen nicht direkt im Root liegen
for stale in firmware-modem firmware-sensor firmware-walter; do
    if [[ -d "$stale" ]]; then
        err "$stale/ liegt noch im Root (gehört nach archive/ oder firmware/)"
    fi
done

# esp-iot-solution darf nicht mehr existieren
if [[ -d "esp-iot-solution" ]]; then
    err "esp-iot-solution/ liegt noch auf der Platte (wurde aus Git entfernt, Ordner löschen!)"
fi

# Keine build-Ordner im Git
if git ls-files --cached | grep -qE '^(firmware/display|firmware/sensorboard)/build/'; then
    warn "build/-Ordner sind im Git-Index (sollten in .gitignore stehen)"
fi

# Keine KiCad-Backups im Git
if git ls-files --cached | grep -qE '\-backups/|\.kicad_sch-bak|fp-info-cache'; then
    warn "KiCad-Backup-Dateien sind noch im Git-Index"
fi
echo ""

# --- Verwaiste große Dateien ---
echo "📦 Große Dateien (>10 MB, getrackt)..."
while IFS= read -r f; do
    if [[ -f "$f" ]]; then
        size=$(stat --printf="%s" "$f" 2>/dev/null || echo 0)
        if (( size > 10485760 )); then
            warn "$f ist $(( size / 1048576 )) MB groß"
        fi
    fi
done < <(git ls-files)
echo ""

# --- Doku-Links prüfen (einfache Prüfung) ---
echo "🔗 Doku-Querverweise..."
broken=0
while IFS= read -r link; do
    # Nur relative Links prüfen (keine http/https)
    if [[ "$link" == http* ]] || [[ "$link" == "#"* ]]; then
        continue
    fi
    # Link relativ zum docs/-Verzeichnis auflösen
    resolved="docs/$link"
    if [[ ! -e "$resolved" ]]; then
        warn "Toter Link in docs/README.md: $link → $resolved"
        broken=$((broken + 1))
    fi
done < <(grep -oP '\]\(\K[^)]+' docs/README.md 2>/dev/null || true)
if (( broken == 0 )); then
    ok "Alle Links in docs/README.md erreichbar"
fi
echo ""

# --- Zusammenfassung ---
echo "═══════════════════════════════════════════"
if (( ERRORS > 0 )); then
    echo "  ❌ $ERRORS Fehler, $WARNINGS Warnungen"
    echo "═══════════════════════════════════════════"
    exit 1
elif (( WARNINGS > 0 )); then
    echo "  ⚠️  Keine Fehler, $WARNINGS Warnungen"
    echo "═══════════════════════════════════════════"
    exit 0
else
    echo "  ✅ Alles konsistent!"
    echo "═══════════════════════════════════════════"
    exit 0
fi
