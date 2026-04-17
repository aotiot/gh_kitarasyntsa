// =============================================================================
// GUITAR HERO PS3 → ESP32 MOZZI SYNTETISAATTORI
// =============================================================================
//
// LAITTEISTO:
//   ESP32 DevKit V1 + PCM5102A I2S DAC + PAM8403 vahvistin + kaiutin
//   2× 18650 Li-ion akku + TP4056 latauspiiri + USB-C latausportti
//
// KIRJASTOT (asenna Arduino Library Managerista):
//   - Mozzi by Tim Barrass
//
// =============================================================================
// KAIKKI HELPOSTI MUUTETTAVAT ASETUKSET
// =============================================================================

// --- ÄÄNI ---
#define AUDIO_SCALER        20    // Ylivuodon esto. Suurempi = hiljaisempi. Alue: 15–30.
#define CONTROL_RATE        128   // Mozzi control-rate Hz. Käytä 2:n potensseja.

// ADSR-verhokäyrä
#define ENV_ATTACK_MS         5
#define ENV_DECAY_MS         80
#define ENV_SUSTAIN_MS      150
#define ENV_RELEASE_MS      400
#define ENV_ATTACK_LEVEL    255
#define ENV_DECAY_LEVEL     180

// Avoin sointu sammuu automaattisesti tämän ajan jälkeen (ms)
// Estää avoimen soinnun jäämisen soimaan loputtomiin
#define OPEN_CHORD_TIMEOUT  2000

// --- EFEKTIT ---
#define DISTORTION_GAIN       6   // Säröytyksen vahvistus. Alue: 3–10.
#define TREMOLO_RATE_HZ     4.0f  // Tremolo-nopeus Hz
#define VIBRATO_RATE_HZ     6.0f  // Vibrato-nopeus Hz
#define VIBRATO_DEPTH       0.015f // Vibrato-syvyys ±1.5%
#define RING_MOD_FREQ_HZ    110.0f // Ring mod kantotaajuus Hz (A2)

// --- WHAMMY ---
#define WHAMMY_MAX_BEND     0.05f // Maksimi pitch bend 5%

// --- DEMO ---
#define DEMO_CHORD_MS       600   // Sointujen soittoaika ms
#define DEMO_NOTE_MS        300   // Nuottien soittoaika ms
#define DEMO_GAP_MS         100   // Tauko sävelten välillä ms

// --- FREERTOS ---
#define BUTTON_TASK_STACK   4096  // Kasvata jos ESP32 kaatuu (stack overflow)
#define BUTTON_POLL_MS        10  // Nappien lukukierroksen viive ms

// =============================================================================
// PINNIMÄÄRITYKSET
// =============================================================================
//
// ESP32 DevKit V1 — rajoitukset:
//   GPIO 6–11  = Flash-muisti — EI KOSKAAN käytä
//   GPIO 1, 3  = USB Serial TX/RX — varattava ohjelmointia varten
//   GPIO 25,26 = I2S Mozzi (BCK, WS) — varattu
//   GPIO 19    = I2S Mozzi (DATA)    — varattu
//   GPIO 0,2,15= Strapping-pinnejä — toimivat INPUT_PULLUP:lla
//   GPIO 12    = Strapping flash-jännitteelle — VÄLTÄ, käytetty Demo1→siirretty
//   GPIO 34,35,36,39 = Vain input, ei sisäistä pull-up vastusta
//
// PINNIKARTTA:
//  VASEN REUNA (ylhäältä alas)     OIKEA REUNA (ylhäältä alas)
//  3V3  → PCM5102A VCC             VIN  → TP4056 OUT+
//  GND  → Yhteinen maa             GND  → TP4056 OUT-
//  IO15 → SELECT                   IO13 → START
//  IO2  → ORANSSI fret             IO12   (vapaa — strapping, vältä)
//  IO4  → D-pad ylös               IO14 → STRUM ylös
//  IO5  → D-pad alas               IO16 → D-pad vasen
//  IO18 → DEMO 3                   IO17 → D-pad oikea
//  IO19 → I2S DATA (Mozzi)         IO32 → DEMO 1
//  IO21 → KELTAINEN fret           IO33 → DEMO 2
//  IO22 → SININEN fret             IO34 → WHAMMY (ADC1, input-only)
//  IO23 → STRUM alas               IO35 → VIHREÄ fret (input-only, pot.OK)
//  IO25 → I2S WS   (Mozzi)         IO36   (vapaa, input-only)
//  IO26 → I2S BCK  (Mozzi)         IO39   (vapaa, input-only)
//  IO27 → PUNAINEN fret
//
// RYHMÄT:
//   A — Fretit:    VIHREÄ=35*, PUNAINEN=27, KELTAINEN=21, SININEN=22, ORANSSI=2
//                  (* input-only, ei pull-up — nappi kytkee GND:hen suoraan OK)
//   B — Strum:     YLÖS=14, ALAS=23, SELECT=15, START=13
//   C — D-pad:     YLÖS=4, ALAS=5, VASEN=16, OIKEA=17
//   D — Demo:      DEMO1=32, DEMO2=33, DEMO3=18
//   E — Whammy:    GPIO 34 (ADC1, input-only)
//   F — I2S DAC:   BCK=26, WS=25, DATA=19
//   G — Avoin:     osc[15..17] varattu avoimelle soinnulle/nuotille

