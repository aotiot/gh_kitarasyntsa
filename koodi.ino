// kääntyy idessä, 20.4.26 aot

// =============================================================================
// GUITAR HERO PS3 → ESP32 WROOM MOZZI SYNTETISAATTORI
// =============================================================================
//
// YLEISKUVAUS:
//   Guitar Hero PS3 -ohjaimen napit muutetaan syntetisaattoriksi.
//   Frettien ja strumin yhdistelmä soittaa sointuja tai yksittäisiä nuotteja
//   Mozzi-äänikirjastolla. Ääni ohjataan I2S-väylän kautta PCM5102A DAC:lle.
//
// ARKKITEHTUURI (kaksiydininen ESP32 WROOM):
//   Core 0 — buttonTask (FreeRTOS):
//             Lukee GPIO-pinnien tilan 10ms välein ja asettaa volatile-flagit.
//   Core 1 — Mozzi (updateControl + updateAudio):
//             Käsittelee flagit, päivittää taajuudet ja tuottaa äänen.
//   Corejen välinen kommunikointi: volatile-flagit (triggerFlags, releaseFlags,
//   muteOpenFlag, doReset, demoMode jne.) — yksittäiset bool/int-kirjoitukset
//   ovat atomisia ESP32:lla, erillistä lukitusta ei tarvita.
//
// LAITTEISTO:
//   - ESP32 WROOM DevKit V1
//   - PCM5102A I2S DAC + PAM8403 vahvistin + kaiutin
//   - Guitar Hero PS3 -ohjain (napit irrotettu johdoille)
//   - Whammy-potentiometri (alkuperäinen tai korvaava 10kΩ)
//
// KIRJASTOT (asenna Arduino Library Managerista):
//   - Mozzi by Tim Barrass (versio 1.x)
//   Board manager: Espressif ESP32 (Tools → Boards → Boards Manager)
//
// =============================================================================
// KYTKENTÄ — ESP32 WROOM DevKit V1
// =============================================================================
//
// PCM5102A I2S DAC:
//   GPIO26 (I2S BCK)  → PCM5102A BCK
//   GPIO25 (I2S WS)   → PCM5102A LCK
//   GPIO19 (I2S DATA) → PCM5102A DIN
//   3.3V              → PCM5102A VIN
//   GND               → PCM5102A GND
//   3.3V              → PCM5102A XSMT  ← PAKOLLINEN! Kelluvana = mute.
//   GND               → PCM5102A FLT, DMP, SCL, FMT
//
// Napit (kaikki INPUT_PULLUP — nappi painettu = LOW):
//   GPIO32 → VIHREÄ fret
//   GPIO27 → PUNAINEN fret
//   GPIO21 → KELTAINEN fret
//   GPIO22 → SININEN fret
//   GPIO2  → ORANSSI fret       (strapping-pinni, INPUT_PULLUP OK)
//   GPIO14 → STRUM ylös
//   GPIO23 → STRUM alas
//   GPIO15 → SELECT             (strapping-pinni, INPUT_PULLUP OK)
//   GPIO13 → START
//   GPIO4  → D-pad ylös
//   GPIO5  → D-pad alas
//   GPIO16 → D-pad vasen
//   GPIO17 → D-pad oikea
//   GPIO33 → DEMO 1
//   GPIO18 → DEMO 2
//
// Whammy-potentiometri (analoginen):
//   3.3V              → Potentiometrin toinen pää
//   GND               → Potentiometrin toinen pää
//   GPIO34 (ADC1)     → Potentiometrin liukupiste (keskipiste)
//   HUOM: GPIO34 on input-only eikä siinä ole sisäistä pull-upia, mutta
//   potentiometri kytkettynä 3.3V:n ja GND:n väliin pitää jännitteen
//   aina välillä 0–3.3V — ulkoista pull-up-vastusta ei tarvita.
//
// Käytössä olevat pinnit yhteensä: 15 digitaalista + 1 analoginen + 3 I2S
// Vapaat pinnit (käytettävissä laajennuksille): 12, 35, 36, 39
//
// Vältä näitä pinnejä:
//   GPIO 6–11  = Flash-muisti (sisäinen, EI KOSKAAN käytä)
//   GPIO 1, 3  = USB Serial TX/RX (varattuna ohjelmointia varten)
//
// =============================================================================
// PELIMEKANIIKKA
// =============================================================================
//
// SOINTUTILA (oletus, SELECT vaihtaa):
//   Strum ilman frettiä   → avoin I-asteen sointu aktiivisesta setistä
//   Fretti + strum        → kyseisen fretin sointu (IV / V / vi / ii / bVII)
//   Fretti vapautetaan    → sointu siirtyy ADSR release-vaiheeseen
//   Avoin sointu sammuu automaattisesti OPEN_CHORD_TIMEOUT ms kuluttua
//
// NUOTTITILA (SELECT vaihtaa):
//   Strum ilman frettiä   → avoin C aktiivisessa oktaavissa
//   Fretti + strum        → yksittäinen nuotti: D E F G A
//   D-pad ylös/alas       → oktaavi: Basso (D3–A3) / Normaali (D4–A4) / Melodia (D5–A5)
//
// EFEKTIT (D-pad vasen/oikea kierrättää):
//   0 Clean       — puhdas siniaalto
//   1 Distortion  — amplitudileikkaus (kovakoodattu ±127)
//   2 Tremolo     — amplitudin modulaatio LFO:lla (4 Hz)
//   3 Vibrato     — taajuuden modulaatio LFO:lla (6 Hz, ±1.5%)
//   4 Ring Mod    — kerrotaan 110 Hz kantoaallolla
//
// WHAMMY:
//   Potentiometri taivuttaa kaikkien aktiivisten äänten taajuutta
//   ylöspäin välillä 0–5% (WHAMMY_MAX_BEND). Lepo-asento = 0V = ei taivutusta.
//
// SOINTUSETIT (START kierrättää):
//   0 Pop     — G-duuri: avoin G, fretit C D Em Am F
//   1 Rock    — E-duuri: avoin E, fretit A B C#m F#m D
//   2 Balladi — C-duuri: avoin C, fretit F G Am Dm Bb
//
// DEMO:
//   DEMO1-nappi → Let It Be / Ode to Joy (sointu-/nuottitila)
//   DEMO2-nappi → Smoke on the Water / Seven Nation Army
//   Sama nappi uudelleen pysäyttää demon.
//   START + STRUM → nollaa kaikki oletusarvoihin.
//
// =============================================================================
// ÄÄNISYNTEESIN RAKENNE
// =============================================================================
//
// 18 siniaaltoosillaattoria (SIN2048-taulukko, 2048 näytettä):
//   osc[0..2]   = fretti 0 VIHREÄ   — 3 osasäveltä kolmisoinnulle
//   osc[3..5]   = fretti 1 PUNAINEN
//   osc[6..8]   = fretti 2 KELTAINEN
//   osc[9..11]  = fretti 3 SININEN
//   osc[12..14] = fretti 4 ORANSSI
//   osc[15..17] = avoin sointu/nuotti (erillinen ryhmä, OPEN_OSC_BASE=15)
//
// Nuottitilassa vain osc[fret*3] on aktiivinen, osc[fret*3+1] ja [+2]
// asetetaan 0 Hz:iin eikä niitä kuulu (ADSR ei ole noteOn-tilassa).
//
// ADSR per osillaattori:
//   noteOn()  → attack (5ms) → decay (80ms) → sustain (taso 180/255)
//   noteOff() → release (400ms) → hiljaisuus
//   Sustain-vaihe kestää niin kauan kuin fretti on pohjassa tai
//   OPEN_CHORD_TIMEOUT (2000ms) avoimella soinnulla.
//
// Miksaus updateAudio():ssa:
//   Summa / AUDIO_SCALER → efekti → MonoOutput::from8Bit()
//   AUDIO_SCALER (20) estää ylivuodon kun useita ääniä soi samanaikaisesti.
//
// =============================================================================

