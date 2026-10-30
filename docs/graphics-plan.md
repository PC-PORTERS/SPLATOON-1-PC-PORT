# Plan: Plaza wie das realistische Referenzbild (2026-10-29)

Vergleich: unser Stand (`build/abtest/ref4_sheet.png`, 14:34 und 18:32 „nach dem Regen") gegen das realistische Bild des Nutzers (gleiche Kamerapositionen).

## 1. Was im Referenzbild anders ist

### Tag (oben)
| Merkmal | Unser Bild | Referenz |
|---|---|---|
| Farben | knallig, Comic-Sättigung (Neon-Grün am Turm) | natürlich, ca. 20–30 % weniger Sättigung, Olivgrün statt Neon |
| Tonkurve | harter Kontrast, Weiß clippt | filmisch: weiche Lichter, leicht angehobene Schatten |
| Luft | ferne Gebäude so scharf und kräftig wie nahe | Luftperspektive: Ferne heller, bläulicher, kontrastärmer |
| Licht | Schatten flach | weiche Umgebungsverdeckung in Ecken, Licht „füllt" den Raum |
| Materialien | glatte, saubere Texturen | Rost, Schmutz, Gebrauchsspuren am Turm, Beton- und Asphaltstruktur |
| Figuren | glatt wie Plastik | wirken wie echte Figuren mit Stoff und Material |
| Himmel | gesättigtes Blau, harte Wolken | heller Dunst am Horizont, weichere, natürlichere Wolken |
| Kanten | FXAA, Gitter flimmern noch | ruhige, filmische Kanten |

### Sonnenuntergang (unten)
| Merkmal | Unser Bild | Referenz |
|---|---|---|
| Luft | klar, Sonne als Scheibe | ganze Luft glüht golden (Dunst im Gegenlicht), Gebäude werden zu Silhouetten |
| Sonne | kleine Scheibe, Spielblende | überstrahlter Kern, Sternstrahlen, großer Glow, Linsen-Geister |
| Gegenlicht | Figuren normal beleuchtet | goldene Randlichter an Figuren und Kanten |
| Farbe | orange Wolken, sonst kräftige Farben | einheitlich warm-golden, entsättigt, Schatten warm aufgehellt |
| Atmosphäre | – | Staubteilchen schweben im Licht, leichte Tiefenunschärfe |

**Ehrlich:** Das Referenzbild ist KI-bearbeitet. Neue Geometrie (andere Figurenmodelle, andere Beschriftungen) lässt sich nicht 1:1 nachbauen. Licht, Luft, Farbe, Optik und Materialeindruck aber schon. Das macht den Großteil des „realistisch"-Gefühls aus.

## 2. Umsetzung, nach Wirkung sortiert

### Phase 1 – Farbwissenschaft (größte Wirkung, ca. 1 Sitzung)
1. **Grading aus dem Bildpaar lernen.** Neues Werkzeug `tools/perf/fit_lut.py`: Es legt unseren Screenshot und das Referenzbild übereinander (gleiche Kamera), passt eine 33³-3D-LUT per regularisierter Regression an und glättet sie. Eine LUT gibt es für den Tag, eine für die Goldene Stunde und eine für die Nacht. Die Endkomposition liest die LUT und blendet nach Sonnenhöhe über. Im Menü kommt die Wahl „Look: Original / Realistisch" dazu.
2. **HDR-Grading im Spiel selbst.** Das Farbkorrektur-Objekt des Spiels (`color_correction`/toycam in `plazaday.baglccr`: Sättigung, Kontrast, Helligkeit, Levels) steuere ich nach Tageszeit, analog zu den Lichtern. So passiert die Entsättigung vor dem Tonemapping des Spiels und nicht auf dem fertigen 8-Bit-Bild.
3. **Filmische Tonkurve (AgX-artig).** Weiche Schulter, leicht angehobenes Schwarz, Entsättigung in den Lichtern. Sie ersetzt die jetzige S-Kurve.

### Phase 2 – Luft und Licht (ca. 1–2 Sitzungen)
4. **Luftperspektive.** Ein Aerial-Perspective-Volumen nach Hillaire (32×32×32 Froxel aus den vorhandenen Atmosphären-Tabellen) wird auf die Szene nach Tiefe angewendet. Ferne Gebäude werden dadurch heller, bläulich und bei Sonnenuntergang golden.
5. **Goldener Gegenlicht-Dunst.** Streulicht in der Luft mit Mie-Phasenfunktion zur Sonne hin, abgeschattet durch die vorhandene Lichtstrahlen-Maske. Bei tiefer Sonne glüht die Luft, und die Gebäude werden zu Silhouetten.
6. **Randlicht über die Spiel-Lichter.** `BacksideColor` und `SpecularColor` des Hauptlichts (bisher Schwarz bzw. Weiß) treibe ich bei tiefer Sonne golden. Dazu kommt ein wärmeres Bodenlicht im Himmelslicht. Das echte Gegenlicht an Figuren entsteht dann durch die Shader des Spiels.
7. **Himmel.** Mehr Dunst am Horizont, weichere Wolken (Detail-Erosion zurücknehmen, Puderzucker-Effekt). Bei Sonnenuntergang einen überstrahlten Sonnenkern, damit der Himmel um die Sonne weißgolden wird.

### Phase 3 – Kamera und Optik (ca. 1 Sitzung)
8. **Linsensystem.** Geister-Reflexe, Halo-Ring und Sternstrahlen (Beugung an der Blende) aus der hellen Sonne im HDR-Puffer. Es ersetzt die Spielblende bei tiefer Sonne.
9. **Staub im Licht.** Wenige, sehr kleine schwebende Partikel, sichtbar nur in den Lichtstrahlen (Maske aus Schritt 5).
10. **Bloom.** Größerer Radius, energieerhaltend, abhängig von der Belichtung.
11. **Tiefenunschärfe.** Über die DOF-Parameter des Spiels (`DepthOfFieldObj0`): dezent in der Ferne, bei Sonnenuntergang stärker.
12. **TAA statt FXAA.** Zeitliche Kantenglättung mit Rückprojektion über Tiefe und Kameramatrizen, Nachbarschafts-Clamping gegen Geisterbilder, danach CAS-Schärfung. Das Flimmern an Zäunen und Drähten verschwindet, das Bild wirkt filmisch.

### Phase 4 – Materialien (größter Aufwand, mehrere Sitzungen)
13. **Umgebungsverdeckung nur aufs Umgebungslicht.** Den Licht-Puffer des Spiels (Light Pre-Pass) finden und AO dort einrechnen statt auf das fertige Bild. Dazu GTAO mit zwei Radien (Kontakt plus Raum).
14. **Gebrauchsspuren.** Weltfeste, prozedurale Verschmutzung (Triplanar-Rauschen × Krümmung × AO) auf großen statischen Flächen: Turm, Fassaden, Boden. Figuren bleiben sauber (Maske über Tiefe und Bewegung).
15. **HD-Texturpaket.** Texturen nach Hash ausgeben, hochskalieren und mit Gebrauchsspuren versehen (z. B. ESRGAN, eigene Bearbeitung), beim Laden nach Hash ersetzen. Wir fangen mit Turm, Gebäuden und Boden an. Nur so kommt echte Materialstruktur wie im Referenzbild ins Spiel.
16. **Trockene Spiegelungen.** Dezente Bildschirm-Spiegelungen mit hoher Rauheit auf Glas, Metall und Boden, auch ohne Regen.

### Phase 5 – Prüfen nach jedem Schritt
- **Werkzeug `tools/perf/compare_ref.py`:** Es vergleicht unser Bild bei Replay-Bild 1800/2340 (14:34 und 18:32) mit dem Referenzbild über Farbabstand ΔE, Histogramm und Struktur (SSIM). Jede Änderung muss den Abstand verringern.
- **FPS-Budget:** höchstens etwa 1,5 ms zusätzlich pro Phase, gemessen mit `dev analyze` vorher und nachher (Stand jetzt: 49,9 FPS mit Effekten, 50,7 ohne).
- Jeder Effekt bekommt eine Menü-Option und einen Env-Schalter zurück zum alten Verhalten.

## 3. Reihenfolge
Phase 1 → 2 → 3 bringen den Großteil des Unterschieds und sind in wenigen Sitzungen machbar. Phase 4 braucht am längsten; das Texturpaket (15) ist der einzige Weg zu echten Materialdetails. Zurückgestellt bleiben Regen, Sounds und Kino-Modus.

## Stand 2026-10-30
- Erledigt: 1.1 (gelernte Farbabstimmung per Verteilungs-Angleichung), 1.3 (Tonkurve über den Look), 2.4 (Luftperspektive), 2.5 (goldener Dunst), 2.6 (Randlicht über `BacksideColor`), 3.8 (Blendschleier, Sternstrahlen, Linsen-Geister), 3.9 (Staub im Licht), 3.10 (Bloom-Kette), 3.12 (TAA).
- Offen: 1.2 (HDR-Grading über das toycam-Objekt), 2.7 (Himmel-Dunst feiner), 3.11 (Tiefenunschärfe), Phase 4 (AO im Licht-Puffer, Gebrauchsspuren, HD-Texturpaket, trockene Spiegelungen), Phase 5 Vergleichswerkzeug.

## Später (erst wenn Tag und Sonnenuntergang wirklich gut aussehen) – Wünsche vom 2026-10-30

### Wetter und Zeit live
- Wolken frei einstellbar: Bedeckung, Höhe, Dichte, Größe, Windrichtung und -stärke (Schieberegler im ESC-Menü, wirkt sofort).
- Live-Wettersystem: Wetter wechselt von selbst nach Wahrscheinlichkeiten (klar → bewölkt → Regen → nach dem Regen), mit fließenden Übergängen; jederzeit manuell überschreibbar.
- Zeitfenster: Tageszeit frei wählbar (Uhrzeit-Regler), Tageslänge, Zeit anhalten, Zeitraffer, optional echte Uhrzeit des PCs.

### Kinomodus und Fotomodus
- UI ausblenden (HUD, Namen, Hinweise).
- Kamera: Entfernung zum Charakter, Höhe, Winkel und Sichtfeld frei einstellbar, freie Kamera (fliegen) im Fotomodus, Spiel pausiert.
- Weiche Kamerabewegung: Glättung, dezentes Handkamera-Wackeln (Stärke und Frequenz einstellbar, standardmäßig aus).
- Tiefenunschärfe mit Fokuspunkt (auf Charakter oder per Klick), Blendenzahl, Bokeh-Form.
- Echter Nebel: Volumennebel mit Höhe und Dichte, Bodennebel am Morgen, Lichtstrahlen im Nebel.
- Bild-Einstellungen: Sättigung, Kontrast, Belichtung, Weißabgleich, Vignette, Filmkorn, Farbfilter-Vorlagen.
- Screenshot in hoher Auflösung (Supersampling) aus dem Fotomodus.

### Weitere Ideen für mehr Realismus
- Höher aufgelöste Texturen (Texturpaket über Hash, Grundlage `gx2_texture_pack` steht schon).
- Bewegungsunschärfe pro Objekt, Hitzeflimmern über Asphalt am Mittag, Wolkenschatten über die Plaza.
- Regenbogen nach dem Regen, Morgentau-Glanz, Laub und Papier im Wind.
- Beleuchtete Fenster und Neon-Spiegelungen nachts, Lichtkegel von Laternen im Nebel.
- Belichtungsanpassung beim Wechsel zwischen Schatten und Sonne (Augenadaption).
- Bei vielen Wolken: viele schöne Lichtstrahlen (Crepuscular Rays) durch Wolkenlücken, volumetrisch aus der Wolkenkarte (Schattenkarte der Wolken), auch am Tag und besonders bei Sonnenauf- und -untergang.
- 2026-10-30 abends: 3.11 Tiefenunschärfe (fern, dezent) und 4.2 Gebrauchsspuren (prozedural über AO und Weltrauschen) erledigt; Schatten/Kontrast und „Filter-Look“ am Tag korrigiert (Bild war linear, doppelte Umrechnung). Offen: 4.3 Texturpaket laden (Ausgeben geht), 1.2 HDR-Grading über toycam, Sonnenuntergang noch etwas zu satt.
- 2026-10-30 nachts: 4.3 Texturpaket lädt Ersatztexturen (gleiche Größe/Format), 5 Vergleichswerkzeug `tools/perf/compare_ref.py`. Hauptplan damit bis auf echte HD-Texturen (Inhalt, nicht Technik) und 1.2 (HDR-Grading über toycam, durch die gelernte Farbabstimmung weitgehend ersetzt) erledigt.
- 2026-10-30: großer volumetrischer Kumulus hinter dem Turm; Wolkenmenge-Regler und Kumulus an/aus im Menü (erster Punkt aus „Später“).
- Nächster großer Punkt: Spielkamera (Sichtfeld etwas weiter, Entfernung zum Charakter, Höhe) – braucht das Finden der Kamera-Parameter im Spiel (wie bei den Lichtern über das AAMP-Parameter-Scanning bzw. die Schreib-Falle).
- Später (vom Nutzer gemerkt): Kamera-Wackeln (Wiggle) im Kinomodus nicht vergessen; Regler für die Wolkengeschwindigkeit.
- 2026-10-30: Sichtfeld-Regler fertig (Hook vor `setFovy`, Standard 62°, Plaza wirkt größer); zweiter großer Kumulus gegenüber.
- 2026-10-30 spät: ESC-Menü für Einsteiger umgebaut: „Grafik-Voreinstellung“ ganz oben (Original / Balanced / Realistic / Cinematic, sonst „Custom“), drei Seiten „Graphics“, „Weather & time“, „Camera & mouse“, unter jeder Zeile eine Erklärung in einfachen Worten. Neu: Wolkengeschwindigkeit (`cloud_speed`, `RBREW_CLOUD_SPEED`), Kamera-Entfernung/-Höhe (Hook auf `sead::LookAtCamera::doUpdateMatrix` 0281ADB4), freie Flugkamera F7 (Pfeile, Bild auf/ab, Ziffernblock 4/6/8/2, Shift), Fotomodus F12 (PNG ohne FPS-Anzeige und Menü nach `Screenshots\`), direktes Maus-Zielen wie in einem PC-Spiel (`mouse_direct`, `RBREW_MOUSE_DIRECT=0` = alter Stick-Weg; braucht Bewegungssteuerung „An“ im Spiel), alle GamePad-Eingaben auf Tasten (Spezialwaffe = mittlere Maustaste, Zurufe = 1–4, L = Z, linker Stick = C). Nasser Boden nach Regen war versehentlich auskommentiert, wieder an.
- Später (Wunsch 2026-10-30): **Die Plaza soll sich viel größer anfühlen.** Ideen: Kamera tiefer und näher am Boden mit etwas weiterem Sichtfeld (Größenwirkung wie aus Augenhöhe), keine Unschärfe, die wie Tilt-Shift wirkt (lässt alles klein aussehen), Luftperspektive schon auf mittlere Entfernung dezent (Gebäude am Rand blasser = weiter weg), weitere Skyline und Hochhäuser hinter der Plaza, Wolken- und Lichtmaßstab an echte Größen anpassen, Menschen/NPCs im Hintergrund kleiner wirkend durch Dunst.
- Kamera startet wieder genau wie im Spiel (55°, 100 %, Höhe 0); Sichtfeld, Entfernung und Höhe nur auf Wunsch im Menü, „Reset camera to the original“. Neue Menüseite „Tools & hacks“: Flugkamera an/aus, Foto, Kinomodus. Spawn-Hack folgt später.
- Noch offen aus „Später“: HUD im Kinomodus in allen Ansichten ausblenden (geht nur teilweise), Fotomodus mit Supersampling und Pause, Fokus-Tiefenunschärfe, Spawn-Hack, Regen-Optik, Wetter-Sounds, Octo Valley.

## Alle Wünsche im Überblick (Stand 2026-10-30, 17:40)

Alles Einstellbare liegt im ESC-Menü: Hauptseite mit „Graphics preset“, dann die Seiten Graphics, Weather & time, Camera & mouse, Cinema, photo & hacks. Unter jeder Zeile steht eine Erklärung.

Erledigt:
- Realistische Shader, Licht über die Spiel-Lichter, volumetrische Wolken, Tag/Nacht mit Sonnenauf- und -untergang, Mond und Sterne, Lichtstrahlen (stärker bei aufgelockerten Wolken), Qualitätsstufen.
- Zwei große bewegte Kumuluswolken (echtes Volumen), Wolkenmenge, Wolkengeschwindigkeit, Windrichtung.
- Wetter: klar, bewölkt, Regen-Licht, Gewitter mit Blitzen, nach dem Regen (nasser Boden), Live-Wetter; Tageszeit, Tageslänge, Zyklus an/aus, PC-Uhr.
- Bild: Farben realistisch/original, Sättigung, Kontrast, Helligkeit, Farbwärme, Vignette, Bodennebel, Glühen, Kantenglättung, Filmlook, Bewegungsunschärfe, Kontaktschatten, HD-Texturen (60 Plaza-Texturen).
- Kamera: Sichtfeld, Entfernung, Höhe (alles optional, Start = Original), „Reset camera to the original“.
- PC-Steuerung: direktes Maus-Zielen, alle GamePad-Eingaben auf Tasten.
- Kinomodus ohne HUD, Kamerawackeln nur im Kinomodus (Stärke einstellbar), Fokus-Unschärfe, Foto (F12), freie Flugkamera (F7) mit drei Flug-Orten zum Speichern und Hinfliegen.

Offen:
- Kinomodus: HUD verschwindet noch nicht in allen Ansichten.
- Fotomodus mit Pause und Supersampling (höhere Auflösung als der Bildschirm).
- Spawn-Hack (Charakter an einen Ort setzen) – braucht die Spielerposition im Spielspeicher.
- Plaza soll sich größer anfühlen (siehe oben).
- Wolken: Höhe, Dichte und Größe als Regler.
- Spiegelungen (nasser Boden, Fenster), echter Regen (Optik), Wetter-Sounds – vom Nutzer zurückgestellt.
- Octo Valley und die anderen Gebiete.
- DLSS.
- 2026-10-30: Kinomodus-HUD weg auch ohne Shader und außerhalb der Plaza; Kinomodus schaltet die freie Kamera ein (Maus schauen, WASD fliegen, Leertaste/Strg hoch/runter, Shift schnell, F7 zurück).
- 2026-10-30: Spawn-Hack fertig (drei Orte für den Charakter speichern, dort spawnen, vor der Kamera spawnen); Shader-Einstellungen im Menü als (experimental) markiert.
