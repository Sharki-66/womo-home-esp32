---
type: "Referenz"
title: "AGENTS"
description: "Eingebettete Betriebsanleitung für Coding-Agents zur Nutzung des generierten OpenWiki-Evidenz-Index dieses Repositories: wann Retrieval sinnvoll ist, welche Tools dafür zu verwenden sind und dass generierte Wiki-Seiten nicht von Hand bearbeitet werden sollen."
tags: [agents, openwiki, documentation, workflow, reference]
openwiki_generated: true
verified:
  - by: openwiki/0.7.2
    at: 2026-10-19T20:49:57.319Z
sources:
  - id: openwiki-source-8037e2358a2c4f9b2c722a11
    resource: repo://AGENTS.md
  - id: openwiki-source-a2371d6362e5db4bc834ad03
    resource: repo://CLAUDE.md
generated: { by: "openwiki/0.7.2", at: "2026-10-19T20:49:57.319Z" }
---

<!-- OPENWIKI:START -->

## OpenWiki

Dieses Repository verfügt über einen generierten `openwiki/`-Evidenz-Index. Er ist optionaler Kontext für den Bedarfsfall (just-in-time), keine Pflichtlektüre zu Beginn einer Aufgabe.

- Wikis zu Beginn einer Aufgabe nicht auflisten, vorab laden oder durchsuchen. Retrieval nutzen, wenn der Nutzer ausdrücklich danach fragt, wenn unbekanntes Architektur- oder Abhängigkeitsverhalten die Aufgabe wesentlich beeinflusst, oder wenn die Quellcode-Prüfung eine wichtige Unsicherheit offen lässt. Aufhören, sobald die Frage hinreichend geklärt ist.
- Wenn diese Bedingungen zutreffen und OpenWiki-Retrieval-Tools verfügbar sind, `openwiki_search` für punktuellen Kontext und `openwiki_read` für die relevanten vollständigen Abschnitte verwenden. Liefert die Suche `workspace_required`, nach dem zu verwendenden Workspace aus der angezeigten Liste fragen und mit dessen ID erneut versuchen.
- `openwiki_list_workspaces` oder `openwiki_list_wikis` verwenden, wenn die Workspace-Zugehörigkeit selbst erst ermittelt werden muss.
- Sind die Retrieval-Tools nicht verfügbar, `openwiki/quickstart.md` lesen und den dortigen Links zu den relevanten Seiten folgen.
- Quellcode und Tests als maßgeblich behandeln. Unklarheiten und Prüfpunkte eines Briefings sind Verifikationslücken, keine automatischen Anforderungen.
- Die schmalste, stille Validierung bevorzugen, die das geänderte Verhalten belegt. Vollständige Fehlerausgaben erhalten.

Der geplante OpenWiki-GitHub-Actions-Workflow aktualisiert das Repository-Wiki. Generierte OpenWiki-Seiten nicht von Hand bearbeiten, außer es wird ausdrücklich verlangt; stattdessen Quellcode/Dokumentation aktualisieren und das Wiki neu generieren lassen.

<!-- OPENWIKI:END -->
