# Development Guidelines - WoMo Home ESP32 Project

## 🎯 **GRUNDSÄTZE - KEINE AUSNAHMEN**

### **1. FAKTEN-BASIERTE ENTWICKLUNG**
- ✅ **NUR** implementieren was durch offizielle Dokumentation belegt ist
- ✅ **NUR** Hardware-Code mit verifizierten Datenblättern/Schaltplänen
- ✅ **NUR** APIs verwenden die in offiziellen Specs dokumentiert sind
- ❌ **KEINE** Vermutungen, Annahmen oder "könnte funktionieren"
- ❌ **KEINE** Code-Generierung ohne verifizierte Basis

### **2. HARDWARE-SPEZIFIKATIONEN**
- ✅ **ERST** Datenblätter und offizielle Dokumentation beschaffen
- ✅ **ERST** Pinouts, Register-Maps und Timing-Diagramme prüfen
- ✅ **ERST** offizielle Beispiele vom Hersteller analysieren
- ❌ **NICHT** programmieren ohne Hardware-Fakten

### **3. SOFTWARE-ARCHITEKTUR**
- ✅ **ERST** APIs und Bibliotheks-Dokumentation studieren
- ✅ **ERST** Kompatibilität und Abhängigkeiten klären
- ✅ **ERST** Build-System und Toolchain verifizieren
- ❌ **NICHT** Code schreiben ohne funktionierende Toolchain

### **4. TESTING & VERIFIKATION**
- ✅ **JEDER** Code muss buildbar sein
- ✅ **JEDE** Hardware-Integration muss testbar sein
- ✅ **JEDE** Annahme muss durch Tests belegt werden
- ❌ **KEINE** ungetesteten Implementierungen

### **5. KOMMUNIKATION**
- ✅ **EHRLICH** sagen wenn Informationen fehlen
- ✅ **TRANSPARENT** über Unsicherheiten kommunizieren
- ✅ **NACHFRAGEN** bevor Annahmen gemacht werden
- ❌ **NICHT** vorgeben etwas zu wissen was unbekannt ist

## 📋 **ARBEITSABLAUF**

### **Phase 1: Recherche & Verifikation**
1. Offizielle Dokumentation beschaffen
2. Hardware-Spezifikationen sammeln
3. Software-Dependencies prüfen
4. Kompatibilität verifizieren

### **Phase 2: Planung & Design**
1. Anforderungen definieren
2. Architektur entwerfen
3. APIs und Interfaces festlegen
4. Test-Strategie entwickeln

### **Phase 3: Implementierung**
1. Build-System aufsetzen
2. Grundfunktionen implementieren
3. Schrittweise erweitern
4. Kontinuierlich testen

### **Phase 4: Integration & Test**
1. Hardware-Integration
2. End-to-End Tests
3. Dokumentation aktualisieren
4. Release vorbereiten

## ⚠️ **STOPP-KRITERIEN**

**SOFORT STOPPEN wenn:**
- Hardware-Dokumentation fehlt
- Build-System nicht funktioniert
- APIs undokumentiert sind
- Tests fehlschlagen
- Annahmen nötig werden

## 📚 **DOKUMENTATIONS-QUELLEN**

### **Für dieses Projekt erforderlich:**
- [ ] Waveshare ESP32-S3 AMOLED offizielle Dokumentation
- [ ] ESP-IDF API Referenz (aktuelle Version)
- [ ] LVGL Dokumentation (kompatible Version)
- [ ] Walter Modem Library Dokumentation
- [ ] Sensor-Datenblätter (BME280, INA226, etc.)

---

**DIESE RICHTLINIEN SIND BINDEND FÜR ALLE ENTWICKLUNGSARBEITEN**

*Erstellt: 26. Oktober 2025*  
*Status: Aktiv und bindend*