// I2S DAC -pinnit — PAKKO määritellä ennen Mozzi-includeja
#define MOZZI_AUDIO_MODE    MOZZI_OUTPUT_I2S_DAC
#define MOZZI_I2S_PIN_BCK   26
#define MOZZI_I2S_PIN_WS    25
#define MOZZI_I2S_PIN_DATA  19

// =============================================================================
// SÄÄDETTÄVÄT ASETUKSET
// =============================================================================

#define AUDIO_SCALER        20    // Miksausskaala ylivuodon estoon. Suurempi = hiljaisempi. Alue: 15–30.
#define CONTROL_RATE        128   // Mozzi updateControl()-kutsujen määrä sekunnissa. Käytä 2:n potensseja.

// ADSR-verhokäyrä — kaikille osillaattoreille sama
#define ENV_ATTACK_MS         5   // Nousuaika (ms)
#define ENV_DECAY_MS         80   // Laskuaika attack-huipusta sustain-tasolle (ms)
#define ENV_SUSTAIN_MS      150   // Maksimi sustain-aika (ms) — käytännössä fretti rajoittaa
#define ENV_RELEASE_MS      400   // Sammumisaika noteOff():n jälkeen (ms)
#define ENV_ATTACK_LEVEL    255   // Attack-huipputaso (0–255)
#define ENV_DECAY_LEVEL     180   // Sustain-taso (0–255)

#define OPEN_CHORD_TIMEOUT  2000  // Avoin sointu/nuotti sammuu automaattisesti tämän jälkeen (ms)

// Efektiasetukset
#define DISTORTION_GAIN       6   // Distortion-vahvistus ennen leikkausta. Alue: 3–10.
#define TREMOLO_RATE_HZ     4.0f  // Tremolo LFO:n nopeus (Hz)
#define VIBRATO_RATE_HZ     6.0f  // Vibrato LFO:n nopeus (Hz)
#define VIBRATO_DEPTH       0.015f // Vibrato-syvyys suhteellisena taajuuspoikkeamana (1.5%)
#define RING_MOD_FREQ_HZ    110.0f // Ring Mod kantoaallon taajuus — A2, luo metallinen sointi

// Whammy
#define WHAMMY_MAX_BEND     0.05f  // Maksimi taajuustaivutus ylöspäin (5%). Alue: 0.01–0.15.

// Demo-ajoitukset
#define DEMO_CHORD_MS       600   // Yhden soinnun kesto demossa (ms)
#define DEMO_NOTE_MS        300   // Yhden nuotin kesto demossa (ms)
#define DEMO_GAP_MS          80   // Hiljainen tauko nuottien välissä demossa (ms)

// FreeRTOS
#define BUTTON_TASK_STACK   4096  // buttonTask-pinon koko tavuina (4KB riittää hyvin)
#define BUTTON_POLL_MS        10  // Nappien pollausväli (ms). Alle 20ms = ihmiselle huomaamaton viive.

// =============================================================================
// PINNIMÄÄRITYKSET
// =============================================================================

// Fretit — normaalit GPIO:t, sisäinen pull-up toimii
#define PIN_GREEN           32
#define PIN_RED             27
#define PIN_YELLOW          21
#define PIN_BLUE            22
#define PIN_ORANGE           2    // Strapping-pinni — INPUT_PULLUP pitää HIGH käynnistyksessä, OK