// I2S DAC — määriteltävä ENNEN kirjastoja
#define MOZZI_AUDIO_MODE    MOZZI_OUTPUT_I2S_DAC
#define MOZZI_I2S_PIN_BCK   26
#define MOZZI_I2S_PIN_WS    25
#define MOZZI_I2S_PIN_DATA  19

// Fretit (ryhmä A)
#define PIN_GREEN           35   // input-only — nappi kytkee suoraan GND:hen, OK
#define PIN_RED             27
#define PIN_YELLOW          21
#define PIN_BLUE            22
#define PIN_ORANGE           2   // strapping, INPUT_PULLUP OK

// Strum ja ohjainnapit (ryhmä B)
#define PIN_STRUM_U         14
#define PIN_STRUM_D         23
#define PIN_SELECT          15
#define PIN_START           13

// D-pad (ryhmä C)
#define PIN_JOY_UP           4   // D-pad ylös  → oktaavi ylös
#define PIN_JOY_DOWN         5   // D-pad alas  → oktaavi alas
#define PIN_JOY_LEFT        16   // D-pad vasen → edellinen efekti
#define PIN_JOY_RIGHT       17   // D-pad oikea → seuraava efekti

// Demo-napit (ryhmä D) — GPIO 12 vaihdettu 32:een (ei strapping)
#define PIN_DEMO1           32
#define PIN_DEMO2           33
#define PIN_DEMO3           18

// Whammy (ryhmä E) — ADC1, input-only, ei pinMode tarvita
#define PIN_WHAMMY          34

// =============================================================================
// TAAJUUSTAULUKOT
// =============================================================================

#define NUM_SETS     3
#define NUM_FRETS    5
#define NUM_OCT      3
#define NUM_FX       5
// 15 osillaattoria freteille + 3 avoimelle soinnulle = 18
#define NUM_OSC      18
#define OPEN_OSC_BASE 15   // Avoimen soinnun/nuotin osillaattorit: osc[15..17]

// Avoin strum-sointu per setti (I-sointu)
const float OPEN_CHORD[NUM_SETS][3] = {
   { 196.00f, 246.94f, 293.66f },   // G  (Pop)
   { 164.81f, 207.65f, 246.94f },   // E  (Rock)
   { 130.81f, 164.81f, 196.00f },   // C  (Balladi)
};

// Frettisoinnut — sama nappi = sama harmoninen funktio
const float CHORDS[NUM_SETS][NUM_FRETS][3] = {
   {  // Pop (G-duuri): IV, V, vi, ii, bVII
      { 130.81f, 164.81f, 196.00f },   // C  IV
      { 146.83f, 185.00f, 220.00f },   // D  V
      { 164.81f, 196.00f, 246.94f },   // Em vi
      { 220.00f, 261.63f, 329.63f },   // Am ii
      { 174.61f, 220.00f, 261.63f },   // F  bVII
   },
   {  // Rock (E-duuri) — "kitaran linkkuveitsi": IV, V, vi, ii, bVII
      { 110.00f, 138.59f, 164.81f },   // A   IV
      { 123.47f, 155.56f, 185.00f },   // B   V
      { 138.59f, 164.81f, 207.65f },   // C#m vi
      { 185.00f, 220.00f, 277.18f },   // F#m ii
      { 146.83f, 185.00f, 220.00f },   // D   bVII
   },
   {  // Balladi (C-duuri): IV, V, vi, ii, bVII
      { 174.61f, 220.00f, 261.63f },   // F  IV
      { 196.00f, 246.94f, 293.66f },   // G  V
      { 220.00f, 261.63f, 329.63f },   // Am vi
      { 146.83f, 174.61f, 220.00f },   // Dm ii
      { 116.54f, 146.83f, 174.61f },   // Bb bVII
   },
};

