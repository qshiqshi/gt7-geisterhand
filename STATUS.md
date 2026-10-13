# Stand

Version 0.1.0 · 14.10.2026 · Einstieg für die Weiterarbeit. Die Anleitung für Nutzer steht in
[README.de.md](README.de.md) / [README.md](README.md).

## Was es ist

Ein Fenster für Mac und Windows: empfängt den Lenkwinkel aus den Fahrdaten von Gran Turismo 7
und lässt ein Lenkrad am Rechner mitdrehen. C++17, SDL 3 (Fenster, Joystick, Force Feedback,
USB-HID) und Dear ImGui, fest eingebunden – je eine Datei ohne Laufzeit.

## Fertig und wie geprüft

| Teil | Geprüft durch |
| --- | --- |
| Entschlüsselung und Paketfelder | Selbsttest gegen Pakete aus pycryptodome (`tools/gen_test_packets.py`) |
| Empfang, Suche, Wiedergabepuffer | `tools/fake_ps5.py` mit dem Mitschnitt „Deep Forest“ des Companion, stoßweise Zustellung; Zustand „Port belegt“ gesehen |
| Regelung und Wächter | Selbsttest gegen nachgebildete Lenkräder (2,1 Nm Zahnrad bis 25 Nm Direktantrieb, beide Kraftrichtungen, 60–500 Hz, harte Manöver, Randstein-Rütteln, Datenlücke, blockiertes Rad, falsche Kraftrichtung) |
| Mac-App (`dist/mac`, arm64 + x86_64) | macOS 27 auf Apple Silicon: Selbsttest, Demo, Netzweg, Bilder der Anleitung |
| Windows-EXE (`dist/windows`, mingw-w64, statisch, 5,5 MB) | Windows 11 ARM in einer VM (x64-Emulation): Selbsttest, Fenster mit Demo, Netzweg gegen die nachgebaute Konsole auf dem Mac, Defender ohne Befund |

## Nicht geprüft – das sind die Gerätetests

Bisher hat das Programm kein echtes Lenkrad gedreht und keine echte PlayStation gehört.

1. **RS50 am Mac (direkter Weg):** Erscheint es in der Liste mit „direkt“? Nimmt es die
   Kraftpakete an (macOS schickt Ausgabeberichte ohne Treiber)? Antippen beim Start, dann
   Mitlenken; fällt die Kraft bei Stopp, Taste und Abziehen weg? Stimmt der Drehbereich?
2. **Echte PlayStation:** Suche im Heimnetz, Paket „B“ während einer Wiederholung, Vorzeichen
   des Lenkwinkels (aus dem Mitschnitt: positiv = links), Winkelbereich bei Fahrten mit
   Lenkrad (der Mitschnitt stammt vom Controller und bleibt in ±180°).
3. **Windows-PC mit Lenkrad:** DirectInput über G HUB, Thrustmaster- oder Fanatec-Treiber.
4. Intel-Mac; Bau unter Windows mit MSVC; lokale-Netzwerk-Frage von macOS bei der App.

Für den ersten Lauf am RS50: Kraft auf 10–15 %, Hände weg, Leertaste bereithalten.

## Bekannte Grenzen

- macOS: nur Logitech RS50/G PRO (direkt) und die älteren Logitech-Räder über SDL; das G923
  fehlt, für Thrustmaster und Fanatec gibt es keinen Mac-Treiber.
- SDL meldet eine Achse nach dem Öffnen erst, wenn sie sich um gut 1 % bewegt hat. Das
  Antippen beim Start ruckt deshalb einmalig um etwa 6° (bei 900°) und öffnet damit die Achse.
- Der Drehbereich wird von Hand eingetragen; ausgelesen wird er nicht.
- Von den Fahrdaten wird nur der Lenkwinkel benutzt: keine Schaltlichter, keine
  Randstein-Vibration, keine nachgerechneten Lenkkräfte.
- Beide Programme sind unsigniert (Mac: nur für diesen Rechner signiert).

## Bauen und prüfen

```sh
./build.sh                                   # beide; einzeln: ./build.sh mac | windows
dist/mac/Geisterhand.app/Contents/MacOS/Geisterhand --selftest [mitschnitt.gt7r]
python3 tools/fake_ps5.py --recording mitschnitt.gt7r --burst 60 &
dist/mac/Geisterhand.app/Contents/MacOS/Geisterhand --ps5 127.0.0.1 --send-port 43739 --receive-port 43740 --wheel demo --autostart
```

Weitere Schalter für Tests und Bilder stehen am Anfang von `src/main.cpp` (`--shot`,
`--record`, `--quit-after`, `--lang`). Ein Lauf mit Testschaltern rührt die gespeicherten
Einstellungen nicht an. Die Bilder der Anleitung entstehen mit `--demo --autostart --shot …`
und `--record …` (Animation: ffmpeg, 460 px breit).

## Entscheidungen

- **Kraft über die Schnittstelle des Systems** (SDL-Haptik: DirectInput, ForceFeedback) statt
  je Hersteller ein eigenes Protokoll – so drehen Rennspiele jedes Lenkrad. Ausnahme RS50 und
  G PRO: Ihr Protokoll ist offen beschrieben (logitech-trueforce-linux-driver) und braucht
  keinen Treiber, das macht sie am Mac nutzbar.
- **Lageregelung nur mit Kraft:** Das Programm tippt das Lenkrad an und lernt die Kraftrichtung,
  tastet die Verstärkung je Lenkrad von unten heran und nimmt sie zurück, sobald es zittert.
  „Kraft“ begrenzt nur das Drehmoment und ändert die Regelung nicht.
- **Eigener Thread für das Lenkrad:** Ein hängendes Fenster lässt keine Kraft stehen.
- **Wiedergabe nach Paketnummer** mit kleinem Puffer statt nach Ankunftszeit, weil die Pakete
  im Heimnetz stoßweise kommen.
