# Guitar Hero PS3 → ESP32 Mozzi Syntetisaattori

Muutetaan Guitar Hero 3 PS3 -kitara itsenäiseksi syntetisaattorisoittimeksi. Kaikki elektroniikka mahtuu kitaran rungon sisään — ei kaapeleita, ei konsolia, ei tietokonetta. Ääni lähtee sisäisestä kaiuttimesta ja laite toimii ladattavilla akuilla noin 17 tuntia.

---

## Ominaisuudet

- **Sointutila** — kolme sointusettia (Pop, Rock, Balladi), jokainen fretti soittaa kolmisoinnun
- **Nuottitila** — C-duuriasteikko (C D E F G A) pentatonisessa järjestyksessä
- **Kolme oktaavia** — basso, normaali, melodia
- **Viisi efektiä** — Clean, Distortion, Tremolo, Vibrato, Ring Modulator
- **Whammy bar** — portaaton pitch bend reaaliajassa
- **Demo-tila** — kolme automaattisesti soivaa kappaletta (sointutila + nuottitila)
- **USB-C lataus** — 2× 18650 Li-ion akku, noin 17 h käyttöaika
- **Dual-core** — FreeRTOS, napit Core 0:lla, Mozzi-ääni Core 1:llä

---

## Laitteisto

| Komponentti | Malli | Hinta |
|---|---|---|
| Mikrokontrolleri | ESP32 DevKit V1 | ~2€ |
| DAC | PCM5102A I2S | ~2€ |
| Vahvistin | PAM8403 3W | ~0.50€ |
| Kaiutin | 8Ω 2W 50mm | ~1–2€ |
| Akut | 2× 18650 Li-ion | ~4–6€ |
| Latauspiiri | TP4056 + DW01 | ~0.50€ |
| USB-C liitäntä | USB-C breakout | ~0.30€ |
| Virtakytkin | Liukukytkin | ~0.20€ |
| Vastukset | 4kΩ, 5.1kΩ×2, 1kΩ | ~0.10€ |
| **Yhteensä** | | **~11–14€** |

---

## Kirjastot

Asenna Arduino Library Managerista:

- [Mozzi](https://sensorium.github.io/Mozzi/) by Tim Barrass

---

## Tiedostot

```
gh_synth/
├── gh_synth_final.ino   — Pääkoodi
├── KAYTTO-OHJE.md       — Käyttöohje soittajalle
├── KYTKENTA.md          — Pinnikytkentäohje rakentajalle
└── README.md            — Tämä tiedosto
```

---

## Nappien toiminnot

| Nappi | Toiminto |
|---|---|
| Fretit (5 kpl) | Sointu tai nuotti tilan mukaan |
| Strum ylös / alas | Laukaisee äänen |
| Start + Strum | Nollaa kaikki asetukset |
| Start yksin | Kierrätä sointusetti (Pop → Rock → Balladi) |
| Select | Vaihda sointu ↔ nuotti -tila |
| D-pad ylös / alas | Oktaavi ylös / alas |
| D-pad vasen / oikea | Edellinen / seuraava efekti |
| Whammy bar | Pitch bend |
| Demo 1 / 2 / 3 | Käynnistä tai pysäytä demo |

---

## Sointusetit

Sama nappi soittaa aina saman harmonisen funktion — vain perusääni vaihtuu setin mukaan.

| Nappi | Funktio | Pop (G) | Rock (E) | Balladi (C) |
|---|---|---|---|---|
| Strum (avoin) | I | G | E | C |
| Vihreä | IV | C | A | F |
| Punainen | V | D | B | G |
| Keltainen | vi | Em | C#m | Am |
| Sininen | ii | Am | F#m | Dm |
| Oranssi | bVII | F | D | Bb |

---

## Pinnijärjestys (ESP32 DevKit V1)

```
VASEN REUNA                    OIKEA REUNA
3V3  → PCM5102A VCC            VIN  → TP4056 OUT+
GND  → Yhteinen maa            GND  → TP4056 OUT-
IO15 → SELECT                  IO13 → START
IO2  → ORANSSI fret            IO12 → DEMO 1
IO4  → D-pad ylös              IO14 → STRUM ylös
IO5  → D-pad alas              IO16 → D-pad vasen
IO18 → DEMO 3                  IO17 → D-pad oikea
IO19 → I2S DATA (Mozzi)        IO32 → VIHREÄ fret
IO21 → KELTAINEN fret          IO33 → DEMO 2
IO22 → SININEN fret            IO34 → WHAMMY (ADC1)
IO23 → STRUM alas              IO27 → PUNAINEN fret
IO25 → I2S WS   (Mozzi)
IO26 → I2S BCK  (Mozzi)
```

Tarkemmat kytkentätiedot: [KYTKENTA.md]([KYTKENTA.md](https://github.com/aotiot/gh_kitarasyntsa/blob/main/kytkenta.md))

---

## Demo-kappaleet

| Nappi | Sointutila | Nuottitila |
|---|---|---|
| Demo 1 | Let It Be — C G Am F | Ode to Joy |
| Demo 2 | Smoke on the Water — E G A | Seven Nation Army |
| Demo 3 | House of the Rising Sun — Am C D F | Twinkle Twinkle |

---

## Rakennusvaiheet lyhyesti

1. Testaa ESP32 + PCM5102A + PAM8403 + kaiutin koekytkennällä
2. Avaa kitara (T10 Torx, ruuvit pohjassa)
3. Kartoita fret-nappien johtimet yleismittarilla
4. Juota johdot GPIO-pinneihin
5. Asenna komponentit runkoon, poraa reikä kaiuttimelle
6. Asenna virtakytkin ja USB-C latausportti sivuun
7. Mittaa whammy-lepotila: `Serial.println(mozziAnalogRead(34))`

Tarkemmat ohjeet: kytkenta.md tiedostossa

---

## Lisenssi

MIT — tee vapaasti mitä haluat.
