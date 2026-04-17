# 🎸 Guitar Hero PS3 → ESP32 Mozzi Syntetisaattori

> Vanha Guitar Hero 3 PS3 -pelikitara muutettuna itsenäiseksi syntetisaattorisoittimeksi.  
> Kaikki elektroniikka kitaran sisällä — ei kaapeleita, ei konsolia, ei tietokonetta.

---

## Esittely

Tässä projektissa Guitar Hero 3 PS3 -kitaran alkuperäinen elektroniikka korvataan ESP32-mikrokontrollerilla ja Mozzi-syntetisaattorikirjastolla. Lopputuloksena on itsenäinen soitin jossa:

- Fretit soittavat sointuja tai nuotteja
- Strum laukaisee äänen
- Whammy bar taivuttaa äänenkorkeutta reaaliajassa
- Sisäinen kaiutin ja akku — täysin langaton
- USB-C lataus, noin 17 tuntia käyttöaikaa

---

## Ominaisuudet

| Ominaisuus | Kuvaus |
|---|---|
| Sointutila | 3 sointusettia: Pop (G), Rock (E), Balladi (C) |
| Nuottitila | C-duuriasteikko: C D E F G A |
| Oktaavit | Basso / Normaali / Melodia |
| Efektit | Clean, Distortion, Tremolo, Vibrato, Ring Modulator |
| Whammy bar | Portaaton pitch bend ±5% |
| Demo-tila | 2 demo-nappia, eri kappale sointu- ja nuottitilassa |
| Äänilähtö | PCM5102A I2S DAC → PAM8403 3W vahvistin |
| Virta | 2× 18650 Li-ion, USB-C lataus, ~17 h käyttöaika |
| Arkkitehtuuri | FreeRTOS dual-core: napit Core 0, Mozzi Core 1 |

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

## Demo-kappaleet

| Nappi | Sointutila | Nuottitila |
|---|---|---|
| Demo 1 (GPIO 33) | Let It Be — C G Am F | Ode to Joy |
| Demo 2 (GPIO 18) | Smoke on the Water — E G A | Seven Nation Army |

---

## Bill of Materials (BOM)

| # | Komponentti | Malli / Spesifikaatio | Määrä | Hinta (AliExpress) |
|---|---|---|---|---|
| 1 | Mikrokontrolleri | ESP32 DevKit V1 (WROOM-32) | 1 | ~2.00€ |
| 2 | I2S DAC | PCM5102A breakout board | 1 | ~2.00€ |
| 3 | Vahvistin | PAM8403 3W stereo | 1 | ~0.50€ |
| 4 | Kaiutin | 8Ω 2W 50mm | 1 | ~1.50€ |
| 5 | Akku | 18650 Li-ion 3.7V ~3000mAh | 2 | ~3.00€ |
| 6 | Akkukotelo | 2× 18650 rinnankytketty | 1 | ~1.00€ |
| 7 | Latauspiiri | TP4056 + DW01 suojapiiri | 1 | ~0.50€ |
| 8 | USB-C liitin | USB-C breakout board | 1 | ~0.30€ |
| 9 | Virtakytkin | Liukukytkin | 1 | ~0.20€ |
| 10 | Vastus | 4kΩ (TP4056 PROG) | 1 | ~0.01€ |
| 11 | Vastus | 5.1kΩ (USB-C CC) | 2 | ~0.02€ |
| 12 | Vastus | 1kΩ (PAM8403 bias) | 1 | ~0.01€ |
| | **Yhteensä** | | | **~11–14€** |

> **Huom:** Hinnat ovat arvioita AliExpressistä. Elektroniikkakaupasta ostettuna hinnat voivat olla korkeammat.

---

## Pinnijärjestys (ESP32 DevKit V1)

