# GitHub Issues und Pull Requests schließen

## Übersicht

Diese Anleitung erklärt, wie Issues, Pull Requests und Diskussionen in GitHub geschlossen werden können.

---

## Issue schließen

### Als Autor oder Collaborator

1. **Öffne das Issue** auf GitHub
2. **Scrolle nach unten** zum Kommentar-Bereich
3. **Klicke auf "Close issue"** Button unten rechts
4. Optional: Füge einen finalen Kommentar hinzu, bevor du schließt

### Mit einem Commit

Du kannst Issues automatisch schließen, indem du in deiner Commit-Nachricht spezielle Keywords verwendest:

```bash
git commit -m "Fix dashboard display issue

Fixes #42"
```

**Keywords die Issues schließen:**
- `Fixes #123`
- `Closes #123`
- `Resolves #123`
- `Fix #123`
- `Close #123`
- `Resolve #123`

Wenn der Commit in den default branch (main/master) gemergt wird, wird das Issue automatisch geschlossen.

---

## Pull Request schließen

### Als Autor

1. **Öffne den Pull Request** auf GitHub
2. **Scrolle nach unten** zum Ende der Conversation
3. **Klicke auf "Close pull request"** Button

### Als Maintainer

**Option 1: Merge und automatisch schließen**
1. Öffne den Pull Request
2. Klicke auf "Merge pull request" (wenn alle Checks grün sind)
3. Der PR wird automatisch geschlossen nach dem Merge

**Option 2: Ohne Merge schließen**
1. Öffne den Pull Request
2. Klicke auf "Close pull request"
3. Optional: Füge einen Kommentar hinzu, warum der PR nicht gemergt wird

---

## Diskussionen schließen

### Diskussion als beantwortet markieren

1. **Öffne die Diskussion**
2. **Wähle die beste Antwort** aus (wenn vorhanden)
3. **Klicke auf "Mark as answer"** bei dem hilfreichen Kommentar
4. Die Diskussion wird als "Answered" markiert

### Diskussion sperren

1. Öffne die Diskussion
2. Klicke auf "Lock conversation" in der Sidebar
3. Wähle einen Grund (optional)
4. Bestätige

---

## Dieses Projekt: Issue/PR Status

### Aktueller Stand

Dieses Repository ist auf dem Branch `copilot/list-open-hardware-projects`.

**Wenn du das aktuelle Issue/PR schließen möchtest:**

1. **Gehe zu GitHub**: https://github.com/Sharki-66/womo-home-esp32/pulls
2. **Finde deinen Pull Request** mit dem Namen "list open hardware projects" oder ähnlich
3. **Überprüfe die Änderungen** - sind sie vollständig?
4. **Wenn zufrieden**: 
   - Klicke auf "Merge pull request" um zu mergen und zu schließen
   - ODER klicke auf "Close pull request" um ohne Merge zu schließen
5. **Wenn nicht zufrieden**: 
   - Kommentiere was noch fehlt
   - Weitere Änderungen können gemacht werden

### Automatisches Schließen aktivieren

Wenn du möchtest, dass dieser PR ein Issue automatisch schließt:

1. Bearbeite die PR-Beschreibung
2. Füge hinzu: `Closes #[ISSUE_NUMBER]`
3. Zum Beispiel: `Closes #5`
4. Wenn der PR gemergt wird, wird das Issue automatisch geschlossen

---

## Best Practices

### Wann sollte man schließen?

✅ **Schließe wenn:**
- Das Problem ist gelöst
- Die Frage wurde beantwortet
- Die Änderung wurde erfolgreich gemergt
- Die Diskussion ist zu einem Abschluss gekommen
- Das Issue ist veraltet oder nicht mehr relevant

❌ **Schließe NICHT wenn:**
- Die Arbeit ist noch nicht fertig
- Tests schlagen fehl
- Es gibt noch offene Fragen
- Feedback wurde noch nicht adressiert

### Gute Praxis beim Schließen

1. **Kommentiere** warum du schließt
2. **Danke** den Beteiligten
3. **Verlinke** auf relevante Commits oder PRs
4. **Dokumentiere** die Lösung wenn hilfreich

**Beispiel:**
```
Vielen Dank für das Feedback! Die Community Hardware Plattformen 
Dokumentation ist jetzt vollständig und wurde in diesem PR 
hinzugefügt: #[PR_NUMBER]

Schließe dieses Issue als erledigt.

Fixes #[ISSUE_NUMBER]
```

---

## Weitere Ressourcen

- **GitHub Docs:** https://docs.github.com/en/issues/tracking-your-work-with-issues/closing-an-issue
- **Pull Request Guide:** https://docs.github.com/en/pull-requests/collaborating-with-pull-requests
- **Issue Management:** https://docs.github.com/en/issues

---

**Stand:** 2024-11-01  
**Version:** 1.0
