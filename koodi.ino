// =============================================================================
// GUITAR HERO PS3 → ESP32 MOZZI SYNTETISAATTORI
// =============================================================================
//
// LAITTEISTO:
//   ESP32 DevKit V1 + PCM5102A I2S DAC + PAM8403 vahvistin + kaiutin
//   2× 18650 Li-ion akku + TP4056+DW01 latauspiiri + USB-C latausportti
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
#define OPEN_CHORD_TIMEOUT  2000

// --- EFEKTIT ---
#define DISTORTION_GAIN       6   // Säröytyksen vahvistus. Alue: 3–10.
#define TREMOLO_RATE_HZ     4.0f
#define VIBRATO_RATE_HZ     6.0f
#define VIBRATO_DEPTH       0.015f
#define RING_MOD_FREQ_HZ    110.0f

// --- WHAMMY ---
#define WHAMMY_MAX_BEND     0.05f // Max pitch bend 5%

// --- DEMO ---
#define DEMO_CHORD_MS       600   // Soinnun kesto ms
#define DEMO_NOTE_MS        300   // Nuotin kesto ms
#define DEMO_GAP_MS         80    // Hiljainen tauko nuottien välillä ms

// --- FREERTOS ---
#define BUTTON_TASK_STACK   4096
#define BUTTON_POLL_MS        10

// =============================================================================
// PINNIMÄÄRITYKSET
// =============================================================================
//
// ESP32 DevKit V1 rajoitukset:
//   GPIO 6–11   = Flash-muisti — EI KOSKAAN käytä
//   GPIO 1, 3   = USB Serial — varattava ohjelmointia varten
//   GPIO 25, 26 = I2S Mozzi BCK/WS — varattu
//   GPIO 19     = I2S Mozzi DATA   — varattu
//   GPIO 34–39  = Vain input, ei sisäistä pull-up vastusta
//   GPIO 0, 2, 15 = Strapping — toimivat INPUT_PULLUP:lla
//
// PINNIKARTTA:
//   VASEN REUNA                    OIKEA REUNA
//   3V3  → PCM5102A VCC            VIN  → TP4056 OUT+
//   GND  → Yhteinen maa            GND  → TP4056 OUT-
//   IO15 → SELECT                  IO13 → START
//   IO2  → ORANSSI fret            IO12   (vapaa, vältä strapping)
//   IO4  → D-pad ylös              IO14 → STRUM ylös
//   IO5  → D-pad alas              IO16 → D-pad vasen
//   IO18 → DEMO 2                  IO17 → D-pad oikea
//   IO19 → I2S DATA (Mozzi)        IO32 → VIHREÄ fret
//   IO21 → KELTAINEN fret          IO33 → DEMO 1
//   IO22 → SININEN fret            IO34 → WHAMMY (ADC1)
//   IO23 → STRUM alas              IO35   (vapaa, input-only)
//   IO25 → I2S WS   (Mozzi)        IO36   (vapaa, input-only)
//   IO26 → I2S BCK  (Mozzi)        IO39   (vapaa, input-only)
//   IO27 → PUNAINEN fret
//
// RYHMÄT:
//   A — Fretit:  VIHREÄ=32, PUNAINEN=27, KELTAINEN=21, SININEN=22, ORANSSI=2
//   B — Strum:   YLÖS=14, ALAS=23, SELECT=15, START=13
//   C — D-pad:   YLÖS=4, ALAS=5, VASEN=16, OIKEA=17
//   D — Demo:    DEMO1=33, DEMO2=18
//   E — Whammy:  GPIO 34 (ADC1, input-only, pot. hoitaa pull-upin)
//   F — I2S DAC: BCK=26, WS=25, DATA=19

// I2S DAC — määriteltävä ENNEN kirjastoja
#define MOZZI_AUDIO_MODE    MOZZI_OUTPUT_I2S_DAC
#define MOZZI_I2S_PIN_BCK   26
#define MOZZI_I2S_PIN_WS    25
#define MOZZI_I2S_PIN_DATA  19

// Fretit (ryhmä A) — kaikki normaaleja, sisäinen pull-up toimii
#define PIN_GREEN           32
#define PIN_RED             27
#define PIN_YELLOW          21
#define PIN_BLUE            22
#define PIN_ORANGE           2   // strapping, INPUT_PULLUP pitää HIGH käynn. — OK