// Strum ja ohjainnapit
#define PIN_STRUM_U         14    // Strum ylöspäin
#define PIN_STRUM_D         23    // Strum alaspäin
#define PIN_SELECT          15    // Vaihda sointu-/nuottitila. Strapping-pinni, INPUT_PULLUP OK.
#define PIN_START           13    // Kierrätä sointusetti / START+STRUM = reset

// D-pad
#define PIN_JOY_UP           4    // Oktaavi ylös
#define PIN_JOY_DOWN         5    // Oktaavi alas
#define PIN_JOY_LEFT        16    // Edellinen efekti
#define PIN_JOY_RIGHT       17    // Seuraava efekti

// Demo-napit
#define PIN_DEMO1           33    // Demo 1: Let It Be / Ode to Joy
#define PIN_DEMO2           18    // Demo 2: Smoke on the Water / Seven Nation Army

// Whammy-potentiometri: kytketään 3.3V:n ja GND:n väliin,
// liukupiste GPIO34:ään. GPIO34 on input-only ADC-pinni — ei
// sisäistä pull-upia, mutta potentiometri hoitaa jännitteen
// ankkuroinnin (0–3.3V). Ulkoista vastusta ei tarvita.
#define PIN_WHAMMY          34

// =============================================================================
// KIRJASTOT
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <MozziGuts.h>
#include <Oscil.h>
#include <ADSR.h>
#include <tables/sin2048_int8.h>

// =============================================================================
// TAAJUUSTAULUKOT
// =============================================================================

#define NUM_SETS       3   // Sointusetit
#define NUM_FRETS      5   // Frettinappeja
#define NUM_OCT        3   // Oktaavitasoja
#define NUM_FX         5   // Efektejä
#define NUM_OSC       18   // Osillaattoreita yhteensä (5 frettiä × 3 + 3 avointa)
#define OPEN_OSC_BASE 15   // Avoimen soinnun osillaattorien alkuindeksi

// Avoin I-asteen kolmisointu per setti (soitetaan ilman frettiä)
const float OPEN_CHORD[NUM_SETS][3] = {
   { 196.00f, 246.94f, 293.66f },  // Pop:     G3 B3 D4
   { 164.81f, 207.65f, 246.94f },  // Rock:    E3 G#3 B3
   { 130.81f, 164.81f, 196.00f },  // Balladi: C3 E3 G3
};

// Frettisoinnut: IV, V, vi, ii, bVII — klassinen pop/rock-harmonia
const float CHORDS[NUM_SETS][NUM_FRETS][3] = {
   {  // Pop — G-duuri
      { 130.81f, 164.81f, 196.00f },  // C  IV   C3 E3 G3
      { 146.83f, 185.00f, 220.00f },  // D  V    D3 F#3 A3
      { 164.81f, 196.00f, 246.94f },  // Em vi   E3 G3 B3
      { 220.00f, 261.63f, 329.63f },  // Am ii   A3 C4 E4
      { 174.61f, 220.00f, 261.63f },  // F  bVII F3 A3 C4
   },
   {  // Rock — E-duuri
      { 110.00f, 138.59f, 164.81f },  // A   IV   A2 C#3 E3
      { 123.47f, 155.56f, 185.00f },  // B   V    B2 D#3 F#3
      { 138.59f, 164.81f, 207.65f },  // C#m vi   C#3 E3 G#3
      { 185.00f, 220.00f, 277.18f },  // F#m ii   F#3 A3 C#4
      { 146.83f, 185.00f, 220.00f },  // D   bVII D3 F#3 A3
   },
   {  // Balladi — C-duuri
      { 174.61f, 220.00f, 261.63f },  // F  IV   F3 A3 C4
      { 196.00f, 246.94f, 293.66f },  // G  V    G3 B3 D4
      { 220.00f, 261.63f, 329.63f },  // Am vi   A3 C4 E4
      { 146.83f, 174.61f, 220.00f },  // Dm ii   D3 F3 A3
      { 116.54f, 146.83f, 174.61f },  // Bb bVII Bb2 D3 F3
   },
};

// Nuottitila: D-duuriasteikko D E F G A kolmessa oktaavissa
// Fretti-järjestys: Vihreä=D, Punainen=E, Keltainen=F, Sininen=G, Oranssi=A
const float NOTES[NUM_OCT][NUM_FRETS] = {
   { 146.83f, 164.81f, 174.61f, 196.00f, 220.00f },  // Basso:    D3 E3 F3 G3 A3
   { 293.66f, 329.63f, 349.23f, 392.00f, 440.00f },  // Normaali: D4 E4 F4 G4 A4
   { 587.33f, 659.25f, 698.46f, 784.00f, 880.00f },  // Melodia:  D5 E5 F5 G5 A5
};

// Avoin C per oktaavi — strumataan ilman frettiä nuottitilassa
const float OPEN_NOTE[NUM_OCT] = {
   130.81f,  // C3
   261.63f,  // C4
   523.25f,  // C5
};

// Nimet sarjamonitoritulosteisiin
const char* SET_NAMES[NUM_SETS] = { "Pop", "Rock", "Balladi" };
const char* OCT_NAMES[NUM_OCT]  = { "Basso", "Normaali", "Melodia" };
const char* FX_NAMES[NUM_FX]    = { "Clean", "Distortion", "Tremolo", "Vibrato", "Ring Mod" };