// Nuotit kolmessa oktaavissa — C-duuriasteikko
// Strum (avoin) = C, fretit = D E F G A
const float NOTES[NUM_OCT][NUM_FRETS] = {
   { 146.83f, 164.81f, 174.61f, 196.00f, 220.00f },  // Basso    D3 E3 F3 G3 A3
   { 293.66f, 329.63f, 349.23f, 392.00f, 440.00f },  // Normaali D4 E4 F4 G4 A4
   { 587.33f, 659.25f, 698.46f, 784.00f, 880.00f },  // Melodia  D5 E5 F5 G5 A5
};

// Avoin C per oktaavi (strum ilman frettiä nuottitilassa)
const float OPEN_NOTE[NUM_OCT] = {
   130.81f,   // C3
   261.63f,   // C4
   523.25f,   // C5
};

const char* SET_NAMES[NUM_SETS]  = { "Pop", "Rock", "Balladi" };
const char* OCT_NAMES[NUM_OCT]   = { "Basso", "Normaali", "Melodia" };
const char* FX_NAMES[NUM_FX]     = { "Clean", "Distortion", "Tremolo", "Vibrato", "Ring Mod" };
const char* OPEN_NAMES[NUM_SETS] = { "G (I)", "E (I)", "C (I)" };
const char* CHORD_NAMES[NUM_SETS][NUM_FRETS] = {
   { "C (IV)", "D (V)",  "Em (vi)", "Am (ii)", "F (bVII)"  },
   { "A (IV)", "B (V)",  "C#m(vi)", "F#m(ii)", "D (bVII)"  },
   { "F (IV)", "G (V)",  "Am (vi)", "Dm (ii)", "Bb (bVII)" },
};

// =============================================================================
// DEMO-KAPPALEET
// =============================================================================
//
// SOINTUTILA:
//   Demo 1 — Let It Be (Beatles):         C–G–Am–F  (Pop-setti)
//   Demo 2 — Smoke on the Water (Purple): E–G–A     (Rock-setti)
//   Demo 3 — House of the Rising Sun:     Am–C–D–F  (Pop-setti)
//
// NUOTTITILA:
//   Demo 1 — Ode to Joy (Beethoven)
//   Demo 2 — Seven Nation Army (The White Stripes)
//   Demo 3 — Twinkle Twinkle Little Star
//
// Sointunumero: -1 = avoin sointu, 0–4 = fretti-indeksi

const int   D1_CHORDS[]  = { 0, -1, 3, 4,  0, -1, 3, 4 };
const int   D1_CHORDS_N  = 8;
const float D1_NOTES[]   = {
   329.63f, 329.63f, 349.23f, 392.00f,
   392.00f, 349.23f, 329.63f, 293.66f,
   261.63f, 261.63f, 293.66f, 329.63f,
   329.63f, 293.66f, 293.66f
};
const int   D1_NOTES_N   = 15;

const int   D2_CHORDS[]  = { -1, 2, 0,  -1, 2, 3, 0,  -1, 2, 0 };
const int   D2_CHORDS_N  = 10;
const float D2_NOTES[]   = {
   164.81f, 164.81f, 196.00f, 164.81f,
   146.83f, 130.81f, 123.47f,
   164.81f, 164.81f, 196.00f, 164.81f,
   130.81f, 146.83f, 130.81f
};
const int   D2_NOTES_N   = 14;

const int   D3_CHORDS[]  = { 3, 0, 1, 4,  3, 0, -1, -1 };
const int   D3_CHORDS_N  = 8;
const float D3_NOTES[]   = {
   261.63f, 261.63f, 392.00f, 392.00f,
   440.00f, 440.00f, 392.00f,
   349.23f, 349.23f, 329.63f, 329.63f,
   293.66f, 293.66f, 261.63f,
   392.00f, 392.00f, 349.23f, 349.23f,
   329.63f, 329.63f, 293.66f,
   392.00f, 392.00f, 349.23f, 349.23f,
   329.63f, 329.63f, 293.66f
};
const int   D3_NOTES_N   = 28;

#define DEMO_OFF  0
#define DEMO_1    1
#define DEMO_2    2
#define DEMO_3    3

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
// TILAMUUTTUJAT — volatile: Core 0 kirjoittaa, Core 1 lukee
// =============================================================================

