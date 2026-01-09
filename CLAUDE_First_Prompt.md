# Projekt: wM-Bus Modul für Bruce Firmware

## Deine Aufgabe

Analysiere die Bruce Firmware Codebase und entwickle einen Lösungsvorschlag für ein wM-Bus Modul. Ich möchte deine Expertise nutzen – du kennst die Codebase besser als ich nach der Analyse.

**Bitte NICHT sofort mit dem Coden beginnen.** Erst analysieren, dann einen konkreten Vorschlag machen, den wir besprechen.

---

## Was ich erreichen möchte

### Zielgerät
LILYGO T-Embed CC1101 (ESP32-S3, CC1101, 320x170 Display, 16MB Flash, 8MB PSRAM)

### Anwendungsfall 1: Scan Mode (Mobil)
Ich fahre vor 200+ Haushalte und lese deren Wärmezähler ab. Die Daten brauche ich zur Erstellung der Abrechnung einer Biogasanlage. Die Zähler senden auf 868 MHz via Wireless M-Bus (T1/C1 Mode). Ich möchte:
- Alle empfangenen Zähler automatisch erfassen
- Daten auf dem Gerät speichern (stromsicher)
- Per Handy auf die Daten zugreifen (Web-Interface über ESP32-Hotspot)
- Am Ende CSV exportieren für die Abrechnung
- Zähler in Gruppen organisieren können
- Das Ganze läuft ca. 6 Stunden am Stück

### Anwendungsfall 2: Home Mode (Stationär)
Das Gerät steht dauerhaft zuhause und liest meinen persönlichen Wärmezähler (Diehl Sharky 774) aus. Die Daten sollen per MQTT an Home Assistant geschickt werden. Läuft 24/7.

### Moduswahl
Die Modi sollen manuell im Menü gewählt werden, keine automatische Erkennung.

---

## Technische Rahmenbedingungen

### Wärmezähler-Protokoll
- Wireless M-Bus (wM-Bus) auf 868 MHz
- T1 und C1 Mode
- Oft AES-128 verschlüsselt
- Referenz: Diehl Sharky 774 (wmbusmeters driver: "sharky")

### Daten die ein Wärmezähler liefert
- Zähler-ID
- Gesamtverbrauch (kWh)
- Vorlauftemperatur (°C)
- Rücklauftemperatur (°C)
- Durchfluss (m³/h)
- Signalstärke

### Speicheranforderungen
- Mindestens 2.000 Zählereinträge speichern können
- Stromsicher (bei Stromverlust keine/minimale Datenverluste)
- Automatisches Aufräumen wenn Speicher voll wird

### Existierende Ressourcen
- wmbusmeters Library: https://github.com/weetmuts/wmbusmeters
- ESPHome wM-Bus Component: https://github.com/SzczepanLeon/esphome-components
- Bruce nutzt bereits den CC1101 für Sub-GHz

---

## Was ich von dir brauche

### Schritt 1: Codebase analysieren
- Wie ist Bruce strukturiert?
- Wie werden Module/Features eingebunden?
- Wie nutzt Bruce den CC1101 aktuell?
- Welche UI-Komponenten gibt es?
- Wie funktioniert das Menüsystem?
- Gibt es bereits Web-Server Funktionalität?
- Wie werden Einstellungen persistent gespeichert?

### Schritt 2: Lösungsvorschlag präsentieren
Basierend auf deiner Analyse, schlage mir vor:

1. **Architektur:** Wie würdest du das Modul strukturieren?
2. **Integration:** Wo und wie in Bruce einhängen?
3. **CC1101:** Kannst du die existierende Sub-GHz Implementierung nutzen oder brauchst du was Eigenes für wM-Bus?
4. **Speicherung:** Welches Format/System empfiehlst du für die Daten?
5. **Web-Interface:** Wie würdest du das umsetzen (falls Bruce schon was hat, wiederverwenden)?
6. **Phasenplan:** In welcher Reihenfolge sollten wir das bauen?

### Schritt 3: Offene Fragen klären
Wenn dir Infos fehlen oder du Entscheidungen von mir brauchst, frag nach bevor du loslegst.

---

## Meine Wunsch-Features (Priorität)

### Must-Have
- [ ] Scan Mode: Zähler empfangen und auf Display anzeigen
- [ ] Scan Mode: Daten persistent speichern
- [ ] Scan Mode: Web-Interface zum Abrufen der Daten per Handy
- [ ] Home Mode: MQTT an Home Assistant senden
- [ ] Moduswahl über Menü

### Nice-to-Have
- [ ] CSV Export
- [ ] Zähler in Gruppen organisieren
- [ ] Home Assistant Auto-Discovery
- [ ] Pagination bei vielen Zählern
- [ ] AES-Entschlüsselung konfigurierbar

---

## Hinweise

- Ich bin kein C++ Experte, aber ich kann Code lesen und anpassen
- Das Gerät (T-Embed CC1101) habe ich bereits
- AES-Key für meinen Heimzähler muss ich noch beim Versorger anfragen
- Für die Biogasanlage-Abrechnung gibt es ~300 Haushalte, realistisch empfange ich davon vielleicht 200+ pro Tour

---

## Nächster Schritt

Bitte analysiere jetzt die Bruce Firmware Codebase und präsentiere mir deinen Lösungsvorschlag. Ich bin gespannt auf deine Einschätzung, wie wir das am besten umsetzen.
