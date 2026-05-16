#!/usr/bin/env bash
# ============================================================
# WoMoHome – Router WebUI Deploy
# Speichert womo_router.html persistent in /etc/womo/ und
# richtet eine zweite uhttpd-Instanz auf Port 8080 ein.
#
# Warum /etc/womo/ ?
#   /www  ist auf dem RUTX11 ein separates SquashFS-Mount
#   (read-only, kein Overlay). Nur /etc/ ist über das
#   JFFS2-Overlay persistent beschreibbar.
#
# Verwendung:
#   ./deploy.sh [router-ip]             Standard: 192.168.10.1
#   ./deploy.sh 192.168.10.1 root       (anderer SSH-User)
# ============================================================
set -e

ROUTER_IP="${1:-192.168.10.1}"
ROUTER_USER="${2:-root}"
WOMO_PORT="8080"
SSH_CTL="$(mktemp -u /tmp/womo_ssh_XXXXXX)"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=8 -o ControlMaster=auto -o ControlPath=${SSH_CTL} -o ControlPersist=30"
DIR="$(cd "$(dirname "$0")" && pwd)"
HTML="${DIR}/womo_router.html"
TMP="$(mktemp /tmp/womo_router_deploy_XXXXXX.html)"
trap 'rm -f "$TMP"; ssh -O exit -o ControlPath=${SSH_CTL} ${ROUTER_USER}@${ROUTER_IP} 2>/dev/null || true' EXIT

echo "═══════════════════════════════════════════════"
echo "  WoMoHome Router WebUI – Deploy"
echo "  Router : ${ROUTER_IP}:${WOMO_PORT}"
echo "═══════════════════════════════════════════════"
echo ""

# Passwort einmalig abfragen – gilt für SSH und WebUI (admin-User)
read -r -s -p "Passwort (SSH + WebUI): " RPASS
echo ""
echo ""

# SSH-Befehl: sshpass wenn verfügbar, sonst normales SSH (fragt nochmal)
if command -v sshpass >/dev/null 2>&1; then
  SSH="sshpass -p ${RPASS} ssh ${SSH_OPTS} ${ROUTER_USER}@${ROUTER_IP}"
else
  SSH="ssh ${SSH_OPTS} ${ROUTER_USER}@${ROUTER_IP}"
  echo "  Tipp: 'sudo apt install sshpass' vermeidet nochmalige Passwort-Eingabe."
  echo ""
fi

# Passwort sicher via Python3 einbetten – json.dumps escapet alle JS-Sonderzeichen korrekt
export _WOMO_PASS="${RPASS}"
python3 - "${HTML}" "${TMP}" << 'PYEOF'
import sys, os, json
html = open(sys.argv[1]).read()
html = html.replace('__ROUTER_PASS__', json.dumps(os.environ['_WOMO_PASS']))
open(sys.argv[2], 'w').write(html)
PYEOF
unset _WOMO_PASS

# 1. SSH-Verbindung aufbauen (ControlMaster → einmal Passwort für alle Befehle)
echo "[1/5] SSH verbinden…"
if ! $SSH 'echo ok' > /dev/null; then
  echo "✗ Router ${ROUTER_IP} nicht erreichbar."
  echo "  Im Router-WLAN? SSH aktiviert (Services → SSH)?"
  exit 1
fi
echo "  → verbunden"

# 2. ubus direkt testen (von lokalem PC aus – kein SSH-Escaping-Problem)
echo "[2/5] Passwort testen…"
export _WOMO_PASS2="${RPASS}"
DIRECT=$(curl -s --max-time 8 -X POST -H 'Content-Type: application/json' \
  --data "$(python3 -c "
import os, json
p = os.environ['_WOMO_PASS2']
print(json.dumps({'jsonrpc':'2.0','id':1,'method':'call',
  'params':['00000000000000000000000000000000','session','login',
  {'username':'admin','password':p}]}))" \
  )" \
  "http://${ROUTER_IP}/ubus" 2>/dev/null || echo '')
unset _WOMO_PASS2
if echo "$DIRECT" | grep -q '"ubus_rpc_session"'; then
  echo "  → Passwort korrekt ✓"
else
  echo "✗ WebUI admin-Passwort falsch oder Router nicht erreichbar!"
  echo "  Antwort: ${DIRECT}"
  exit 1
fi