volatile bool chordMode   = true;
volatile int  currentSet  = 0;     // volatile: Core 0 kirjoittaa, Core 1 lukee
volatile int  currentOct  = 1;     // volatile: Core 0 kirjoittaa, Core 1 lukee
volatile int  currentFx   = 0;     // volatile: Core 0 kirjoittaa, Core 1 lukee
volatile bool doReset     = false;
volatile int  demoMode    = DEMO_OFF;
volatile bool demoChanged = false;

// Avoimen soinnun ajastin — sammutus OPEN_CHORD_TIMEOUT ms jälkeen
// Nolla = ei aktiivista avointa sointua
static unsigned long openChordStartMs = 0;
static bool          openChordActive  = false;

// =============================================================================
// MOZZI-OBJEKTIT — Core 1
// =============================================================================

// osc[0..14]  = frettiosillaattorit (fretti N → osc[N*3..N*3+2])
// osc[15..17] = avoimen soinnun/nuotin omat osillaattorit
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> osc[NUM_OSC] = {
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA),
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA), // osc[15] avoin
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA), // osc[16] avoin
   Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>(SIN2048_DATA), // osc[17] avoin
};

ADSR<CONTROL_RATE, AUDIO_RATE> env[NUM_OSC];
Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfo(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE>   ringOsc(SIN2048_DATA);

// =============================================================================
// NAPPIEN TILAMUUTTUJAT — Core 0
// =============================================================================

const int FRET_PINS[NUM_FRETS] = {
   PIN_GREEN, PIN_RED, PIN_YELLOW, PIN_BLUE, PIN_ORANGE
};

bool fretHeld[NUM_FRETS]  = {false};
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
bool demo3Last     = true;

volatile bool triggerFlags[NUM_FRETS] = {false};
volatile bool releaseFlags[NUM_FRETS] = {false};
volatile bool triggerOpen = false;

// =============================================================================
// TEHTÄVÄ: Nappien luku Core 0:lla
// =============================================================================

void buttonTask(void *pvParameters) {
   while (true) {
      bool cStrumD    = digitalRead(PIN_STRUM_D)  == LOW;
      bool cStrumU    = digitalRead(PIN_STRUM_U)  == LOW;
      bool cSelect    = digitalRead(PIN_SELECT)    == LOW;
      bool cStart     = digitalRead(PIN_START)     == LOW;
      bool cDpadUp    = digitalRead(PIN_JOY_UP)    == LOW;
      bool cDpadDown  = digitalRead(PIN_JOY_DOWN)  == LOW;
      bool cDpadLeft  = digitalRead(PIN_JOY_LEFT)  == LOW;
      bool cDpadRight = digitalRead(PIN_JOY_RIGHT) == LOW;
      bool cDemo1     = digitalRead(PIN_DEMO1)     == LOW;
      bool cDemo2     = digitalRead(PIN_DEMO2)     == LOW;
      bool cDemo3     = digitalRead(PIN_DEMO3)     == LOW;

      // Demo-napit: sama nappi pysäyttää, eri nappi vaihtaa
      if (cDemo1 && !demo1Last) {
         demoMode    = (demoMode == DEMO_1) ? DEMO_OFF : DEMO_1;
         demoChanged = true;
      }
      if (cDemo2 && !demo2Last) {
         demoMode    = (demoMode == DEMO_2) ? DEMO_OFF : DEMO_2;
         demoChanged = true;
      }
      if (cDemo3 && !demo3Last) {
         demoMode    = (demoMode == DEMO_3) ? DEMO_OFF : DEMO_3;
         demoChanged = true;
      }
      demo1Last = cDemo1;
      demo2Last = cDemo2;
      demo3Last = cDemo3;

      // Muut napit eivät toimi demon aikana
      if (demoMode != DEMO_OFF) {
         vTaskDelay(BUTTON_POLL_MS / portTICK_PERIOD_MS);
         continue;
      }

      // Fretit
      for (int i = 0; i < NUM_FRETS; i++) {
         bool pressed = digitalRead(FRET_PINS[i]) == LOW;
         if (!pressed && fretHeld[i]) releaseFlags[i] = true;
         fretHeld[i] = pressed;
      }

      // Strum — START + STRUM = nollaa
      bool strumEdge = (cStrumD && !strumDLast) || (cStrumU && !strumULast);
      if (strumEdge) {
         if (cStart) {
            doReset = true;
         } else {
            bool any = false;
            for (int i = 0; i < NUM_FRETS; i++) {
               if (fretHeld[i]) { triggerFlags[i] = true; any = true; }
            }
            if (!any) triggerOpen = true;
         }
      }
      strumDLast = cStrumD;
      strumULast = cStrumU;

      // Select
      if (cSelect && !selectLast) {
         chordMode = !chordMode;
         Serial.println(chordMode ? "Tila: Sointutila" : "Tila: Nuottitila");
      }
      selectLast = cSelect;

      // Start yksin: kierrätä sointusetti
      if (cStart && !startLast && !cStrumD && !cStrumU) {
         currentSet = (currentSet + 1) % NUM_SETS;
         Serial.println("Setti: " + String(SET_NAMES[currentSet]));
      }
      startLast = cStart;

      // D-pad ylös/alas: oktaavi
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

      // D-pad vasen/oikea: efekti
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
// ÄÄNEN OHJAUSFUNKTIOT — Core 1
// =============================================================================

// Laukaisee fretin soinnun. fret=-1 käyttää avoimen soinnun osillaattoreita.
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
         int idx = fret * 3 + n;
         osc[idx].setFreq(CHORDS[currentSet][fret][n]);
         env[idx].noteOn();
      }
   }
}