// =============================================================================
// DEMO-KAPPALEET
// =============================================================================
//
// Sointutila:  Demo 1 = Let It Be (Beatles),         Demo 2 = Smoke on the Water (Deep Purple)
// Nuottitila:  Demo 1 = Ode to Joy (Beethoven),      Demo 2 = Seven Nation Army (White Stripes)
//
// Sointunumerot: -1 = avoin sointu (I), 0–4 = fretti-indeksi (IV V vi ii bVII)

// --- Let It Be — Pop-setti (G-duuri) ---
// C=fretti0(IV), G=avoin(-1,I), Am=fretti2(vi), F=fretti4(bVII)
// Rakenne: Verse×2 + Chorus + Outro = 32 sointua × 600ms ≈ 32s
const int D1_CHORDS[] = {
   // Verse 1:  C  G  Am F   C  G  F  F
                0, -1,  2,  4,  0, -1,  4,  4,
   // Verse 2:  C  G  Am F   C  G  F  F
                0, -1,  2,  4,  0, -1,  4,  4,
   // Chorus:   Am F  C  G   Am F  G  G
                2,  4,  0, -1,  2,  4, -1, -1,
   // Outro:    C  G  Am F   C  G  C  C
                0, -1,  2,  4,  0, -1,  0,  0,
};
const int D1_CHORDS_N = 32;

// --- Ode to Joy — kaksi säkeistöä, viimeinen tahti variaatiolla ---
const float D1_NOTES[] = {
   // Säkeistö 1
   329.63f, 329.63f, 349.23f, 392.00f,  // E4 E4 F4 G4
   392.00f, 349.23f, 329.63f, 293.66f,  // G4 F4 E4 D4
   261.63f, 261.63f, 293.66f, 329.63f,  // C4 C4 D4 E4
   329.63f, 293.66f, 293.66f,           // E4 D4 D4
   // Säkeistö 2
   329.63f, 329.63f, 349.23f, 392.00f,  // E4 E4 F4 G4
   392.00f, 349.23f, 329.63f, 293.66f,  // G4 F4 E4 D4
   261.63f, 261.63f, 293.66f, 329.63f,  // C4 C4 D4 E4
   329.63f, 293.66f, 329.63f, 293.66f,  // E4 D4 E4 D4 (variaatio)
   261.63f, 261.63f,                    // C4 C4 (lopetuskadenssi)
};
const int D1_NOTES_N = 32;

// --- Smoke on the Water — Rock-setti (E-duuri) ---
// E=avoin(-1,I), G=fretti2(vi), A=fretti0(IV), B=fretti1(V)
// Pääriffi × 3 = 36 sointua × 600ms ≈ 36s
const int D2_CHORDS[] = {
   // Riffi: E G A  E G B A  E G A  G E
   -1,  2,  0,  -1,  2,  1,  0,  -1,  2,  0,  2, -1,
   -1,  2,  0,  -1,  2,  1,  0,  -1,  2,  0,  2, -1,
   -1,  2,  0,  -1,  2,  1,  0,  -1,  2,  0,  2, -1,
};
const int D2_CHORDS_N = 36;

// --- Seven Nation Army — pääriffi × 2 + bridge + paluu ---
const float D2_NOTES[] = {
   // Pääriffi ×2
   164.81f, 164.81f, 196.00f, 164.81f, 146.83f, 130.81f, 123.47f,  // E3 E3 G3 E3 D3 C3 B2
   164.81f, 164.81f, 196.00f, 164.81f, 130.81f, 146.83f, 130.81f,  // E3 E3 G3 E3 C3 D3 C3
   164.81f, 164.81f, 196.00f, 164.81f, 146.83f, 130.81f, 123.47f,
   164.81f, 164.81f, 196.00f, 164.81f, 130.81f, 146.83f, 130.81f,
   // Bridge: nouseva linja
   110.00f, 130.81f, 146.83f, 174.61f, 146.83f, 130.81f,           // A2 C3 D3 F3 D3 C3
   // Paluu pääriffiiin
   164.81f, 164.81f, 196.00f, 164.81f, 146.83f, 130.81f, 123.47f,
   164.81f, 164.81f,                                                 // E3 E3 (lopetus)
};
const int D2_NOTES_N = 40;

#define DEMO_OFF  0
#define DEMO_1    1
#define DEMO_2    2

// =============================================================================
// TILAMUUTTUJAT
// Kaikki volatile — Core 0 (buttonTask) kirjoittaa, Core 1 (Mozzi) lukee.
// Yksittäiset bool/int-kirjoitukset ovat atomisia ESP32:lla.
// =============================================================================

volatile bool chordMode    = true;      // true = sointutila, false = nuottitila
volatile int  currentSet   = 0;         // Aktiivinen sointusetti: 0=Pop, 1=Rock, 2=Balladi
volatile int  currentOct   = 1;         // Aktiivinen oktaavi: 0=Basso, 1=Normaali, 2=Melodia
volatile int  currentFx    = 0;         // Aktiivinen efekti: 0=Clean … 4=Ring Mod
volatile bool doReset      = false;     // Jos true, resetAll() kutsutaan seuraavassa updateControl():ssa
volatile int  demoMode     = DEMO_OFF;  // Aktiivinen demo: DEMO_OFF / DEMO_1 / DEMO_2
volatile bool demoChanged  = false;     // Jos true, updateDemo() alustaa demon alusta
volatile bool muteOpenFlag = false;     // Jos true, avoin sointu vaimennetaan ennen frettisoinnun laukaisua

// Avoimen soinnun ajastin — käytetään vain Core 1:llä, ei tarvitse volatile
static unsigned long openChordStartMs = 0;
static bool          openChordActive  = false;

// =============================================================================
// MOZZI-OBJEKTIT — käytetään vain Core 1:llä
// =============================================================================

