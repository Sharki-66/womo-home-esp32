---
type: architecture-overview
title: Marketing-/Doku-Website (Astro)
description: Übersicht über das Astro-Projekt website/, das WoMoHome öffentlich präsentiert (Marketing, Fotos, Schaltplan-Ansicht, Impressum/Datenschutz) und über Netlify samt einer kleinen Seitenaufrufzähler-Funktion bereitgestellt wird; rein präsentationsorientiert und keine Quelle der Wahrheit für Firmware-/Hardware-Fakten.
tags: [website, astro, netlify, documentation, hardware, presentation-layer]
sources:
  - id: openwiki-source-54c78a773ffb5a9f658c0477
    resource: repo://netlify.toml
  - id: openwiki-source-d88dd82fdc1561fa990b757e
    resource: repo://website/astro.config.mjs
  - id: openwiki-source-b59bac7bbf6d2fa90d531faa
    resource: repo://website/netlify/functions/counter.mjs
  - id: openwiki-source-1bcf1c7d77193ec95fe9d655
    resource: repo://website/package.json
  - id: openwiki-source-0fda89fe98b26bc41f50dc55
    resource: repo://website/src/pages/datenschutz.astro
  - id: openwiki-source-51321f59f381a8f9d01eb6b9
    resource: repo://website/src/pages/horizont.astro
  - id: openwiki-source-29377e99099053af7f6bb77a
    resource: repo://website/src/pages/index.astro
  - id: openwiki-source-59ff67ef78c61380f8c4c204
    resource: repo://website/src/pages/schaltplan.astro