void releaseChord(int fret) {
   if (fret == -1) {
      for (int n = 0; n < 3; n++) env[OPEN_OSC_BASE + n].noteOff();
      openChordActive = false;
   } else {
      for (int n = 0; n < 3; n++) env[fret * 3 + n].noteOff();
   }
}

// Laukaisee yhden nuotin. Avoin nuotti käyttää osc[OPEN_OSC_BASE].
void triggerNote(int fret) {
   osc[fret * 3].setFreq(NOTES[currentOct][fret]);
   env[fret * 3].noteOn();
}

void releaseNote(int fret) {
   env[fret * 3].noteOff();
}

// Soittaa suoran taajuuden — demotila käyttää nuottitilassa
void triggerFreq(float freq) {
   osc[OPEN_OSC_BASE].setFreq(freq);
   env[OPEN_OSC_BASE].noteOn();
}

// Strum ilman frettiä — käyttää omia osillaattoreita (ei konflikti frettien kanssa)
void triggerOpenSound() {
   if (chordMode) {
      triggerChord(-1);
      Serial.println(OPEN_NAMES[currentSet]);
   } else {
      osc[OPEN_OSC_BASE].setFreq(OPEN_NOTE[currentOct]);
      env[OPEN_OSC_BASE].noteOn();
      openChordActive  = true;
      openChordStartMs = millis();
   }
}

void silenceAll() {
   for (int i = 0; i < NUM_OSC; i++) env[i].noteOff();
   openChordActive = false;
}