// Strum ja ohjainnapit (ryhmä B)
#define PIN_STRUM_U         14
#define PIN_STRUM_D         23
#define PIN_SELECT          15   // strapping, INPUT_PULLUP OK
#define PIN_START           13

// D-pad (ryhmä C)
#define PIN_JOY_UP           4   // oktaavi ylös
#define PIN_JOY_DOWN         5   // oktaavi alas
#define PIN_JOY_LEFT        16   // edellinen efekti
#define PIN_JOY_RIGHT       17   // seuraava efekti

// Demo-napit (ryhmä D) — molemmat normaaleja, sisäinen pull-up
#define PIN_DEMO1           33
#define PIN_DEMO2           18

// Whammy (ryhmä E) — ADC1, input-only, ei pinMode tarvita
// Potentiometri hoitaa jännitteenjaon — ei pull-up vastusta tarvita
#define PIN_WHAMMY          34

// =============================================================================
// TAAJUUSTAULUKOT
// =============================================================================

#define NUM_SETS      3
#define NUM_FRETS     5
#define NUM_OCT       3
#define NUM_FX        5
#define NUM_OSC       18          // 15 frettiosillaattoria + 3 avoimelle soinnulle
#define OPEN_OSC_BASE 15          // Avoimen soinnun osillaattorit: osc[15..17]

// Avoin strum-sointu per setti (I-sointu, soitetaan ilman frettiä)
const float OPEN_CHORD[NUM_SETS][3] = {
   { 196.00f, 246.94f, 293.66f },   // G  (Pop)
   { 164.81f, 207.65f, 246.94f },   // E  (Rock)
   { 130.81f, 164.81f, 196.00f },   // C  (Balladi)
};

