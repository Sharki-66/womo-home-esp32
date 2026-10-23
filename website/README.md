# WoMoHome – Projekt-Website

Statische Projektseite (Astro 6 + Tailwind 4), gehostet auf Netlify.
Build-Konfiguration: `../netlify.toml` (Basis `website/`, Ausgabe `dist/`).

## Seiten

| Datei | Inhalt |
|---|---|
| `src/pages/index.astro` | Startseite: Komponenten, Features, Selbstbau, Kaufen, FAQ, Kontakt |
| `src/pages/horizont.astro` | Demo des künstlichen Horizonts (simulierte Daten) |
| `src/pages/schaltplan.astro` | Sensorboard-Schaltplan (`public/Sensorboard.svg`) |
| `src/pages/impressum.astro` | Impressum + Haftungsausschluss |
| `src/pages/datenschutz.astro` | Datenschutzerklärung |
| `src/layouts/Layout.astro` | Gemeinsames Layout, Tag/Nacht-Theme |
| `netlify/functions/counter.mjs` | Seitenaufrufzähler (`/api/counter`, Netlify Blobs, nur Gesamtzahl) |

## Inhalte aktuell halten

- Feature-Texte und Hardware-Liste in `index.astro` bei Firmware-Änderungen mitziehen
  (Quellen: `firmware/*/README.md`, `firmware/*/memory-bank/progress.md`).
- Schaltplan-SVG nach Änderungen am KiCad-Projekt neu exportieren und an beide Stellen kopieren:

  ```sh
  kicad-cli sch export svg -o <ziel> hardware/schematics/Sensorboard/Sensorboard.kicad_sch
  # -> hardware/schematics/Sensorboard/Sensorboard.svg und website/public/Sensorboard.svg
  ```

  Revision und KiCad-Version in `schaltplan.astro` anpassen.

## Befehle

| Befehl | Aktion |
|---|---|
| `npm install` | Abhängigkeiten installieren |
| `npm run dev` | Entwicklungsserver auf `localhost:4321` |
| `npm run build` | Produktions-Build nach `./dist/` |
| `npm run preview` | Build lokal ansehen |

Der Seitenaufrufzähler läuft nur auf Netlify (bzw. mit `netlify dev`), lokal bleibt die Zeile leer.