void resetAll() {
   silenceAll();
   for (int i = 0; i < NUM_FRETS; i++) {
      triggerFlags[i] = false;
      releaseFlags[i] = false;
      fretHeld[i]     = false;
   }
   triggerOpen      = false;
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
// DEMOTILAN AJASTIN — Core 1
// =============================================================================

static int           demoStep   = 0;
static unsigned long demoNextMs = 0;

void updateDemo() {
   if (demoChanged) {
      silenceAll();
      demoStep    = 0;
      demoNextMs  = millis() + 200;
      demoChanged = false;
      return;
   }
   if (demoMode == DEMO_OFF) return;
   if (millis() < demoNextMs) return;

   const int*   chords  = nullptr; int chordsN = 0;
   const float* notes   = nullptr; int notesN  = 0;
   switch (demoMode) {
      case DEMO_1: chords=D1_CHORDS; chordsN=D1_CHORDS_N; notes=D1_NOTES; notesN=D1_NOTES_N; break;
      case DEMO_2: chords=D2_CHORDS; chordsN=D2_CHORDS_N; notes=D2_NOTES; notesN=D2_NOTES_N; break;
      case DEMO_3: chords=D3_CHORDS; chordsN=D3_CHORDS_N; notes=D3_NOTES; notesN=D3_NOTES_N; break;
      default: return;
   }

   silenceAll();
   if (chordMode) {
      triggerChord(chords[demoStep % chordsN]);
      demoStep++;
      demoNextMs = millis() + DEMO_CHORD_MS;
   } else {
      triggerFreq(notes[demoStep % notesN]);
      demoStep++;
      demoNextMs = millis() + DEMO_NOTE_MS + DEMO_GAP_MS;
   }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
   Serial.begin(115200);

   // WiFi ja Bluetooth pois — ei tarvita, vapauttaa ADC2-pinnit
   WiFi.mode(WIFI_OFF);
   btStop();

   // Fretit — GPIO 35 on input-only, ei sisäistä pull-up
   // Nappi kytkee suoraan GND:hen → digitalRead LOW kun painettu → toimii
   pinMode(PIN_GREEN,     INPUT);        // GPIO 35 — input-only, nappi → GND
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
   pinMode(PIN_DEMO3,     INPUT_PULLUP);
   // PIN_WHAMMY (34) input-only ADC1 — ei pinMode tarvita

   for (int i = 0; i < NUM_OSC; i++) {
      env[i].setADLevels(ENV_ATTACK_LEVEL, ENV_DECAY_LEVEL);
      env[i].setTimes(ENV_ATTACK_MS, ENV_DECAY_MS, ENV_SUSTAIN_MS, ENV_RELEASE_MS);
   }

   lfo.setFreq(TREMOLO_RATE_HZ);
   ringOsc.setFreq(RING_MOD_FREQ_HZ);

   xTaskCreatePinnedToCore(
      buttonTask, "ButtonTask", BUTTON_TASK_STACK, NULL, 1, NULL, 0
   );

   startMozzi(CONTROL_RATE);
   Serial.println("Kaynnistetty — Sointutila / Pop / Normaali / Clean");
}

// =============================================================================
// MOZZI CONTROL LOOP — Core 1, 128 Hz
// =============================================================================

void updateControl() {
   if (doReset) { resetAll(); return; }

   if (demoMode != DEMO_OFF || demoChanged) {
      updateDemo();
      return;
   }

   // Avoimen soinnun automaattinen sammutus ajastimen perusteella
   if (openChordActive && millis() - openChordStartMs > OPEN_CHORD_TIMEOUT) {
      releaseChord(-1);
      Serial.println("Avoin sointu sammutettu (timeout)");
   }

   // Vapautus-flagit
   for (int i = 0; i < NUM_FRETS; i++) {
      if (releaseFlags[i]) {
         chordMode ? releaseChord(i) : releaseNote(i);
         releaseFlags[i] = false;
      }
   }

   // Laukaisu-flagit
   for (int i = 0; i < NUM_FRETS; i++) {
      if (triggerFlags[i]) {
         chordMode ? triggerChord(i) : triggerNote(i);
         triggerFlags[i] = false;
      }
   }
   if (triggerOpen) {
      triggerOpenSound();
      triggerOpen = false;
   }

   // Whammy
   float bend = (float)mozziAnalogRead(PIN_WHAMMY) / 4095.0f * WHAMMY_MAX_BEND;

   // LFO efektin mukaan
   float vibratoMod = 0.0f;
   if (currentFx == 2) {
      lfo.setFreq(TREMOLO_RATE_HZ);
   } else if (currentFx == 3) {
      lfo.setFreq(VIBRATO_RATE_HZ);
      vibratoMod = VIBRATO_DEPTH * ((float)lfo.next() / 128.0f);
   }

   // Päivitä frettiosillaattorien taajuudet (osc[0..14])
   for (int i = 0; i < NUM_FRETS; i++) {
      for (int n = 0; n < 3; n++) {
         int   idx  = i * 3 + n;
         float base = chordMode
            ? CHORDS[currentSet][i][n]
            : (n == 0 ? NOTES[currentOct][i] : 0.0f);
         if (base > 0.0f)
            osc[idx].setFreq(base * (1.0f + bend + vibratoMod));
         env[idx].update();
      }
   }

   // Päivitä avoimen soinnun osillaattorit (osc[15..17])
   for (int n = 0; n < 3; n++) {
      env[OPEN_OSC_BASE + n].update();
   }
}

// =============================================================================
// MOZZI AUDIO LOOP — Core 1, 16384 Hz
// =============================================================================

AudioOutput_t updateAudio() {
   int32_t out = 0;

   for (int i = 0; i < NUM_OSC; i++)
      out += (int32_t)(env[i].next() * osc[i].next()) >> 8;

   // Jaa 18:lla (NUM_OSC) ylikuormituksen estämiseksi
   out /= AUDIO_SCALER;

   switch (currentFx) {
      case 1:  out = constrain(out * DISTORTION_GAIN, -128, 127); break;
      case 2:  out = (int32_t)(out * (128 + lfo.next())) >> 8;    break;
      case 3:  break;
      case 4:  out = (int32_t)(out * ringOsc.next()) >> 7;         break;
      default: break;
   }

   return StereoOutput::fromNBit(8, out, out);
}

void loop() { audioHook(); }