generated: { by: "openwiki/0.7.2", at: "2026-10-19T20:49:57.319Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-19T20:49:57.319Z
---

## Zweck

`website/` ist eine eigenständige [Astro](https://astro.build)-Static-Site, getrennt von den
Projekten Firmware (`firmware/`) und Hardware (`hardware/`) in diesem Repository. Sie ist die
öffentliche Marketing- und Dokumentationsoberfläche des WoMoHome-Projekts: eine Landingpage, eine
flugzeugartige "Künstlicher Horizont"-Live-Demo-Seite, ein Impressum, eine Datenschutzerklärung und
eine Seite, die den Schaltplan des Sensorboards als eingebettetes SVG darstellt. Es handelt sich
ausschließlich um eine Präsentationsschicht — sie implementiert oder besitzt kein Firmware-Verhalten,
kein Funkprotokoll und kein Hardware-Pinout; sie zeigt lediglich statische Inhalte (Bilder, SVGs,
PDFs) an, die manuell aus den Hardware-/Firmware-Quellen exportiert wurden.

## Projektstruktur

- `website/astro.config.mjs` — Astro-Konfiguration; verwendet das Plugin `@tailwindcss/vite` für
  Tailwind-CSS-v4-Styling, ansonsten Standard-Astro-Einstellungen (kein SSR-Adapter, keine
  Integrationen außer Tailwind).
- `website/src/layouts/Layout.astro` — gemeinsame HTML-Hülle (Meta-Tags, Favicon, ein
  Tag/Nacht-`data-theme`-Umschalter, der sich an der lokalen Uhrzeit orientiert, sowie `<slot />`
  für den Seiteninhalt). Alle Seiten betten ihr Markup in dieses Layout ein.
- `website/src/pages/` — dateibasierte Routen, jeweils eine `.astro`-Seite:
  - `index.astro` — die Marketing-Landingpage (Hero, Features, Foto-Galerie/Lightbox,
    "Selbst bauen"- vs. "Kaufen"-Bereiche, FAQ, Kontaktformular und der
    Seitenaufrufzähler im Footer).
  - `horizont.astro` — eine live/simulierte "Künstlicher Horizont"-Widget-Demo, die beschreibt,
    wie der Webserver des Sensorboards dieselbe Ansicht per WLAN aus einer BNO055-IMU
    bereitstellen kann.
  - `schaltplan.astro` — rendert den Sensorboard-Schaltplan (`/Sensorboard.svg`) sowie
    Metadaten-Karten mit Links zum CERN-OHL-P-v2-Lizenztext, zu den KiCad-Quelldateien auf
    GitHub und zu einem Hersteller-PDF.
  - `impressum.astro` — das Impressum.
  - `datenschutz.astro` — die Datenschutzerklärung; behandelt u. a. das Hosting bei Netlify
    (Server-Logfiles mit IP-Adresse, Zeitstempel, aufgerufener Seite, Referrer, User-Agent),
    den Seitenaufrufzähler (nur eine Gesamtzahl, keine personenbezogenen Daten, keine Cookies),
    das Kontaktformular (`mailto:`, keine serverseitige Speicherung) sowie externe Links und
    Betroffenenrechte nach DSGVO.
- `website/public/` — statisch ausgelieferte Assets: Logo, Hero-Hintergrund, Gerätefotos
  (`photos/screen*.jpg`), das exportierte `Sensorboard.svg`-Schaltbild, `system-overview*.svg`-
  Diagramme und ein Hersteller-PDF (`LT_45x.pdf`).
- `website/package.json` — Astro-6- + Tailwind-CSS-4-Toolchain plus `@netlify/blobs` als
  Laufzeitabhängigkeit der Counter-Funktion; die Skripte sind die Standardbefehle
  `astro dev` / `astro build` / `astro preview`.
- `website/netlify/functions/counter.mjs` — eine einzelne Netlify-Function (siehe unten), die
  eine kleine serverseitige API-Route für den Seitenaufrufzähler bereitstellt.

## Seitenaufrufzähler (Netlify Function + Netlify Blobs)

`website/netlify/functions/counter.mjs` ist der einzige serverseitige Codepfad der Website. Es
handelt sich um eine Netlify Function v2, die per `export const config = { path: '/api/counter' }`
unter `/api/counter` erreichbar ist:

- Sie akzeptiert ausschließlich `POST`-Requests; alle anderen Methoden erhalten `405 Method Not
  Allowed`.
- Über `getStore({ name: 'counter', consistency: 'strong' })` aus `@netlify/blobs` liest sie den
  aktuellen Zählerstand (`pageviews`), erhöht ihn um eins und schreibt ihn zurück; die neue Zahl
  wird als `{ count }`-JSON mit `Cache-Control: no-store` zurückgegeben.
- Das Inkrement ist laut Code-Kommentar nicht atomar — gleichzeitige Aufrufe können sich
  gegenseitig überschreiben —, was für einen reinen Anzeigezähler als vertretbar gilt.
- Gespeichert wird ausschließlich die Gesamtzahl; es werden weder IP-Adresse noch Uhrzeit noch
  User-Agent persistiert.
- Aufgerufen wird die Funktion aus dem Footer-Skript in `index.astro` (`fetch('/api/counter', {
  method: 'POST' })`), das bei Erfolg die zurückgegebene Zahl als "N Seitenaufrufe" anzeigt und
  bei einem Fehler die Zeile einfach leer lässt.
- Die Datenschutz-Implikationen dieses Zählers und des Netlify-Hostings selbst sind in
  `datenschutz.astro` dokumentiert.

## Verhältnis zu Firmware/Hardware (nur lesend, präsentationsorientiert)

Die Website bettet vorab generierte Artefakte aus dem Hardware-Projekt ein, statt sie selbst zu
erzeugen oder zu validieren:

- `schaltplan.astro` zeigt einen statischen `Sensorboard.svg`-Export und verlinkt auf die
  tatsächlichen KiCad-Quellen (`hardware/schematics/Sensorboard` auf GitHub) sowie auf die
  CERN-OHL-P-v2-Lizenz. Der Seitentext selbst nennt die Board-Revision (Rev. 0.2), den MCU
  (ESP32-S3 DevKitC-1, N16R8), die KiCad-Version (KiCad 8) und den Sensorsatz (BME680, BME280,
  BNO055, HX711, PCF8523, INA226, Batterie-/Tank-ADC) rein als aus dem Hardware-Design
  übernommenen Bildunterschriften-Text — er wird nicht live aus den `.kicad_sch`-Dateien
  abgeleitet.
- Die Demo-Seite `horizont.astro` weist ausdrücklich darauf hin, dass das echte Artefakt auf dem
  eigenen Webserver des Sensorboards liegt (der diese Ansicht live aus BNO055-Daten ausliefert);
  die Website-Kopie ist ein simulierter/statischer Platzhalter zu Marketingzwecken.
- Da die SVGs, Pin-/Sensorlisten und PDFs auf der Website manuell exportierte/kopierte
  Momentaufnahmen sind, können sie vom tatsächlichen Schaltplan und von der Firmware abweichen.
  Das KiCad-Projekt unter `hardware/` sowie der Firmware-Quellcode/die READMEs bleiben die
  maßgebliche Quelle der Wahrheit für Pinouts, Protokolle und Board-Revisionen — siehe
  [Boards & KiCad](../hardware/boards-and-kicad.md). Leser, die aktuelle, maßgebliche
  Pin-/Protokoll-/Board-Details benötigen, sollten diese Hardware-Dokumentation und den
  Firmware-Quellcode direkt konsultieren, nicht die Kopie oder Bildunterschriften der Website.

## Build und Deployment (Netlify)

Die Site wird über [Netlify](https://www.netlify.com) gebaut und bereitgestellt, konfiguriert durch
die `netlify.toml` im Repository-Root:

```toml
[build]
  base = "website"
  command = "npm run build"
  publish = "dist"
```

- `base = "website"` weist Netlify an, `website/` als Build-Root zu behandeln, obwohl die
  Konfigurationsdatei im Repository-Root liegt, sodass die anderen Projekte des Monorepos
  (`firmware/`, `hardware/`) nicht Teil des Website-Builds sind. Da keine gesonderte
  `functions`-Einstellung existiert, erkennt Netlify `netlify/functions/` relativ zu diesem
  Build-Root — also `website/netlify/functions/` — als Verzeichnis für Functions wie
  `counter.mjs`.
- `command = "npm run build"` führt den Standard-Astro-Produktionsbuild aus (`astro build`).
- `publish = "dist"` ist das statische Ausgabeverzeichnis (`website/dist/`), das Netlify nach
  Abschluss des Builds ausliefert/per CDN verteilt.

Lokal wird derselbe Build mit `npm install && npm run build` aus `website/` heraus erzeugt, mit
`npm run preview` angesehen und mit `npm run dev` entwickelt (Standard-Dev-Server unter
`localhost:4321`), gemäß `website/README.md`.

## Invarianten und Grenzen des Geltungsbereichs

- Die Website ist größtenteils ein statischer Build ohne eigenes Backend; ihr einziger
  serverseitiger Codepfad ist die einzelne Netlify Function `counter.mjs` für den
  Seitenaufrufzähler (siehe oben) — es gibt keine serverseitige Logik, die KiCad-Schaltpläne neu
  rendert oder Firmware-Header einliest.
- Alle gerätespezifischen Fakten auf der Site (Sensorlisten, Board-Revisionen, Pinouts,
  Protokolle) sind illustrativer Marketingtext bzw. statische Exporte, nicht zur Build-Zeit aus
  den Firmware-/Hardware-Quellen generiert oder gegen sie validiert.
- Änderungen am tatsächlichen Hardware-Design oder Firmware-Verhalten pflanzen sich nicht
  automatisch auf die Website fort; Schaltplan-SVGs und Beschreibungstext müssen manuell
  reexportiert/aktualisiert werden, um synchron zu bleiben.
- Siehe [Quickstart](../quickstart.md) dazu, wie das Website-Projekt neben den Firmware- und
  Hardware-Projekten in das Gesamtrepository passt.
