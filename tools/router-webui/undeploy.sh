#!/usr/bin/env bash
# ============================================================
# WoMoHome – Router WebUI Undeploy
# Macht alle Änderungen von deploy.sh vollständig rückgängig.
# ============================================================
set -e

ROUTER_IP="${1:-192.168.10.1}"
ROUTER_USER="${2:-root}"
SSH_CTL="$(mktemp -u /tmp/womo_ssh_XXXXXX)"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=8 -o ControlMaster=auto -o ControlPath=${SSH_CTL} -o ControlPersist=30"
SSH="ssh ${SSH_OPTS} ${ROUTER_USER}@${ROUTER_IP}"
trap 'ssh -O exit -o ControlPath=${SSH_CTL} ${ROUTER_USER}@${ROUTER_IP} 2>/dev/null || true' EXIT

echo "═══════════════════════════════════════════════"
echo "  WoMoHome Router WebUI – Undeploy"
echo "  Router : ${ROUTER_IP}"
echo "═══════════════════════════════════════════════"
echo ""

echo "[1/5] SSH verbinden…"
$SSH 'echo ok' > /dev/null
echo "  → verbunden"

echo "[2/5] Datei entfernen…"
$SSH 'rm -rf /etc/womo'
echo "  → /etc/womo gelöscht"

echo "[3/5] uhttpd-Instanz 'womo' entfernen…"
$SSH '
  if uci show uhttpd.womo > /dev/null 2>&1; then
    uci delete uhttpd.womo
    uci commit uhttpd
    /etc/init.d/uhttpd restart
    echo "  → uhttpd.womo entfernt"
  else
    echo "  → uhttpd.womo war nicht vorhanden"
  fi
'

echo "[3/5] Cron-Einträge entfernen…"
$SSH '
  if crontab -l 2>/dev/null | grep -q "/etc/womo/poll"; then
    crontab -l 2>/dev/null | grep -v "/etc/womo/poll" | crontab -
    /etc/init.d/cron restart 2>/dev/null || true
    echo "  → Cron-Einträge entfernt"
  else
    echo "  → Keine Cron-Einträge gefunden"
  fi
'

echo "[4/5] Firewall-Regel 'womo_webui' entfernen…"
$SSH '
  IDX=$(uci show firewall 2>/dev/null | grep -n "womo_webui" | head -1 | cut -d: -f1)
  if [ -n "$IDX" ]; then
    # Index aus "firewall.@rule[N].name=womo_webui" extrahieren
    RULE=$(uci show firewall 2>/dev/null | grep "womo_webui" | grep -o "@rule\[[0-9]*\]" | head -1)
    if [ -n "$RULE" ]; then
      uci delete firewall.${RULE}
      uci commit firewall
      /etc/init.d/firewall reload
      echo "  → Firewall-Regel entfernt"
    fi
  else
    echo "  → Keine Firewall-Regel gefunden"
  fi
'

echo "[5/5] Abschluss…"
echo ""
echo "══════════════════════════════════════════════════════"
echo "  ✓ Alles rückgängig gemacht."
echo "══════════════════════════════════════════════════════"