// 18 siniaaltoosillaattoria (SIN2048: 2048 näytteen taulukko)
// Indeksikaava: fretti i → osc[i*3], osc[i*3+1], osc[i*3+2]
// Avoin sointu: osc[15], osc[16], osc[17] (OPEN_OSC_BASE = 15)
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> osc[NUM_OSC] = {
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[0]  fretti 0, ääni 1
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[1]  fretti 0, ääni 2
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[2]  fretti 0, ääni 3
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[3]  fretti 1, ääni 1
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[4]  fretti 1, ääni 2
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[5]  fretti 1, ääni 3
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[6]  fretti 2, ääni 1
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[7]  fretti 2, ääni 2
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[8]  fretti 2, ääni 3
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[9]  fretti 3, ääni 1
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[10] fretti 3, ääni 2
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[11] fretti 3, ääni 3
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[12] fretti 4, ääni 1
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[13] fretti 4, ääni 2
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[14] fretti 4, ääni 3
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[15] avoin, ääni 1
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[16] avoin, ääni 2
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),  // osc[17] avoin, ääni 3
};

ADSR<CONTROL_RATE, AUDIO_RATE> env[NUM_OSC];            // Yksi ADSR-verhokäyrä per osillaattori
Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfo(SIN2048_DATA);     // LFO tremololle ja vibratolle
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>   ringOsc(SIN2048_DATA); // Ring Mod -kantoaalto (110 Hz)

// =============================================================================
// NAPPIEN TILAMUUTTUJAT — käytetään vain Core 0:lla (buttonTask)
// =============================================================================

// Frettipinnit taulukossa indeksöintiä varten
const int FRET_PINS[NUM_FRETS] = {
   PIN_GREEN, PIN_RED, PIN_YELLOW, PIN_BLUE, PIN_ORANGE
};

// Fretin nykyinen pidostila — true jos fretti on pohjassa
bool fretHeld[NUM_FRETS] = {false};

// Edellinen tila nousevien reunojen tunnistamiseen (LOW→HIGH = nappi painettu,
// HIGH→LOW = nappi vapautettu). Alkuarvo true = ei painettu (pull-up = HIGH).
bool strumDLast    = true;
bool strumULast    = true;
bool selectLast    = true;
bool startLast     = true;
bool dpadUpLast    = true;
bool dpadDownLast  = true;
bool dpadLeftLast  = true;
bool dpadRightLast = true;
bool demo1Last     = true;
bool demo2Last     = true;

// Kommunikointiflakit Core 0 → Core 1.
// Core 0 asettaa true, Core 1 lukee ja nollaa oman kierroksensa alussa.
volatile bool triggerFlags[NUM_FRETS] = {false}; // Strum painettu ko. fretti pohjassa
volatile bool releaseFlags[NUM_FRETS] = {false}; // Ko. fretti vapautettu
volatile bool triggerOpen = false;               // Strum painettu ilman frettiä

// =============================================================================
// NAPPITEHTÄVÄ — ajetaan Core 0:lla FreeRTOS:n kautta
// Mozzi varaa Core 1:n äänituotantoon, joten napit luetaan Core 0:lla.
// =============================================================================

