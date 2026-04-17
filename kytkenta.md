# KYTKENTA.md — Guitar Hero PS3 → ESP32 Syntetisaattori

---

## ESP32 DevKit V1 — pinnikartta

```
           USB
       ┌─────────┐
  3V3  │ ●     ● │ GND
  GND  │ ●     ● │ IO23 ← STRUM alas
 IO15  │ ●     ● │ IO22 ← SININEN fret
  IO2  │ ●     ● │ IO21 ← KELTAINEN fret
  IO4  │ ●     ● │ IO19 ← I2S DATA (Mozzi)
  IO5  │ ●     ● │ IO18 ← DEMO 2
 IO16  │ ●     ● │ IO17 (D-pad oikea)
 IO17  │ ●     ● │ IO16 (D-pad vasen)
 IO18  │ ●     ● │ IO5  (D-pad alas)
 IO19  │ ●     ● │ IO4  (D-pad ylös)
 IO21  │ ●     ● │ IO2  ← ORANSSI fret
 IO22  │ ●     ● │ IO15 ← SELECT
 IO23  │ ●     ● │ GND
 IO25  │ ●     ● │ IO13 ← START
 IO26  │ ●     ● │ IO12   (vapaa, vältä strapping)
       │         │ IO14 ← STRUM ylös
       │ vasen   │ IO27 ← PUNAINEN fret
       │ reuna   │ IO26 ← I2S BCK (Mozzi)
       │         │ IO25 ← I2S WS  (Mozzi)
       │         │ IO33 ← DEMO 1
       │         │ IO32 ← VIHREÄ fret
       │         │ IO35   (vapaa, input-only)
       │         │ IO34 ← WHAMMY BAR (ADC1)
       │         │ IO39   (vapaa, input-only)
       │         │ IO36   (vapaa, input-only)
       │         │ GND
       │         │ VIN  ← TP4056 OUT+
       └─────────┘
        oikea reuna
```

---

## Ryhmä A — Fretit

Johtimet kulkevat kitaran kaulasta runkoon. Kaikki normaaleja GPIO-pinnejä — sisäinen pull-up toimii.

| Nappi | GPIO | Kytkentä |
|---|---|---|
| Vihreä fret | GPIO 32 | GPIO → nappi → GND |
| Punainen fret | GPIO 27 | GPIO → nappi → GND |
| Keltainen fret | GPIO 21 | GPIO → nappi → GND |
| Sininen fret | GPIO 22 | GPIO → nappi → GND |
| Oranssi fret | GPIO 2 | GPIO → nappi → GND |

> GPIO 2 on strapping-pinni. INPUT_PULLUP pitää sen HIGH käynnistyksessä — nappikytkentä on turvallinen.

---

## Ryhmä B — Strum ja ohjainnapit

Johtimet strummilevyn alta runkoon.

| Nappi | GPIO | Kytkentä |
|---|---|---|
| Strum ylös | GPIO 14 | GPIO → nappi → GND |
| Strum alas | GPIO 23 | GPIO → nappi → GND |
| Select | GPIO 15 | GPIO → nappi → GND |
| Start | GPIO 13 | GPIO → nappi → GND |

> Start + Strum yhtä aikaa nollaa kaikki asetukset.

---

## Ryhmä C — D-pad

Neljä erillistä nappia strummilevyn alla.

| Suunta | GPIO | Toiminto |
|---|---|---|
| Ylös | GPIO 4 | Oktaavi ylös |
| Alas | GPIO 5 | Oktaavi alas |
| Vasen | GPIO 16 | Edellinen efekti |
| Oikea | GPIO 17 | Seuraava efekti |

Kytkentä: `GPIO → nappi → GND` (INPUT_PULLUP)

---

## Ryhmä D — Demo-napit

Kaksi nappia kitaran rungossa. Molemmat normaaleja GPIO-pinnejä — sisäinen pull-up, ei ulkoisia vastuksia.