# 3. HTML + CGI-Proxy nach /etc/womo/ schreiben (persistent über Overlay)
echo "[3/5] Datei nach /etc/womo/ schreiben…"
$SSH 'mkdir -p /etc/womo/cgi-bin && chmod 755 /etc/womo /etc/womo/cgi-bin'
$SSH 'cat > /etc/womo/index.html' < "${TMP}"
$SSH 'chmod 644 /etc/womo/index.html'
echo "  → /etc/womo/index.html"

# CGI-Proxy: leitet POST-Requests an den lokalen ubus-HTTP-Daemon (Port 80) weiter
$SSH 'cat > /etc/womo/cgi-bin/ubus && chmod +x /etc/womo/cgi-bin/ubus' << 'CGISCRIPT'
#!/bin/sh
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
# POST-Body komplett in Tempfile lesen (stdin-Piping hängt in manchen uhttpd-Versionen)
TMPF=$(mktemp)
cat > "$TMPF"
printf "Content-Type: application/json\r\n\r\n"
curl -s --max-time 10 -L --insecure \
  -X POST -H "Content-Type: application/json" \
  --data-binary "@$TMPF" \
  "http://127.0.0.1/ubus"
rm -f "$TMPF"
CGISCRIPT
echo "  → /etc/womo/cgi-bin/ubus (ubus-Proxy)"

# CGI-LTE: ruft gsmctl direkt auf (läuft als root, kein ubus file/exec nötig)
$SSH 'cat > /etc/womo/cgi-bin/lte && chmod +x /etc/womo/cgi-bin/lte' << 'CGISCRIPT'
#!/bin/sh
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
printf "Content-Type: application/json\r\n\r\n"
GSMCTL=$(command -v gsmctl 2>/dev/null || echo '')
if [ -z "$GSMCTL" ]; then
  printf '{"error":"gsmctl not found"}\n'
  exit 0
fi
OP=$("$GSMCTL" -o 2>/dev/null | tr -d '\n\r')
TYPE=$("$GSMCTL" -t 2>/dev/null | tr -d '\n\r')
DISABLED=$(uci get network.mob1s1a1.disabled 2>/dev/null | tr -d '\n\r')
# gsmctl -q gibt mehrzeilig aus: "RSSI: -44\nRSRP: -75\nSINR: 23\nRSRQ: -7"
QOUT=$("$GSMCTL" -q 2>/dev/null)
RSSI=$(echo "$QOUT" | awk '/RSSI:/{match($0,/-?[0-9]+/); print substr($0,RSTART,RLENGTH); exit}')
RSRP=$(echo "$QOUT" | awk '/RSRP:/{match($0,/-?[0-9]+/); print substr($0,RSTART,RLENGTH); exit}')
SINR=$(echo "$QOUT" | awk '/SINR:/{match($0,/-?[0-9]+/); print substr($0,RSTART,RLENGTH); exit}')
printf '{"op":"%s","type":"%s","rssi":"%s","rsrp":"%s","sinr":"%s","disabled":"%s"}\n' \
  "$OP" "$TYPE" "${RSSI:--}" "${RSRP:--}" "${SINR:--}" "${DISABLED:-0}"
CGISCRIPT
echo "  → /etc/womo/cgi-bin/lte (gsmctl-Wrapper)"

# CGI-Clients: iwinfo assoclist + dhcp.leases → JSON (läuft als root, kein ubus file/exec nötig)
$SSH 'cat > /etc/womo/cgi-bin/clients && chmod +x /etc/womo/cgi-bin/clients' << 'CGISCRIPT'
#!/bin/sh
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
printf "Content-Type: application/json\r\n\r\n"

# AP-Interface: erstes Interface im Master-Mode
AP_IFACE=""
for d in wlan0 wlan0-1 wlan1 wlan1-1; do
  iwinfo "$d" info 2>/dev/null | grep -qi 'mode.*master' && AP_IFACE="$d" && break
done
[ -z "$AP_IFACE" ] && printf '{"iface":"","clients":[]}\n' && exit 0

# assoclist in Tempfile (awk kann keine Shell-Variablen mit Newlines via -v)
ASSOC_TMP=$(mktemp)
iwinfo "$AP_IFACE" assoclist 2>/dev/null > "$ASSOC_TMP"