void buttonTask(void *pvParameters) {
   while (true) {
      // Demo-napit: nousevalla reunalla toggle (sama nappi käynnistää ja pysäyttää)
      bool cDemo1 = digitalRead(PIN_DEMO1) == LOW;
      bool cDemo2 = digitalRead(PIN_DEMO2) == LOW;

      if (cDemo1 && !demo1Last) {
         demoMode    = (demoMode == DEMO_1) ? DEMO_OFF : DEMO_1;
         demoChanged = true;
         Serial.println(demoMode == DEMO_1 ? "Demo 1 ON" : "Demo OFF");
      }
      if (cDemo2 && !demo2Last) {
         demoMode    = (demoMode == DEMO_2) ? DEMO_OFF : DEMO_2;
         demoChanged = true;
         Serial.println(demoMode == DEMO_2 ? "Demo 2 ON" : "Demo OFF");
      }
      demo1Last = cDemo1;
      demo2Last = cDemo2;

      // Kaikki muut napit ohitetaan demon aikana
      if (demoMode != DEMO_OFF) {
         vTaskDelay(BUTTON_POLL_MS / portTICK_PERIOD_MS);
         continue;
      }

      // Fretit: seuraa vapautusreunat (fretti nostettu = laukaise release)
      for (int i = 0; i < NUM_FRETS; i++) {
         bool pressed = digitalRead(FRET_PINS[i]) == LOW;
         if (!pressed && fretHeld[i]) releaseFlags[i] = true;
         fretHeld[i] = pressed;
      }

      // Strum: nouseva reuna (ei painettu → painettu) laukaisee äänen
      bool cStrumD = digitalRead(PIN_STRUM_D) == LOW;
      bool cStrumU = digitalRead(PIN_STRUM_U) == LOW;
      bool strumEdge = (cStrumD && !strumDLast) || (cStrumU && !strumULast);

      if (strumEdge) {
         bool cStart = digitalRead(PIN_START) == LOW;
         if (cStart) {
            // START + STRUM yhdistelmä: nollaa kaikki asetukset
            doReset = true;
         } else {
            bool any = false;
            for (int i = 0; i < NUM_FRETS; i++) {
               if (fretHeld[i]) { triggerFlags[i] = true; any = true; }
            }
            if (any) {
               // Yksi tai useampi fretti pohjassa: vaimenna avoin sointu ensin
               // jotta se ei jää soimaan uuden soinnun alle
               muteOpenFlag = true;
            } else {
               // Ei frettiä: soita avoin sointu tai nuotti tilan mukaan
               triggerOpen = true;
            }
         }
      }
      strumDLast = cStrumD;
      strumULast = cStrumU;

      // SELECT: nousevalla reunalla vaihda sointu-/nuottitilaa
      bool cSelect = digitalRead(PIN_SELECT) == LOW;
      if (cSelect && !selectLast) {
         chordMode = !chordMode;
         Serial.println(chordMode ? "Tila: Sointutila" : "Tila: Nuottitila");
      }
      selectLast = cSelect;

      // START yksin (ilman strumia): nousevalla reunalla kierrätä sointusetti
      bool cStart = digitalRead(PIN_START) == LOW;
      if (cStart && !startLast && !cStrumD && !cStrumU) {
         currentSet = (currentSet + 1) % NUM_SETS;
         Serial.println("Setti: " + String(SET_NAMES[currentSet]));
      }
      startLast = cStart;

      // D-pad ylös/alas: oktaavi — ei kierrätä rajojen yli
      bool cDpadUp   = digitalRead(PIN_JOY_UP)   == LOW;
      bool cDpadDown = digitalRead(PIN_JOY_DOWN)  == LOW;
      if (cDpadUp && !dpadUpLast && currentOct < NUM_OCT - 1) {
         currentOct++;
         Serial.println("Oktaavi: " + String(OCT_NAMES[currentOct]));
      }
      if (cDpadDown && !dpadDownLast && currentOct > 0) {
         currentOct--;
         Serial.println("Oktaavi: " + String(OCT_NAMES[currentOct]));
      }
      dpadUpLast   = cDpadUp;
      dpadDownLast = cDpadDown;

      // D-pad vasen/oikea: efekti — kierrättää rengasmaisesti
      bool cDpadLeft  = digitalRead(PIN_JOY_LEFT)  == LOW;
      bool cDpadRight = digitalRead(PIN_JOY_RIGHT) == LOW;
      if (cDpadLeft && !dpadLeftLast) {
         currentFx = (currentFx + NUM_FX - 1) % NUM_FX;
         Serial.println("Efekti: " + String(FX_NAMES[currentFx]));
      }
      if (cDpadRight && !dpadRightLast) {
         currentFx = (currentFx + 1) % NUM_FX;
         Serial.println("Efekti: " + String(FX_NAMES[currentFx]));
      }
      dpadLeftLast  = cDpadLeft;
      dpadRightLast = cDpadRight;

      vTaskDelay(BUTTON_POLL_MS / portTICK_PERIOD_MS);
   }
}

// =============================================================================
// ÄÄNEN OHJAUSFUNKTIOT — kutsutaan Core 1:ltä (updateControl)
// =============================================================================

// Laukaisee soinnun ADSR noteOn():lla.
// fret == -1: avoin sointu osc[15..17]:lla (ei häiritse fretti-osc:eja).
// fret 0–4:   kolmisointu osc[fret*3 .. fret*3+2]:lla.
void triggerChord(int fret) {
   if (fret == -1) {
      for (int n = 0; n < 3; n++) {
         osc[OPEN_OSC_BASE + n].setFreq(OPEN_CHORD[currentSet][n]);
         env[OPEN_OSC_BASE + n].noteOn();
      }
      openChordActive  = true;
      openChordStartMs = millis();
   } else {
      for (int n = 0; n < 3; n++) {
         osc[fret * 3 + n].setFreq(CHORDS[currentSet][fret][n]);
         env[fret * 3 + n].noteOn();
      }
   }
}

// Siirtää soinnun ADSR release-vaiheeseen (luonnollinen sammuminen).
void releaseChord(int fret) {
   if (fret == -1) {
      for (int n = 0; n < 3; n++) env[OPEN_OSC_BASE + n].noteOff();
      openChordActive = false;
   } else {
      for (int n = 0; n < 3; n++) env[fret * 3 + n].noteOff();
   }
}

// Laukaisee yksittäisen nuotin nuottitilassa.
// Vain osc[fret*3] käytössä — osc[fret*3+1] ja [+2] eivät ole noteOn-tilassa.
void triggerNote(int fret) {
   osc[fret * 3].setFreq(NOTES[currentOct][fret]);
   env[fret * 3].noteOn();
}

// Siirtää nuotin ADSR release-vaiheeseen.
void releaseNote(int fret) {
   env[fret * 3].noteOff();
}

// Soittaa suoran taajuuden avoimella osillaattorilla (osc[15]).
// Käytetään demotilassa nuottien toistamiseen.
void triggerFreq(float freq) {
   osc[OPEN_OSC_BASE].setFreq(freq);
   env[OPEN_OSC_BASE].noteOn();
}

// Käsittelee strumin ilman frettiä: avoin sointu tai C-nuotti tilan mukaan.
void triggerOpenSound() {
   if (chordMode) {
      triggerChord(-1);
   } else {
      osc[OPEN_OSC_BASE].setFreq(OPEN_NOTE[currentOct]);
      env[OPEN_OSC_BASE].noteOn();
      openChordActive  = true;
      openChordStartMs = millis();
   }
}

// Sammuttaa kaikki äänet välittömästi siirtämällä ne release-vaiheeseen.
void silenceAll() {
   for (int i = 0; i < NUM_OSC; i++) env[i].noteOff();
   openChordActive = false;
}