| Nappi | GPIO | Kytkentä |
|---|---|---|
| Demo 1 | GPIO 33 | GPIO → nappi → GND |
| Demo 2 | GPIO 18 | GPIO → nappi → GND |

Kytkentä: `GPIO → nappi → GND` (INPUT_PULLUP)

---

## Ryhmä E — Whammy bar

Potentiometri strummilevyn alla. GPIO 34 on ADC1-kanava (input-only) — potentiometri hoitaa jännitteenjaon, pull-up vastusta ei tarvita.

```
3.3V ────── potentiometrin pää 1
GPIO 34 ─── potentiometrin keskipiste (wiper)
GND ─────── potentiometrin pää 2
```

---

## Ryhmä F — I2S DAC (PCM5102A)

Äänilähtö. Pinnit varattuja Mozzille — älä käytä muuhun.

| PCM5102A | GPIO | Signaali |
|---|---|---|
| BCK | GPIO 26 | Bitti-kello |
| LCK / WS | GPIO 25 | L/R-kello |
| DIN | GPIO 19 | Audiodata |
| VCC | 3.3V | Käyttöjännite |
| GND | GND | Maa |
| LOUT | PAM8403 L_IN+ | Äänilähtö |
| ROUT | PAM8403 R_IN+ | Äänilähtö |

---

## Ryhmä G — Äänentoisto (PAM8403)

| PAM8403 | Kytkentä |
|---|---|
| L_IN+ | PCM5102A LOUT → 1kΩ → L_IN+ |
| L_IN− | GND |
| VCC | Virtakytkin OUT+ |
| GND | GND |
| OUT+ | Kaiutin + |
| OUT− | Kaiutin − |

---

## Ryhmä H — Virta ja lataus

```
USB-C CC1  → 5.1kΩ → GND    (pakollinen USB-C tunnistus)
USB-C CC2  → 5.1kΩ → GND
USB-C VBUS → TP4056 IN+
USB-C GND  → TP4056 IN−

Akku 1 (+) ──┬── TP4056 BAT+
Akku 2 (+) ──┘
Akku 1 (−) ──┬── TP4056 BAT−
Akku 2 (−) ──┘

TP4056 PROG → 4kΩ → GND    (latausvirta 500mA)
TP4056 OUT+ → Virtakytkin → ESP32 VIN
                           → PAM8403 VCC
TP4056 OUT− → GND
```

> Käytä TP4056-moduulia jossa on yhdistetty suojapiiri (DW01+FS8205A).

---

## Vastusluettelo

| Käyttökohde | Arvo | Määrä | Pakollinen |
|---|---|---|---|
| TP4056 latausvirta (PROG) | 4kΩ | 1 | Kyllä |
| USB-C CC1 ja CC2 | 5.1kΩ | 2 | Kyllä |
| PAM8403 L_IN+ bias | 1kΩ | 1 | Suositeltava |
| **Yhteensä** | | **4 kpl** | |

Kaikki napit käyttävät ESP32:n sisäistä pull-up vastusta — ei ulkoisia nappi-vastuksia tarvita.

---

## Käyttämättömät pinnit

| GPIO | Tyyppi | Huomio |
|---|---|---|
| GPIO 12 | Normaali | Vapaa — strapping, vältä jos mahdollista |
| GPIO 34, 35 | Input-only | Vapaa — vaatisi ulkoisen pull-upin napeille |
| GPIO 36, 39 | Input-only | Vapaa — vaatisi ulkoisen pull-upin napeille |

---

## Pinnirajoitukset — muistilista

| GPIO | Rajoitus |
|---|---|
| 6–11 | Flash-muisti — EI koskaan käytä |
| 1, 3 | USB Serial — varattava ohjelmointia varten |
| 25, 26, 19 | I2S Mozzi — varattu |
| 34, 35, 36, 39 | Vain input — ei sisäistä pull-up vastusta |
| 0, 2, 12, 15 | Strapping — toimivat INPUT_PULLUP:lla |