# awk: liest assoclist-File + /tmp/dhcp.leases direkt (kein -v Newline-Problem)
awk -v iface="$AP_IFACE" '
FNR==NR {
  # Erste Datei: dhcp.leases einlesen (MAC→Hostname + IP)
  if (NF>=4) {
    umac=toupper($2)
    ip[umac]=$3
    if ($4!="*" && $4!="") hof[umac]=$4
  }
  next
}
# Zweite Datei: assoclist parsen
# Format: "1E:9E:87:19:D3:01  -61 dBm / -107 dBm ..."
# $1 = MAC, $2 = Signal-dBm
/^[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:/ {
  mac=$1; sig=$2
  umac=toupper(mac)
  host = (umac in hof) ? hof[umac] : mac
  clientip = (umac in ip) ? ip[umac] : ""
  gsub(/"/, "", host)
  entries[++n] = sprintf("{\"mac\":\"%s\",\"host\":\"%s\",\"ip\":\"%s\",\"signal\":%s}", mac, host, clientip, sig)
}
END {
  printf "{\"iface\":\"%s\",\"clients\":[", iface
  for(i=1;i<=n;i++) { if(i>1) printf ","; printf "%s", entries[i] }
  print "]}"
}
' /tmp/dhcp.leases "$ASSOC_TMP"
rm -f "$ASSOC_TMP"
CGISCRIPT
echo "  → /etc/womo/cgi-bin/clients (assoclist-Wrapper)"

# poll.sh: sammelt alle Daten mit nativen CLI-Tools → /etc/womo/status.json
$SSH 'cat > /etc/womo/poll.sh && chmod +x /etc/womo/poll.sh' << 'POLLSCRIPT'
#!/bin/sh
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
TMP=$(mktemp /tmp/womo_poll_XXXXXX)
T_GPS=$(mktemp); T_LTE=$(mktemp)
trap 'rm -f "$TMP" "$T_GPS" "$T_LTE"' EXIT

# ── UCI (Config, kein Hardware-IO – sofort) ──────────────────────────
AP_SSID=$(uci get wireless.@wifi-iface[0].ssid     2>/dev/null | tr -d '"\n\r\\')
AP_CHAN=$( uci get wireless.radio0.channel          2>/dev/null | tr -d '\n\r')
AP_DIS=$(  uci get wireless.@wifi-iface[0].disabled 2>/dev/null | tr -d '\n\r')
AP_EN=$([ "$AP_DIS" = "1" ] && echo 0 || echo 1)
LTE_DIS=$( uci get network.mob1s1a1.disabled        2>/dev/null | tr -d '\n\r')
STA_EN=1
for i in 0 1 2 3; do
  [ "$(uci get "wireless.@wifi-iface[$i].mode" 2>/dev/null)" = "sta" ] || continue
  DIS=$(uci get "wireless.@wifi-iface[$i].disabled" 2>/dev/null | tr -d '\n\r')
  STA_EN=$([ "$DIS" = "1" ] && echo 0 || echo 1)
  break
done

# ── Parallel: GPS + LTE gleichzeitig (Hardware-IO, teuerste Ops) ─────

# GPS: ubus liest gpsd-Cache (1 Aufruf statt 6×gpsctl)
# Fallback: 6×gpsctl parallel in Subshells
{
  GPS_J=$(ubus call gpsd position 2>/dev/null)
  if [ -n "$GPS_J" ] && [ "$GPS_J" != "{}" ]; then
    printf '%s\n' "$GPS_J" > "$T_GPS"
  else
    T1=$(mktemp); T2=$(mktemp); T3=$(mktemp)
    T4=$(mktemp); T5=$(mktemp); T6=$(mktemp)
    gpsctl -i 2>/dev/null > "$T1" & gpsctl -x 2>/dev/null > "$T2" &
    gpsctl -a 2>/dev/null > "$T3" & gpsctl -v 2>/dev/null > "$T4" &
    gpsctl -p 2>/dev/null > "$T5" & gpsctl -s 2>/dev/null > "$T6" &
    wait
    FIX=$([ "$(cat "$T6" | tr -d '\n\r')" = "0" ] && echo true || echo false)
    printf '{"fix":%s,"latitude":"%s","longitude":"%s","altitude":"%s","speed":"%s","satellites":"%s"}\n' \
      "$FIX" "$(cat "$T1" | tr -d '\n\r')" "$(cat "$T2" | tr -d '\n\r')" \
      "$(cat "$T3" | tr -d '\n\r')" "$(cat "$T4" | tr -d '\n\r')" "$(cat "$T5" | tr -d '\n\r')" \
      > "$T_GPS"
    rm -f "$T1" "$T2" "$T3" "$T4" "$T5" "$T6"
  fi
} &

# LTE: gsmctl sequential (Modem-Seriellport verträgt keinen parallelen Zugriff)
# läuft aber parallel zum GPS-Block oben
{
  QOUT=$(gsmctl -q 2>/dev/null)
  LTE_OP=$(  gsmctl -o              2>/dev/null | tr -d '\n\r')
  LTE_TYPE=$(gsmctl -t              2>/dev/null | tr -d '\n\r')
  LTE_SIM=$( gsmctl -A 'AT+QUIMSLOT?' 2>/dev/null | grep -oE '[12]' | head -1)
  printf '%s\n%s\n%s\n%s\n%s\n%s\n' \
    "$LTE_OP" "$LTE_TYPE" \
    "$(echo "$QOUT" | awk '/RSSI:/{match($0,/-?[0-9]+/);print substr($0,RSTART,RLENGTH);exit}')" \
    "$(echo "$QOUT" | awk '/RSRP:/{match($0,/-?[0-9]+/);print substr($0,RSTART,RLENGTH);exit}')" \
    "$(echo "$QOUT" | awk '/SINR:/{match($0,/-?[0-9]+/);print substr($0,RSTART,RLENGTH);exit}')" \
    "$LTE_SIM" > "$T_LTE"
} &

# ── WiFi + AP (iwinfo, bekannte Interface-Namen direkt ansprechen) ────
AP_IFACE="" STA_IFACE=""
for d in wlan0-1 wlan0 wlan0-2 wlan1-1 wlan1 wlan1-2; do
  INFO=$(iwinfo "$d" info 2>/dev/null) || continue
  if   echo "$INFO" | grep -qi 'mode.*master'; then AP_IFACE="$d"
  elif echo "$INFO" | grep -qi 'mode.*client'; then STA_IFACE="$d"; fi
done
AP_CNT=0
[ -n "$AP_IFACE" ] && AP_CNT=$(iwinfo "$AP_IFACE" assoclist 2>/dev/null \
  | grep -cE '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}' || echo 0)
STA_SSID="" STA_SIG="" STA_BSSID="" STA_CHAN="" STA_CON=0
if [ -n "$STA_IFACE" ]; then
  INFO=$(iwinfo "$STA_IFACE" info 2>/dev/null)
  STA_SSID=$(echo "$INFO" | awk -F'"' '/ESSID/{if(NF>=3){print $2};exit}')
  STA_SIG=$(  echo "$INFO" | grep 'Signal'       | grep -oE '[-0-9]+ dBm'                         | grep -oE '[-0-9]+' | head -1)
  STA_BSSID=$(echo "$INFO" | grep 'Access Point' | grep -oE '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}' | head -1)
  STA_CHAN=$( echo "$INFO" | grep 'Channel'      | grep -oE '[0-9]+'                              | head -1)
  [ -n "$STA_SSID" ] && STA_CON=1
fi

# ── Warten bis GPS + LTE fertig ───────────────────────────────────────
wait

# ── GPS parsen: ubus→latitude/longitude, Fallback→lat/lon ────────────
GPS_J=$(cat "$T_GPS" 2>/dev/null)
GPS_LAT=$(echo "$GPS_J" | jsonfilter -e '@.latitude'   2>/dev/null | tr -d '\n\r')
GPS_LON=$(echo "$GPS_J" | jsonfilter -e '@.longitude'  2>/dev/null | tr -d '\n\r')
[ -z "$GPS_LAT" ] && GPS_LAT=$(echo "$GPS_J" | jsonfilter -e '@.lat' 2>/dev/null | tr -d '"\n\r')
[ -z "$GPS_LON" ] && GPS_LON=$(echo "$GPS_J" | jsonfilter -e '@.lon' 2>/dev/null | tr -d '"\n\r')
GPS_ALT=$(echo "$GPS_J" | jsonfilter -e '@.altitude'   2>/dev/null | tr -d '\n\r')
[ -z "$GPS_ALT" ] && GPS_ALT=$(echo "$GPS_J" | jsonfilter -e '@.alt' 2>/dev/null | tr -d '"\n\r')
GPS_SPD=$(echo "$GPS_J" | jsonfilter -e '@.speed'      2>/dev/null | tr -d '\n\r')
GPS_SAT=$(echo "$GPS_J" | jsonfilter -e '@.satellites' 2>/dev/null | tr -d '\n\r')
GPS_FIX_R=$(echo "$GPS_J" | jsonfilter -e '@.fix_status' 2>/dev/null | tr -d '\n\r')
GPS_FIX="none"
if [ "$GPS_FIX_R" = "1" ] \
  || ([ -n "$GPS_LAT" ] && [ "$GPS_LAT" != "0" ] && [ "$GPS_LAT" != "0.000000" ] \
   && [ -n "$GPS_LON" ] && [ "$GPS_LON" != "0" ] && [ "$GPS_LON" != "0.000000" ]); then
  GPS_FIX="fix"
fi

# ── LTE parsen ────────────────────────────────────────────────────────
LTE_OP=$(      sed -n '1p' "$T_LTE")
LTE_TYPE=$(    sed -n '2p' "$T_LTE")
LTE_RSSI=$(    sed -n '3p' "$T_LTE")
LTE_RSRP=$(    sed -n '4p' "$T_LTE")
LTE_SINR=$(    sed -n '5p' "$T_LTE")
LTE_SIMSLOT=$( sed -n '6p' "$T_LTE")

# ── JSON atomar schreiben ─────────────────────────────────────────────
json_str() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }
TS=$(date +%s)
printf '{"ts":%s,"ap":{"enabled":%s,"ssid":"%s","chan":"%s","cnt":%s},"wifi":{"enabled":%s,"ssid":"%s","signal":"%s","bssid":"%s","chan":"%s","connected":%s},"lte":{"op":"%s","type":"%s","rssi":"%s","rsrp":"%s","sinr":"%s","disabled":"%s","simslot":"%s"},"gps":{"fix":"%s","lat":"%s","lon":"%s","alt":"%s","speed":"%s","sats":"%s"}}\n' \
  "$TS" "$AP_EN" "$(json_str "$AP_SSID")" "$AP_CHAN" "${AP_CNT:-0}" \
  "$STA_EN" "$(json_str "$STA_SSID")" "${STA_SIG:-}" "${STA_BSSID:-}" "${STA_CHAN:-}" "$STA_CON" \
  "$(json_str "$LTE_OP")" "$(json_str "$LTE_TYPE")" "${LTE_RSSI:--}" "${LTE_RSRP:--}" "${LTE_SINR:--}" "${LTE_DIS:-0}" "${LTE_SIMSLOT:-}" \
  "$GPS_FIX" "${GPS_LAT:-}" "${GPS_LON:-}" "${GPS_ALT:-}" "${GPS_SPD:-}" "${GPS_SAT:-}" \
  > "$TMP" && mv "$TMP" /etc/womo/status.json && chmod 644 /etc/womo/status.json
POLLSCRIPT
echo "  → /etc/womo/poll.sh"

# CGI-simswitch: wechselt zur nächsten SIM via sim_switch
$SSH 'cat > /etc/womo/cgi-bin/simswitch && chmod +x /etc/womo/cgi-bin/simswitch' << 'CGISCRIPT'
#!/bin/sh
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
printf "Content-Type: application/json\r\n\r\n"
# Aktuelle SIM ermitteln
CUR=$(ubus call sim.switch get 2>/dev/null | jsonfilter -e '@.sim' 2>/dev/null || \
      gsmctl -A 'AT+QUIMSLOT?' 2>/dev/null | grep -oE '[12]' | head -1 || echo '1')
if [ "$CUR" = "1" ]; then NEXT="sim2"; else NEXT="sim1"; fi
/usr/bin/sim_switch "$NEXT" > /dev/null 2>&1 &
printf '{"ok":true,"msg":"Wechsel zu %s ausgelöst"}\n' "$NEXT"
CGISCRIPT
echo "  → /etc/womo/cgi-bin/simswitch (SIM-Switch)"

# cgi-bin/poll: führt poll.sh aus und liefert status.json zurück
$SSH 'cat > /etc/womo/cgi-bin/poll && chmod +x /etc/womo/cgi-bin/poll' << 'CGISCRIPT'
#!/bin/sh
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
/etc/womo/poll.sh > /dev/null 2>&1
printf "Content-Type: application/json\r\n\r\n"
cat /etc/womo/status.json 2>/dev/null || printf '{"error":"not ready"}\n'
CGISCRIPT
echo "  → /etc/womo/cgi-bin/poll"

# Cron: poll.sh alle 15 s (4× pro Minute)
$SSH '
  crontab -l 2>/dev/null | grep -v "/etc/womo/poll" > /tmp/womo_cron || true
  echo "* * * * * /etc/womo/poll.sh" >> /tmp/womo_cron
  echo "* * * * * sleep 15; /etc/womo/poll.sh" >> /tmp/womo_cron
  echo "* * * * * sleep 30; /etc/womo/poll.sh" >> /tmp/womo_cron
  echo "* * * * * sleep 45; /etc/womo/poll.sh" >> /tmp/womo_cron
  crontab /tmp/womo_cron
  /etc/init.d/cron restart 2>/dev/null || true
  rm -f /tmp/womo_cron
'
echo "  → Cron: poll.sh alle 15 s"

# Erstes poll.sh sofort ausführen damit status.json existiert
$SSH '/etc/womo/poll.sh'
echo "  → Erster Poll ausgeführt"

# 3. Zweite uhttpd-Instanz auf Port 8080 konfigurieren
echo "[4/5] uhttpd-Instanz auf Port ${WOMO_PORT} einrichten…"
$SSH "
  uci set uhttpd.womo=uhttpd
  uci set uhttpd.womo.listen_http='0.0.0.0:${WOMO_PORT}'
  uci set uhttpd.womo.home='/etc/womo'
  uci set uhttpd.womo.cgi_prefix='/cgi-bin'
  uci set uhttpd.womo.redirect_https='0'
  uci set uhttpd.womo.max_requests='5'
  uci commit uhttpd
  /etc/init.d/uhttpd restart
"
echo "  → uhttpd.womo konfiguriert und neu gestartet"

# Firewall: Port 8080 von LAN erlauben (nur hinzufügen wenn noch nicht vorhanden)
$SSH "
  if ! uci show firewall 2>/dev/null | grep -q 'womo_webui'; then
    uci add firewall rule > /dev/null
    uci set firewall.@rule[-1].name='womo_webui'
    uci set firewall.@rule[-1].src='lan'
    uci set firewall.@rule[-1].dest_port='${WOMO_PORT}'
    uci set firewall.@rule[-1].target='ACCEPT'
    uci set firewall.@rule[-1].proto='tcp'
    uci commit firewall
    /etc/init.d/firewall reload
  fi
" 2>/dev/null || true

# 5. CGI-Proxy + Erreichbarkeit prüfen
echo "[5/5] HTTP-Zugriff und CGI-Proxy prüfen…"
sleep 2
HTTP_CODE=$(curl -s -o /dev/null -w '%{http_code}' --max-time 8 \
  "http://${ROUTER_IP}:${WOMO_PORT}/" 2>/dev/null || echo '000')
if [ "$HTTP_CODE" = '200' ]; then
  echo "  → Seite erreichbar (HTTP 200) ✓"
else
  echo "  ⚠ Seite: HTTP ${HTTP_CODE}"
fi

# CGI-Proxy end-to-end testen
PROXY_RESULT=$(curl -s -X POST -H 'Content-Type: application/json' --max-time 8 \
  --data '{"jsonrpc":"2.0","id":1,"method":"call","params":["0000000000000000000000000000000000","session","login",{"username":"admin","password":"TESTONLY"}]}' \
  "http://${ROUTER_IP}:${WOMO_PORT}/cgi-bin/ubus" 2>/dev/null || echo 'FEHLER')
if echo "$PROXY_RESULT" | grep -q '"result"\|"error"'; then
  echo "  → CGI-Proxy antwortet ✓ (ubus erreichbar)"
else
  echo "  ⚠ CGI-Proxy: ${PROXY_RESULT}"
  echo "    SSH-Debug: ssh root@${ROUTER_IP} cat /etc/womo/cgi-bin/ubus"
fi

# LTE-CGI testen
LTE_RESULT=$(curl -s --max-time 8 \
  "http://${ROUTER_IP}:${WOMO_PORT}/cgi-bin/lte" 2>/dev/null || echo 'FEHLER')
if echo "$LTE_RESULT" | grep -q '"op"'; then
  echo "  → LTE-CGI antwortet ✓: ${LTE_RESULT}"
else
  echo "  ⚠ LTE-CGI: ${LTE_RESULT}"
  echo "    SSH-Debug: ssh root@${ROUTER_IP} sh /etc/womo/cgi-bin/lte"
fi

echo ""
echo "══════════════════════════════════════════════════════"
echo "  ✓ Fertig! Kein Login nötig."
echo ""
echo "  Öffne im Browser (im Router-WLAN):"
  echo "    http://${ROUTER_IP}:${WOMO_PORT}/"
echo ""
echo "  Persistent – bleibt nach Reboot erhalten."
echo "══════════════════════════════════════════════════════"
echo ""
echo "  Die Datei bleibt nach Router-Reboot erhalten."
echo "══════════════════════════════════════════════════════"