// Nollaa kaikki asetukset ja tilan oletusarvoihin.
// Laukaistaan START + STRUM -yhdistelmällä.
void resetAll() {
   silenceAll();
   for (int i = 0; i < NUM_FRETS; i++) {
      triggerFlags[i] = false;
      releaseFlags[i] = false;
      fretHeld[i]     = false;
   }
   triggerOpen      = false;
   muteOpenFlag     = false;
   demoMode         = DEMO_OFF;
   demoChanged      = false;
   openChordActive  = false;
   openChordStartMs = 0;
   chordMode        = true;
   currentSet       = 0;
   currentOct       = 1;
   currentFx        = 0;
   doReset          = false;
   lfo.setFreq(TREMOLO_RATE_HZ);
   Serial.println("RESET — Sointutila / Pop / Normaali / Clean");
}

// =============================================================================
// DEMOTILA — kutsutaan updateControl():sta Core 1:llä
// =============================================================================

static int           demoStep   = 0;
static unsigned long demoNextMs = 0;
static bool          demoIsGap  = false;  // Nuottitilassa: onko meneillään tauko äänten välillä

void updateDemo() {
   // Demo vaihtui: alusta tila ja odota 200ms ennen ensimmäistä ääntä
   if (demoChanged) {
      silenceAll();
      demoStep    = 0;
      demoNextMs  = millis() + 200;
      demoIsGap   = false;
      demoChanged = false;
      return;
   }
   if (demoMode == DEMO_OFF) return;
   if (millis() < demoNextMs)  return;  // Ei vielä aika

   const int*   chords = nullptr; int chordsN = 0;
   const float* notes  = nullptr; int notesN  = 0;
   switch (demoMode) {
      case DEMO_1: chords=D1_CHORDS; chordsN=D1_CHORDS_N; notes=D1_NOTES; notesN=D1_NOTES_N; break;
      case DEMO_2: chords=D2_CHORDS; chordsN=D2_CHORDS_N; notes=D2_NOTES; notesN=D2_NOTES_N; break;
      default: return;
   }

   if (chordMode) {
      // Sointutilassa ADSR hoitaa luontevan sammumisen — ei erillistä taukoa
      silenceAll();
      triggerChord(chords[demoStep % chordsN]);
      demoStep++;
      demoNextMs = millis() + DEMO_CHORD_MS;
   } else {
      // Nuottitilassa vuorotellaan: soiva nuotti → hiljainen tauko → seuraava nuotti
      if (demoIsGap) {
         demoIsGap = false;
         triggerFreq(notes[demoStep % notesN]);
         demoStep++;
         demoNextMs = millis() + DEMO_NOTE_MS;
      } else {
         silenceAll();
         demoIsGap  = true;
         demoNextMs = millis() + DEMO_GAP_MS;
      }
   }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
   Serial.begin(115200);

   // Kytke WiFi ja Bluetooth pois — ei käyttöä, säästää virtaa ja
   // vapauttaa ADC2-pinnit (WiFi varaa ne käytössä ollessaan)
   WiFi.mode(WIFI_OFF);
   btStop();

   // Kaikki napit INPUT_PULLUP — nappi painettu vetää LOW
   pinMode(PIN_GREEN,     INPUT_PULLUP);
   pinMode(PIN_RED,       INPUT_PULLUP);
   pinMode(PIN_YELLOW,    INPUT_PULLUP);
   pinMode(PIN_BLUE,      INPUT_PULLUP);
   pinMode(PIN_ORANGE,    INPUT_PULLUP);
   pinMode(PIN_STRUM_D,   INPUT_PULLUP);
   pinMode(PIN_STRUM_U,   INPUT_PULLUP);
   pinMode(PIN_SELECT,    INPUT_PULLUP);
   pinMode(PIN_START,     INPUT_PULLUP);
   pinMode(PIN_JOY_UP,    INPUT_PULLUP);
   pinMode(PIN_JOY_DOWN,  INPUT_PULLUP);
   pinMode(PIN_JOY_LEFT,  INPUT_PULLUP);
   pinMode(PIN_JOY_RIGHT, INPUT_PULLUP);
   pinMode(PIN_DEMO1,     INPUT_PULLUP);
   pinMode(PIN_DEMO2,     INPUT_PULLUP);
   // PIN_WHAMMY (GPIO34) on input-only ADC — pinMode ei tarvita.
   // Potentiometri kytkettynä 3.3V–GND hoitaa jännitteen ankkuroinnin.

   // ADSR-parametrit kaikille 18 osillaattorille
   for (int i = 0; i < NUM_OSC; i++) {
      env[i].setADLevels(ENV_ATTACK_LEVEL, ENV_DECAY_LEVEL);
      env[i].setTimes(ENV_ATTACK_MS, ENV_DECAY_MS, ENV_SUSTAIN_MS, ENV_RELEASE_MS);
   }

   lfo.setFreq(TREMOLO_RATE_HZ);
   ringOsc.setFreq(RING_MOD_FREQ_HZ);

   // Käynnistä nappitehtävä Core 0:lla, prioriteetti 1
   // Mozzi käyttää Core 1:tä — tämä jako estää kilpailutilanteet
   xTaskCreatePinnedToCore(
      buttonTask, "ButtonTask", BUTTON_TASK_STACK, NULL, 1, NULL, 0
   );

   startMozzi(CONTROL_RATE);
   Serial.println("Kaynnistetty — Sointutila / Pop / Normaali / Clean");
}

// =============================================================================
// MOZZI CONTROL LOOP — Core 1, kutsutaan CONTROL_RATE (128) kertaa sekunnissa
// Hoitaa: flagien käsittely, taajuuksien päivitys, ADSR-päivitys, demo-ajoitus
// =============================================================================