// Frettisoinnut — sama nappi = sama harmoninen funktio eri seteissä
// Vihreä=IV, Punainen=V, Keltainen=vi, Sininen=ii, Oranssi=bVII
const float CHORDS[NUM_SETS][NUM_FRETS][3] = {
   {  // Pop (G-duuri)
      { 130.81f, 164.81f, 196.00f },   // C  IV
      { 146.83f, 185.00f, 220.00f },   // D  V
      { 164.81f, 196.00f, 246.94f },   // Em vi
      { 220.00f, 261.63f, 329.63f },   // Am ii
      { 174.61f, 220.00f, 261.63f },   // F  bVII
   },
   {  // Rock (E-duuri) — "kitaran linkkuveitsi"
      { 110.00f, 138.59f, 164.81f },   // A   IV
      { 123.47f, 155.56f, 185.00f },   // B   V
      { 138.59f, 164.81f, 207.65f },   // C#m vi
      { 185.00f, 220.00f, 277.18f },   // F#m ii
      { 146.83f, 185.00f, 220.00f },   // D   bVII
   },
   {  // Balladi (C-duuri)
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
//
// NUOTTITILA:
//   Demo 1 — Ode to Joy (Beethoven)
//   Demo 2 — Seven Nation Army (The White Stripes)
//
// Sointunumero: -1 = avoin sointu (strum), 0–4 = fretti-indeksi

// Demo 1 sointutila: Let It Be — C(0) G(avoin) Am(3) F(4)
const int   D1_CHORDS[]  = { 0, -1, 3, 4,  0, -1, 3, 4 };
const int   D1_CHORDS_N  = 8;

// Demo 1 nuottitila: Ode to Joy
const float D1_NOTES[]   = {
   329.63f, 329.63f, 349.23f, 392.00f,   // E4 E4 F4 G4
   392.00f, 349.23f, 329.63f, 293.66f,   // G4 F4 E4 D4
   261.63f, 261.63f, 293.66f, 329.63f,   // C4 C4 D4 E4
   329.63f, 293.66f, 293.66f             // E4 D4 D4
};
const int   D1_NOTES_N   = 15;

// Demo 2 sointutila: Smoke on the Water — E(avoin) G(2) A(0)
const int   D2_CHORDS[]  = { -1, 2, 0,  -1, 2, 3, 0,  -1, 2, 0 };
const int   D2_CHORDS_N  = 10;

// Demo 2 nuottitila: Seven Nation Army
const float D2_NOTES[]   = {
   164.81f, 164.81f, 196.00f, 164.81f,   // E3 E3 G3 E3
   146.83f, 130.81f, 123.47f,            // D3 C3 B2
   164.81f, 164.81f, 196.00f, 164.81f,   // E3 E3 G3 E3
   130.81f, 146.83f, 130.81f             // C3 D3 C3
};
const int   D2_NOTES_N   = 14;

#define DEMO_OFF  0
#define DEMO_1    1
#define DEMO_2    2

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

volatile bool chordMode    = true;
volatile int  currentSet   = 0;
volatile int  currentOct   = 1;
volatile int  currentFx    = 0;
volatile bool doReset      = false;
volatile int  demoMode     = DEMO_OFF;
volatile bool demoChanged  = false;
volatile bool muteOpenFlag = false;  // Vaimenna avoin sointu kun fretti painetaan

// Avoimen soinnun ajastin (Core 1)
static unsigned long openChordStartMs = 0;
static bool          openChordActive  = false;

// =============================================================================
// MOZZI-OBJEKTIT — Core 1
// =============================================================================

// osc[0..14]  = frettiosillaattorit (fretti N → osc[N*3..N*3+2])
// osc[15..17] = avoimen soinnun/nuotin omat osillaattorit (ei konflikti frettien kanssa)
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

      // Demo-napit: sama nappi pysäyttää, toinen nappi vaihtaa
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

      // Muut napit eivät toimi demon aikana
      if (demoMode != DEMO_OFF) {
         vTaskDelay(BUTTON_POLL_MS / portTICK_PERIOD_MS);
         continue;
      }

      // Fretit — reunatunnistus vapautukselle
      for (int i = 0; i < NUM_FRETS; i++) {
         bool pressed = digitalRead(FRET_PINS[i]) == LOW;
         if (!pressed && fretHeld[i]) releaseFlags[i] = true;
         fretHeld[i] = pressed;
      }

      // Strum — reunatunnistus laukaisulle
      bool strumEdge = (cStrumD && !strumDLast) || (cStrumU && !strumULast);
      if (strumEdge) {
         if (cStart) {
            // START + STRUM = nollaa kaikki
            doReset = true;
         } else {
            bool any = false;
            for (int i = 0; i < NUM_FRETS; i++) {
               if (fretHeld[i]) { triggerFlags[i] = true; any = true; }
            }
            if (any) {
               // Fretti pohjassa: vaimenna avoin sointu ennen frettisoinnun laukaisua
               muteOpenFlag = true;
            } else {
               // Ei frettiä: soita avoin sointu
               triggerOpen = true;
            }
         }
      }
      strumDLast = cStrumD;
      strumULast = cStrumU;

      // Select: vaihda sointu/nuotti-tila
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

      // D-pad ylös/alas: oktaavi (ei kierrä rajojen yli)
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

// Laukaisee soinnun. fret=-1 käyttää avoimen soinnun omia osillaattoreita.
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

// Sammuttaa soinnun release-vaiheeseen
void releaseChord(int fret) {
   if (fret == -1) {
      for (int n = 0; n < 3; n++) env[OPEN_OSC_BASE + n].noteOff();
      openChordActive = false;
   } else {
      for (int n = 0; n < 3; n++) env[fret * 3 + n].noteOff();
   }
}

// Laukaisee yhden nuotin (nuottitila)
void triggerNote(int fret) {
   osc[fret * 3].setFreq(NOTES[currentOct][fret]);
   env[fret * 3].noteOn();
}

// Sammuttaa nuotin release-vaiheeseen
void releaseNote(int fret) {
   env[fret * 3].noteOff();
}

// Soittaa suoran taajuuden — demotila käyttää nuottitilassa
void triggerFreq(float freq) {
   osc[OPEN_OSC_BASE].setFreq(freq);
   env[OPEN_OSC_BASE].noteOn();
}

// Strum ilman frettiä — avoin sointu tai nuotti
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

// Sammuttaa kaikki äänet välittömästi
void silenceAll() {
   for (int i = 0; i < NUM_OSC; i++) env[i].noteOff();
   openChordActive = false;
}

// Nollaa kaiken oletusarvoihin
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
// DEMOTILAN AJASTIN — Core 1, kutsutaan updateControl:ista
// =============================================================================

static int           demoStep   = 0;
static unsigned long demoNextMs = 0;
static bool          demoIsGap  = false; // true = hiljainen tauko nuottien välillä

void updateDemo() {
   if (demoChanged) {
      silenceAll();
      demoStep    = 0;
      demoNextMs  = millis() + 200;
      demoIsGap   = false;
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
      default: return;
   }

   if (chordMode) {
      // Sointutilassa: ADSR hoitaa luontevan sammumisen — ei erillistä taukoa
      silenceAll();
      triggerChord(chords[demoStep % chordsN]);
      demoStep++;
      demoNextMs = millis() + DEMO_CHORD_MS;
   } else {
      // Nuottitilassa: vuorottele nuotti → hiljainen tauko → nuotti
      if (demoIsGap) {
         // Taukotila ohi — soita seuraava nuotti
         demoIsGap  = false;
         triggerFreq(notes[demoStep % notesN]);
         demoStep++;
         demoNextMs = millis() + DEMO_NOTE_MS;
      } else {
         // Nuotti ohi — sammuta ja siirry taukoon
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

   // WiFi ja Bluetooth pois — ei tarvita, säästää virtaa,
   // vapauttaa ADC2-pinnit ja vähentää äänen häiriöitä
   WiFi.mode(WIFI_OFF);
   btStop();

   // Kaikki napit INPUT_PULLUP — painallus vetää LOW
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
   // PIN_WHAMMY (34) on ADC1 input-only — ei pinMode tarvita

   // ADSR kaikille osillaattoreille
   for (int i = 0; i < NUM_OSC; i++) {
      env[i].setADLevels(ENV_ATTACK_LEVEL, ENV_DECAY_LEVEL);
      env[i].setTimes(ENV_ATTACK_MS, ENV_DECAY_MS, ENV_SUSTAIN_MS, ENV_RELEASE_MS);
   }

   lfo.setFreq(TREMOLO_RATE_HZ);
   ringOsc.setFreq(RING_MOD_FREQ_HZ);

   // Nappitehtävä Core 0:lle — Mozzi käyttää Core 1:tä
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

   // Demo hoitaa äänet kun päällä
   if (demoMode != DEMO_OFF || demoChanged) {
      updateDemo();
      return;
   }

   // Vaimenna avoin sointu kun fretti strumataan (muteOpenFlag asetettu Core 0:ssa)
   if (muteOpenFlag) {
      releaseChord(-1);
      muteOpenFlag = false;
   }

   // Avoimen soinnun automaattinen sammutus ajastimen perusteella
   if (openChordActive && millis() - openChordStartMs > OPEN_CHORD_TIMEOUT) {
      releaseChord(-1);
   }

   // Vapautus-flagit (fretti nostettu)
   for (int i = 0; i < NUM_FRETS; i++) {
      if (releaseFlags[i]) {
         chordMode ? releaseChord(i) : releaseNote(i);
         releaseFlags[i] = false;
      }
   }

   // Laukaisu-flagit (strum painettu fretti pohjassa)
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

   // Whammy — pitch bend 0–WHAMMY_MAX_BEND
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
   // Sama whammy + vibrato kuin freteillä
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
// MOZZI AUDIO LOOP — Core 1, 16384 Hz
// =============================================================================

AudioOutput_t updateAudio() {
   int32_t out = 0;

   for (int i = 0; i < NUM_OSC; i++)
      out += (int32_t)(env[i].next() * osc[i].next()) >> 8;

   out /= AUDIO_SCALER;

   switch (currentFx) {
      case 1:  out = constrain(out * DISTORTION_GAIN, -128, 127); break; // Distortion
      case 2:  out = (int32_t)(out * (128 + lfo.next())) >> 8;    break; // Tremolo
      case 3:  break;                                                      // Vibrato
      case 4:  out = (int32_t)(out * ringOsc.next()) >> 7;         break; // Ring Mod
      default: break;                                                      // Clean
   }

   return StereoOutput::fromNBit(8, out, out);
}

void loop() { audioHook(); }