```
           USB
       ┌─────────┐
  3V3  │ ●     ● │ GND
  GND  │ ●     ● │ IO23 ← STRUM alas
 IO15  │ ●     ● │ IO22 ← SININEN fret
  IO2  │ ●     ● │ IO21 ← KELTAINEN fret
  IO4  │ ●     ● │ IO19 ← I2S DATA
  IO5  │ ●     ● │ IO18 ← DEMO 2
 IO16  │ ●     ● │ IO17 (D-pad oikea)
 IO17  │ ●     ● │ IO16 (D-pad vasen)
 IO18  │ ●     ● │ IO5  (D-pad alas)
 IO19  │ ●     ● │ IO4  (D-pad ylös)
 IO21  │ ●     ● │ IO2  ← ORANSSI fret
 IO22  │ ●     ● │ IO15 ← SELECT
 IO23  │ ●     ● │ GND
 IO25  │ ●     ● │ IO13 ← START
 IO26  │ ●     ● │ IO14 ← STRUM ylös
       │         │ IO27 ← PUNAINEN fret
       │         │ IO26 ← I2S BCK
       │         │ IO25 ← I2S WS
       │         │ IO33 ← DEMO 1
       │         │ IO32 ← VIHREÄ fret
       │         │ IO34 ← WHAMMY (ADC1)
       │         │ VIN  ← TP4056 OUT+
       └─────────┘
```

---

## Tiedostorakenne

```
gh_kitarasyntsa/
├── gh_synth_final.ino   — Arduino-koodi (ESP32 + Mozzi)
├── KAYTTO-OHJE.md       — Käyttöohje soittajalle
├── KYTKENTA.md          — Pinnikytkentäohje rakentajalle
└── README.md            — Tämä tiedosto
```

---

## Vaatimukset

**Laitteisto:**
- Guitar Hero 3 PS3 -kitara (tai yhteensopiva)
- Kaikki BOM-listan komponentit
- Juotoskolvi ja yleismittari
- T10 Torx -ruuvimeisseli (kitaran avaamiseen)
- Porakone (kaiuttimen reikää varten)

**Ohjelmisto:**
- [Arduino IDE 2.x](https://www.arduino.cc/en/software)
- ESP32 board package: `Boards Manager → esp32 by Espressif`
- [Mozzi](https://sensorium.github.io/Mozzi/) by Tim Barrass (Library Manager)

---

## Rakennusvaiheet

1. **Testaa ensin koekytkennällä** — ESP32 + PCM5102A + PAM8403 + kaiutin, varmista että ääni toimii ennen kitaran avaamista
2. **Avaa kitara** — ruuvit pohjassa, T10 Torx
3. **Kartoita johtimet** yleismittarilla — jokaisessa napissa signaali + maa
4. **Juota johdot** GPIO-pinneihin
5. **Asenna komponentit** runkoon
6. **Poraa reikä** kaiuttimelle (50mm) rungon pohjaan tai sivuun
7. **Asenna virtakytkin** ja **USB-C portti** kitaran sivuun
8. **Mittaa whammy-lepotila:** `Serial.println(mozziAnalogRead(34))` ja päivitä tarvittaessa

Tarkemmat ohjeet: [KYTKENTA.md](KYTKENTA.md)

---

## Nappien toiminnot

| Nappi | GPIO | Toiminto |
|---|---|---|
| Vihreä fret | 32 | Sointu/nuotti 1 |
| Punainen fret | 27 | Sointu/nuotti 2 |
| Keltainen fret | 21 | Sointu/nuotti 3 |
| Sininen fret | 22 | Sointu/nuotti 4 |
| Oranssi fret | 2 | Sointu/nuotti 5 |
| Strum ylös/alas | 14 / 23 | Laukaisee äänen |
| Start + Strum | 13 + 14/23 | Nollaa kaikki |
| Start yksin | 13 | Kierrätä sointusetti |
| Select | 15 | Sointu ↔ nuotti |
| D-pad ylös/alas | 4 / 5 | Oktaavi ylös/alas |
| D-pad vasen/oikea | 16 / 17 | Edellinen/seuraava efekti |
| Whammy | 34 | Pitch bend |
| Demo 1 | 33 | Käynnistä/pysäytä Demo 1 |
| Demo 2 | 18 | Käynnistä/pysäytä Demo 2 |

---

## Tiedossa olevat rajoitukset

- **GPIO 34–39** ovat input-only pinnejä ilman sisäistä pull-up vastusta — whammy (GPIO 34) toimii koska potentiometri hoitaa jännitteenjaon, mutta näitä ei voi käyttää napeille ilman ulkoisia vastuksia
- **PCM5102A** käyttää GPIO 25 ja 26 I2S-väylään — nämä pinnit eivät ole vapaana muuhun käyttöön
- **WiFi ja Bluetooth** on kytketty pois päältä koodissa — laitetta ei voi käyttää langattomasti muiden laitteiden kanssa

---

## Lisenssi

MIT License — tee vapaasti mitä haluat, mainitse alkuperä.

---

## Tekijä

[@aotiot](https://github.com/aotiot)