void updateControl() {
   // Nollaus ensin — kaikki muu ohitetaan tällä kierroksella
   if (doReset) { resetAll(); return; }

   // Demo hallitsee ääntä kun aktiivinen — normaali nappikäsittely ohitetaan
   if (demoMode != DEMO_OFF || demoChanged) {
      updateDemo();
      return;
   }

   // Vaimenna avoin sointu jos fretti juuri strumattu
   // muteOpenFlag asetetaan Core 0:ssa ENNEN triggerFlags:ien asettamista,
   // joten avoin sointu sammuu aina ennen uuden soinnun laukaisua
   if (muteOpenFlag) {
      releaseChord(-1);
      muteOpenFlag = false;
   }

   // Avoin sointu/nuotti: automaattinen sammutus ajastimella
   if (openChordActive && millis() - openChordStartMs > OPEN_CHORD_TIMEOUT) {
      releaseChord(-1);
   }

   // Käsittele fretin vapautukset (Core 0 asetti releaseFlags[i] = true)
   for (int i = 0; i < NUM_FRETS; i++) {
      if (releaseFlags[i]) {
         chordMode ? releaseChord(i) : releaseNote(i);
         releaseFlags[i] = false;
      }
   }

   // Käsittele strum-laukaisut (Core 0 asetti triggerFlags[i] = true)
   for (int i = 0; i < NUM_FRETS; i++) {
      if (triggerFlags[i]) {
         chordMode ? triggerChord(i) : triggerNote(i);
         triggerFlags[i] = false;
      }
   }

   // Avoin strum (ei frettiä)
   if (triggerOpen) {
      triggerOpenSound();
      triggerOpen = false;
   }

   // Whammy: ADC 0–4095 → 0–WHAMMY_MAX_BEND taajuuslisäys (suhteellinen)
   float bend = (float)mozziAnalogRead(PIN_WHAMMY) / 4095.0f * WHAMMY_MAX_BEND;

   // LFO: tremolo moduloi amplitudia updateAudio():ssa,
   //       vibrato moduloi taajuutta tässä vibratoMod-muuttujan kautta
   float vibratoMod = 0.0f;
   if (currentFx == 2) {
      lfo.setFreq(TREMOLO_RATE_HZ);
   } else if (currentFx == 3) {
      lfo.setFreq(VIBRATO_RATE_HZ);
      // lfo.next() palauttaa -128…127 → normalisoidaan -1…1 → kerrotaan syvyydellä
      vibratoMod = VIBRATO_DEPTH * ((float)lfo.next() / 128.0f);
   }

   // Päivitä frettiosillaattorien taajuudet ja ADSR (osc[0..14])
   for (int i = 0; i < NUM_FRETS; i++) {
      for (int n = 0; n < 3; n++) {
         int   idx  = i * 3 + n;
         // Nuottitilassa vain ääni n=0 on käytössä — n=1 ja n=2 asetetaan 0:ksi
         // jolloin ne eivät vaikuta miksaukseen (ADSR ei ole noteOn-tilassa)
         float base = chordMode
            ? CHORDS[currentSet][i][n]
            : (n == 0 ? NOTES[currentOct][i] : 0.0f);
         if (base > 0.0f)
            osc[idx].setFreq(base * (1.0f + bend + vibratoMod));
         env[idx].update();  // ADSR täytyy päivittää joka kierroksella
      }
   }

   // Päivitä avoimen soinnun osillaattorit (osc[15..17])
   for (int n = 0; n < 3; n++) {
      if (openChordActive) {
         float base = chordMode
            ? OPEN_CHORD[currentSet][n]
            : (n == 0 ? OPEN_NOTE[currentOct] : 0.0f);
         if (base > 0.0f)
            osc[OPEN_OSC_BASE + n].setFreq(base * (1.0f + bend + vibratoMod));
      }
      env[OPEN_OSC_BASE + n].update();
   }
}

// =============================================================================
// MOZZI AUDIO LOOP — Core 1, kutsutaan AUDIO_RATE kertaa sekunnissa
// Laskee yhden audionäytteen. Pidettävä lyhyenä — ei viiveitä, ei Serial-kutsuja.
// =============================================================================

AudioOutput_t updateAudio() {
   // Laske kaikkien osillaattorien ADSR-painotettu summa.
   // env.next() → 0…255 (uint8), osc.next() → -128…127 (int8)
   // Tulo on enintään 16-bittinen → jaetaan 256:lla (>> 8) ennen summaamista
   int32_t out = 0;
   for (int i = 0; i < NUM_OSC; i++)
      out += (int32_t)(env[i].next() * osc[i].next()) >> 8;

   // Skaalaa: ilman jakoa 18 samanaikainen ääntä ylivuotaisi helposti
   out /= AUDIO_SCALER;

   // Sovella valittu efekti
   switch (currentFx) {
      case 1:  // Distortion: vahvista ja leikkaa — kovakoodatut rajat ±127
         out = constrain(out * DISTORTION_GAIN, -128, 127);
         break;
      case 2:  // Tremolo: amplitudin modulaatio LFO:lla
         // lfo.next() → -128…127, lisätään 128 → 0…255, kerrotaan ja jaetaan 256:lla
         out = (int32_t)(out * (128 + lfo.next())) >> 8;
         break;
      case 3:  // Vibrato: taajuusmodulaatio hoidetaan updateControl():ssa
         break;
      case 4:  // Ring Mod: kerrotaan kantoaallolla (110 Hz)
         // ringOsc.next() → -128…127, jaetaan 128:lla normalisointia varten
         out = (int32_t)(out * ringOsc.next()) >> 7;
         break;
      default: // Clean: ei efektiä
         break;
   }

   return MonoOutput::from8Bit(out);
}

// Mozzi vaatii audioHook():n loop():ssa — älä lisää muuta koodia tänne
void loop() { audioHook(); }
