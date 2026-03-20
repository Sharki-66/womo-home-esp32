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
$SSH 'mkdir -p /etc/womo/cgi-bin'
$SSH 'cat > /etc/womo/index.html' < "${TMP}"
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
