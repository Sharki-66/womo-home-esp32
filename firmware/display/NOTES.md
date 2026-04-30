# Display-Firmware Notizen

## Verhalten der SD-Karte

- Wichtige Arbeitshypothese: Die SD-Karten-Probleme treten vor allem nach einem durch den Monitor ausgelösten Reset auf, nicht nach einem echten Power-up.
- Nach einem normalen Einschalten per Versorgungsspannung scheint die SD-Karte deutlich zuverlässiger zu initialisieren und zu laufen.
- Deshalb sollten Logs und Fehler nach einem Monitor-Reset nicht automatisch als repräsentativ für das normale Laufzeitverhalten gewertet werden.
- Wenn wir an den SD-Problemen weiterarbeiten, sollten wir zuerst die Robustheit bei Reset/Re-Init verbessern, bevor wir Icons oder Assets komplett aus der SD herausziehen.

## Aktuell stabile Erkenntnisse

- Die Bootzeit wurde deutlich verbessert, nachdem die RS485-Initialisierung aus dem kritischen Bootpfad herausgezogen wurde.
- Die asynchrone SD-Initialisierung ist aktuell die bessere Wahl als eine frühe synchrone SD-Initialisierung im Bootpfad.
- Die TLS-Stabilität wurde verbessert, indem der mbedTLS-Speicherbedarf reduziert und Hardware-AES deaktiviert wurde.
- Ein Reboot konnte auf NVS-Schreibzugriffe aus `router_poll_task` zurückgeführt werden; dieser Hot-Path schreibt die GPS-Position für das Wetter nun nicht mehr direkt ins NVS.

## Offene Punkte

- Das Laden von Wetter-Icons von der SD kann zur Laufzeit weiterhin fehlschlagen mit:
  - `sdmmc_read_sectors: not enough mem, err=0x101`
- Das LTE-Router-Polling kann zurückliefern:
  - `ERROR: Couldn't retrieve data`
  und sollte dann als offline bzw. `0%` angezeigt werden, nicht mit alten guten Signalwerten.
