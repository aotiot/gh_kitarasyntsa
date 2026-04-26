#!/usr/bin/env python3
"""
Guitar Hero PS3 → ESP32 Mozzi Synthesizer — Python-emulaattori
Windows 11 / Python 3.10+

Asenna riippuvuudet:
    pip install sounddevice numpy keyboard

Käynnistä:
    python python_emu.py

Näppäimet:
    A S D F G     Fretit: Vihreä Punainen Keltainen Sininen Oranssi
    Välilyönti    Strum alas
    Ylänuoli      Strum ylös
    Tab           Select — vaihda sointu ↔ nuotti -tila
    Enter         Start — kierrätä sointusetti
    Enter+Väli    Nollaa kaikki asetukset
    W / X         Oktaavi ylös / alas
    Q / E         Edellinen / seuraava efekti
    Z             Whammy bar — pidä pohjassa, +5 %% pitch bend ylöspäin
    1 / 2         Demo 1 / Demo 2 — käynnistä tai pysäytä
    Esc           Lopeta
"""

import math
import time
import threading
import sys
import tkinter as tk

import numpy as np

try:
    import sounddevice as sd
except ImportError:
    sys.exit("Puuttuu: pip install sounddevice")
try:
    import keyboard as kb
except ImportError:
    sys.exit("Puuttuu: pip install keyboard")

# ── Vakiot — vastaavat koodi.ino:n #define-arvoja ────────────────────────────

SR           = 44100     # Näytetaajuus Hz (ESP32 Mozzi: 16384 — PC:llä 44100 parempi)
CONTROL_RATE = 128       # Kontrollipäivityksiä/s — sama kuin ESP32
CTRL_PERIOD  = SR // CONTROL_RATE  # ~344 näytettä per kontrollipäivitys
BLOCK_SIZE   = 256       # OutputStream-blokin koko näytteinä (~6 ms viive)

AUDIO_SCALER = 20
NUM_OSC      = 18
OPEN_BASE    = 15        # Avoimen soinnun osc-indeksi (15, 16, 17)
NUM_SETS     = 3
NUM_FRETS    = 5
NUM_OCT      = 3
NUM_FX       = 5

ATK_MS, DEC_MS, SUS_MS, REL_MS = 5, 80, 60000, 400
ATK_LVL = 1.0
SUS_LVL = 180 / 255

OPEN_TIMEOUT = 2.0       # Avoimen soinnun automaattinen sammutus (s)
DIST_GAIN    = 6
TREM_HZ      = 4.0
VIB_HZ       = 6.0
VIB_DEPTH    = 0.015
RING_HZ      = 110.0
WHAMMY_MAX   = 0.05

DEMO_CHORD_S  = 0.600
DEMO_NOTE_S   = 0.300
DEMO_GAP_S    = 0.080

SET_NAMES = ["Pop", "Rock", "Balladi"]
OCT_NAMES = ["Basso", "Normaali", "Melodia"]
FX_NAMES  = ["Clean", "Distortion", "Tremolo", "Vibrato", "Ring Mod"]
FRET_NAMES = ["Vihreä", "Punainen", "Keltainen", "Sininen", "Oranssi"]
FRET_COLORS = ["#22cc22", "#dd2222", "#dddd00", "#2266ee", "#ee8800"]

OPEN_CHORD = [
    [196.00, 246.94, 293.66],   # Pop:     G3 B3 D4
    [164.81, 207.65, 246.94],   # Rock:    E3 G#3 B3
    [130.81, 164.81, 196.00],   # Balladi: C3 E3 G3
]
CHORDS = [
    [[130.81,164.81,196.00],[146.83,185.00,220.00],[164.81,196.00,246.94],
     [220.00,261.63,329.63],[174.61,220.00,261.63]],
    [[110.00,138.59,164.81],[123.47,155.56,185.00],[138.59,164.81,207.65],
     [185.00,220.00,277.18],[146.83,185.00,220.00]],
    [[174.61,220.00,261.63],[196.00,246.94,293.66],[220.00,261.63,329.63],
     [146.83,174.61,220.00],[116.54,146.83,174.61]],
]
NOTES = [
    [146.83, 164.81, 174.61, 196.00, 220.00],
    [293.66, 329.63, 349.23, 392.00, 440.00],
    [587.33, 659.25, 698.46, 784.00, 880.00],
]
OPEN_NOTE = [130.81, 261.63, 523.25]

D1_CHORDS = [
    0,-1, 3, 4,  0,-1, 4, 4,
    0,-1, 3, 4,  0,-1, 4, 4,
    3, 4, 0,-1,  3, 4,-1,-1,
    0,-1, 3, 4,  0,-1, 0, 0,
]
D1_CHORDS_N = 32
D1_NOTES = [
    329.63,329.63,349.23,392.00, 392.00,349.23,329.63,293.66,
    261.63,261.63,293.66,329.63, 329.63,293.66,293.66,
    329.63,329.63,349.23,392.00, 392.00,349.23,329.63,293.66,
    261.63,261.63,293.66,329.63, 329.63,293.66,329.63,293.66,
    261.63,261.63,
]
D1_NOTES_N = 33
D2_CHORDS  = [-1,2,0,-1,2,1,0,-1,2,0,2,-1] * 3
D2_CHORDS_N = 36
D2_NOTES = [
    164.81,164.81,196.00,164.81,146.83,130.81,123.47,
    164.81,164.81,196.00,164.81,130.81,146.83,130.81,
    164.81,164.81,196.00,164.81,146.83,130.81,123.47,
    164.81,164.81,196.00,164.81,130.81,146.83,130.81,
    110.00,130.81,146.83,174.61,146.83,130.81,
    164.81,164.81,196.00,164.81,146.83,130.81,123.47,
    164.81,164.81,
]
D2_NOTES_N = 43
DEMO_OFF, DEMO_1, DEMO_2 = 0, 1, 2


# ── ADSR-verhokäyrä ───────────────────────────────────────────────────────────
#
# Vastaa Mozzin ADSR<CONTROL_RATE, AUDIO_RATE>:a.
# next_block(n) generoi n näytteen float32-puskurin arvoilla 0.0–1.0.
# Tilasiirtymät käsitellään puskurin sisällä — ei kutsuvuotoa rajojen yli.

class ADSR:
    IDLE, ATTACK, DECAY, SUSTAIN, RELEASE = 0, 1, 2, 3, 4

    def __init__(self):
        self.state    = self.IDLE
        self.level    = 0.0
        self._sus_cnt = 0
        s = SR / 1000
        self._atk_inc  = ATK_LVL / max(ATK_MS * s, 1)
        self._dec_dec  = (ATK_LVL - SUS_LVL) / max(DEC_MS * s, 1)
        self._rel_dec  = SUS_LVL / max(REL_MS * s, 1)
        self._sus_max  = int(SUS_MS * s)

    def note_on(self):
        self.state    = self.ATTACK
        self.level    = 0.0
        self._sus_cnt = 0

    def note_off(self):
        if self.state != self.IDLE:
            rel_smp = max(REL_MS * SR / 1000, 1)
            self._rel_dec = max(self.level / rel_smp, 1e-10)
            self.state    = self.RELEASE

    def next_block(self, n):
        out     = np.zeros(n, dtype=np.float32)
        level   = self.level
        state   = self.state
        sus_cnt = self._sus_cnt
        for i in range(n):
            if state == self.IDLE:
                break
            elif state == self.ATTACK:
                level += self._atk_inc
                if level >= ATK_LVL:
                    level = ATK_LVL
                    state = self.DECAY
            elif state == self.DECAY:
                level -= self._dec_dec
                if level <= SUS_LVL:
                    level   = SUS_LVL
                    state   = self.SUSTAIN
                    sus_cnt = 0
            elif state == self.SUSTAIN:
                sus_cnt += 1
                if sus_cnt >= self._sus_max:
                    self._rel_dec = max(level / max(REL_MS*SR/1000, 1), 1e-10)
                    state = self.RELEASE
            elif state == self.RELEASE:
                level -= self._rel_dec
                if level <= 0.0:
                    level = 0.0
                    state = self.IDLE
                    break
            out[i] = level
        self.level    = level
        self.state    = state
        self._sus_cnt = sus_cnt
        return out


# ── Siniaaltoosillaattori — phase-accumulator ─────────────────────────────────
#
# next_block(n) generoi n näytettä vektoroidusti numpyllä.
# Arvoalue: -1.0 … 1.0  (ESP32 Mozzi int8: -128 … 127)

class Osc:
    def __init__(self):
        self.phase = 0.0
        self._inc  = 0.0

    def set_freq(self, f):
        self._inc = f / SR

    def next_block(self, n):
        phases     = np.arange(n, dtype=np.float64) * self._inc + self.phase
        self.phase = (self.phase + n * self._inc) % 1.0
        return np.sin(2.0 * math.pi * (phases % 1.0)).astype(np.float32)


# ── Syntetisaattori ───────────────────────────────────────────────────────────
#
# Vastaa ESP32:n updateControl() + updateAudio() yhdistettynä.
# generate_block(n) kutsutaan äänikallbackista SD-säikeessä.
# Tilamuuttujia (trigger_flags, release_flags jne.) kirjoittaa InputHandler-säie —
# sama volatile-malli kuin ESP32:lla; yksittäiset Python-arvojen kirjoitukset
# ovat atomisia GIL:n alla.

class Synth:
    def __init__(self):
        self.chord_mode    = True
        self.current_set   = 0
        self.current_oct   = 1
        self.current_fx    = 0
        self.demo_mode     = DEMO_OFF
        self.demo_changed  = False
        self.do_reset      = False
        self.mute_open     = False
        self.whammy        = 0.0

        self.trigger_flags = [False] * NUM_FRETS
        self.release_flags = [False] * NUM_FRETS
        self.trigger_open  = False

        self._open_active  = False
        self._open_start   = 0.0
        self._demo_step    = 0
        self._demo_next    = 0.0
        self._demo_is_gap  = False

        self.osc = [Osc()  for _ in range(NUM_OSC)]
        self.env = [ADSR() for _ in range(NUM_OSC)]

        # Tremolo LFO: AUDIO_RATE — next_block() kutsutaan generate_block():ssa
        self._trem_lfo = Osc(); self._trem_lfo.set_freq(TREM_HZ)
        # Ring Mod: AUDIO_RATE
        self._ring_osc = Osc(); self._ring_osc.set_freq(RING_HZ)
        # Vibrato LFO: CONTROL_RATE — päivitetään _update_control():ssa manuaalisesti
        # (ESP32:lla Oscil<..., CONTROL_RATE> — phase-increment = freq/CONTROL_RATE per päivitys)
        self._vib_phase = 0.0

        self._ctrl_cnt = 0

    # ── Äänitoiminnot (kutsutaan _update_control:sta) ──

    def _trigger_chord(self, fret):
        if fret == -1:
            for n in range(3):
                self.osc[OPEN_BASE+n].set_freq(OPEN_CHORD[self.current_set][n])
                self.env[OPEN_BASE+n].note_on()
            self._open_active = True
            self._open_start  = time.monotonic()
        else:
            for n in range(3):
                self.osc[fret*3+n].set_freq(CHORDS[self.current_set][fret][n])
                self.env[fret*3+n].note_on()

    def _release_chord(self, fret):
        if fret == -1:
            for n in range(3): self.env[OPEN_BASE+n].note_off()
            self._open_active = False
        else:
            for n in range(3): self.env[fret*3+n].note_off()

    def _trigger_note(self, fret):
        self.osc[fret*3].set_freq(NOTES[self.current_oct][fret])
        self.env[fret*3].note_on()

    def _release_note(self, fret):
        self.env[fret*3].note_off()

    def _trigger_freq(self, freq):
        self.osc[OPEN_BASE].set_freq(freq)
        self.env[OPEN_BASE].note_on()

    def _trigger_open(self):
        if self.chord_mode:
            self._trigger_chord(-1)
        else:
            self.osc[OPEN_BASE].set_freq(OPEN_NOTE[self.current_oct])
            self.env[OPEN_BASE].note_on()
            self._open_active = True
            self._open_start  = time.monotonic()

    def _silence_all(self):
        for e in self.env: e.note_off()
        self._open_active = False

    def _reset_all(self):
        self._silence_all()
        for i in range(NUM_FRETS):
            self.trigger_flags[i] = self.release_flags[i] = False
        self.trigger_open  = False
        self.mute_open     = False
        self.demo_mode     = DEMO_OFF
        self.demo_changed  = False
        self.chord_mode    = True
        self.current_set   = 0
        self.current_oct   = 1
        self.current_fx    = 0
        self.whammy        = 0.0
        self.do_reset      = False
        print("RESET — Sointutila / Pop / Normaali / Clean")

    # ── Demo (vastaa ESP32:n updateDemo()) ──

    def _update_demo(self):
        now = time.monotonic()
        if self.demo_changed:
            self._silence_all()
            self._demo_step   = 0
            self._demo_next   = now + 0.200
            self._demo_is_gap = False
            self.demo_changed = False
            return
        if self.demo_mode == DEMO_OFF or now < self._demo_next:
            return
        if self.demo_mode == DEMO_1:
            chords, chN, notes, noN = D1_CHORDS, D1_CHORDS_N, D1_NOTES, D1_NOTES_N
        else:
            chords, chN, notes, noN = D2_CHORDS, D2_CHORDS_N, D2_NOTES, D2_NOTES_N

        if self.chord_mode:
            self._silence_all()
            self._trigger_chord(chords[self._demo_step % chN])
            self._demo_step += 1
            self._demo_next  = now + DEMO_CHORD_S
        else:
            if self._demo_is_gap:
                self._demo_is_gap = False
                self._trigger_freq(notes[self._demo_step % noN])
                self._demo_step  += 1
                self._demo_next   = now + DEMO_NOTE_S
            else:
                self._silence_all()
                self._demo_is_gap = True
                self._demo_next   = now + DEMO_GAP_S

    # ── Kontrollipäivitys 128 Hz (vastaa ESP32:n updateControl()) ──

    def _update_control(self):
        if self.do_reset:
            self._reset_all()
            return

        if self.demo_mode != DEMO_OFF or self.demo_changed:
            self._update_demo()
            return

        if self.mute_open:
            self._release_chord(-1)
            self.mute_open = False

        if self._open_active and (time.monotonic() - self._open_start) > OPEN_TIMEOUT:
            self._release_chord(-1)

        for i in range(NUM_FRETS):
            if self.release_flags[i]:
                (self._release_chord if self.chord_mode else self._release_note)(i)
                self.release_flags[i] = False

        for i in range(NUM_FRETS):
            if self.trigger_flags[i]:
                (self._trigger_chord if self.chord_mode else self._trigger_note)(i)
                self.trigger_flags[i] = False

        if self.trigger_open:
            self._trigger_open()
            self.trigger_open = False

        # Whammy + vibrato
        bend = self.whammy * WHAMMY_MAX
        # Vibrato LFO: CONTROL_RATE — phase-increment = VIB_HZ / CONTROL_RATE
        vib_val = math.sin(2.0 * math.pi * self._vib_phase)
        self._vib_phase = (self._vib_phase + VIB_HZ / CONTROL_RATE) % 1.0
        vib = VIB_DEPTH * vib_val if self.current_fx == 3 else 0.0

        for i in range(NUM_FRETS):
            for n in range(3):
                base = CHORDS[self.current_set][i][n] if self.chord_mode else \
                       (NOTES[self.current_oct][i] if n == 0 else 0.0)
                if base > 0.0:
                    self.osc[i*3+n].set_freq(base * (1.0 + bend + vib))

        for n in range(3):
            if self._open_active:
                base = OPEN_CHORD[self.current_set][n] if self.chord_mode else \
                       (OPEN_NOTE[self.current_oct] if n == 0 else 0.0)
                if base > 0.0:
                    self.osc[OPEN_BASE+n].set_freq(base * (1.0 + bend + vib))

    # ── Audioblokin generointi (kutsutaan SD-callbackista) ──

    def generate_block(self, n):
        # Kontrollipäivitys CTRL_PERIOD-välein (128 Hz)
        self._ctrl_cnt += n
        while self._ctrl_cnt >= CTRL_PERIOD:
            self._ctrl_cnt -= CTRL_PERIOD
            self._update_control()

        # Kaikki 18 osillaattoria + ADSR yhtäaikaa (numpy-vektorisointi)
        env_arr = np.stack([e.next_block(n) for e in self.env])   # (18, n)
        osc_arr = np.stack([o.next_block(n) for o in self.osc])   # (18, n)
        out = np.sum(env_arr * osc_arr, axis=0) / AUDIO_SCALER    # (n,)

        # Efektit — vastaavat täsmälleen ESP32:n updateAudio():n switch-casea
        fx = self.current_fx
        if fx == 1:   # Distortion: vahvista + leikkaa ±1.0
            np.clip(out * DIST_GAIN, -1.0, 1.0, out=out)
        elif fx == 2: # Tremolo: amplitudin modulaatio (0.5 + 0.5*lfo)
            # ESP32: out * (128 + lfo.next()) >> 8  →  out * (0…1)
            trem = self._trem_lfo.next_block(n)
            out *= 0.5 + 0.5 * trem
        elif fx == 4: # Ring Mod: kerrotaan 110 Hz kantoaallolla
            # ESP32: out * ringOsc.next() >> 7  →  out * (-1…1)
            out *= self._ring_osc.next_block(n)
        # Vibrato (fx==3): hoidettu _update_control:ssa taajuusmodulaationa
        # Clean (fx==0): ei lisätoimia

        np.clip(out, -1.0, 1.0, out=out)
        return out.astype(np.float32)


# ── Näppäimistökäsittely (InputHandler-säie, 10 ms polling) ─────────────────
#
# Vastaa ESP32:n buttonTask():a — lukee nappien tilan ja asettaa synth-flagit.

FRET_KEYS = ['a', 's', 'd', 'f', 'g']

class InputHandler:
    def __init__(self, synth):
        self.synth = synth
        self._fret_held      = [False] * NUM_FRETS
        self._strum_d_last   = False
        self._strum_u_last   = False
        self._select_last    = False
        self._start_last     = False
        self._dpad_up_last   = False
        self._dpad_dn_last   = False
        self._dpad_lt_last   = False
        self._dpad_rt_last   = False
        self._demo1_last     = False
        self._demo2_last     = False
        self._running        = True
        t = threading.Thread(target=self._loop, daemon=True, name="InputHandler")
        t.start()

    def stop(self):
        self._running = False

    def _loop(self):
        s = self.synth
        while self._running:
            # Demo-napit — nouseva reuna toggle
            d1 = kb.is_pressed('1')
            d2 = kb.is_pressed('2')
            if d1 and not self._demo1_last:
                s.demo_mode    = DEMO_OFF if s.demo_mode == DEMO_1 else DEMO_1
                s.demo_changed = True
                print("Demo 1 ON" if s.demo_mode == DEMO_1 else "Demo OFF")
            if d2 and not self._demo2_last:
                s.demo_mode    = DEMO_OFF if s.demo_mode == DEMO_2 else DEMO_2
                s.demo_changed = True
                print("Demo 2 ON" if s.demo_mode == DEMO_2 else "Demo OFF")
            self._demo1_last = d1
            self._demo2_last = d2

            # Whammy: Z-nappi = täysi taivutus (+5 %)
            s.whammy = 1.0 if kb.is_pressed('z') else 0.0

            # Demon aikana muut napit ohitetaan (kuten ESP32:lla)
            if s.demo_mode != DEMO_OFF:
                time.sleep(0.010)
                continue

            # Fretit: seuraa vapautusreunat
            for i, key in enumerate(FRET_KEYS):
                pressed = kb.is_pressed(key)
                if not pressed and self._fret_held[i]:
                    s.release_flags[i] = True
                self._fret_held[i] = pressed

            # Strum: nouseva reuna (ei painettu → painettu)
            sd_   = kb.is_pressed('space')
            su_   = kb.is_pressed('up')
            edge  = (sd_ and not self._strum_d_last) or (su_ and not self._strum_u_last)
            if edge:
                start = kb.is_pressed('enter')
                if start:
                    s.do_reset = True
                else:
                    any_fret = False
                    for i in range(NUM_FRETS):
                        if self._fret_held[i]:
                            s.trigger_flags[i] = True
                            any_fret = True
                    if any_fret:
                        s.mute_open = True
                    else:
                        s.trigger_open = True
            self._strum_d_last = sd_
            self._strum_u_last = su_

            # Select
            sel = kb.is_pressed('tab')
            if sel and not self._select_last:
                s.chord_mode = not s.chord_mode
                print("Tila:", "Sointutila" if s.chord_mode else "Nuottitila")
            self._select_last = sel

            # Start yksin
            start = kb.is_pressed('enter')
            if start and not self._start_last and not sd_ and not su_:
                s.current_set = (s.current_set + 1) % NUM_SETS
                print("Setti:", SET_NAMES[s.current_set])
            self._start_last = start

            # D-pad ylös/alas (oktaavi)
            du = kb.is_pressed('w')
            dd = kb.is_pressed('x')
            if du and not self._dpad_up_last and s.current_oct < NUM_OCT - 1:
                s.current_oct += 1
                print("Oktaavi:", OCT_NAMES[s.current_oct])
            if dd and not self._dpad_dn_last and s.current_oct > 0:
                s.current_oct -= 1
                print("Oktaavi:", OCT_NAMES[s.current_oct])
            self._dpad_up_last = du
            self._dpad_dn_last = dd

            # D-pad vasen/oikea (efekti)
            dl = kb.is_pressed('q')
            dr = kb.is_pressed('e')
            if dl and not self._dpad_lt_last:
                s.current_fx = (s.current_fx + NUM_FX - 1) % NUM_FX
                print("Efekti:", FX_NAMES[s.current_fx])
            if dr and not self._dpad_rt_last:
                s.current_fx = (s.current_fx + 1) % NUM_FX
                print("Efekti:", FX_NAMES[s.current_fx])
            self._dpad_lt_last = dl
            self._dpad_rt_last = dr

            time.sleep(0.010)  # 10 ms polling, sama kuin ESP32:n BUTTON_POLL_MS


# ── Tkinter-käyttöliittymä ────────────────────────────────────────────────────

BG       = "#1a1a1a"   # Pääikkunan tausta
SEC_BG   = "#242424"   # Osion tausta
KEY_BG   = "#444444"   # Näppäinlaatikon tausta (lepotila)
KEY_FG   = "#ffffff"   # Näppäinlaatikon teksti

# Frettien värit: (painettu-tausta, painettu-teksti, lepotila-tausta, lepotila-teksti)
FRET_THEME = [
    ("#00ff55", "#000000", "#005522", "#00ff55"),   # Vihreä
    ("#ff3333", "#ffffff", "#660000", "#ff5555"),   # Punainen
    ("#ffee00", "#000000", "#555200", "#ffee00"),   # Keltainen
    ("#22aaff", "#000000", "#003366", "#44bbff"),   # Sininen
    ("#ff8800", "#000000", "#553300", "#ff9922"),   # Oranssi
]

class UI:
    def __init__(self, root, synth):
        self.root  = root
        self.synth = synth
        root.title("Guitar Hero Syntetisaattori — Python-emulaattori")
        root.configure(bg=BG)
        root.resizable(False, False)

        # ── Tila-palkki ──────────────────────────────────────────────────────
        bar = tk.Frame(root, bg="#2a2a2a", pady=5)
        bar.pack(fill="x")

        self._lbl_mode = self._badge(bar, "SOINTUTILA", "#0088ff")
        self._lbl_set  = self._badge(bar, "POP",        "#00cc33")
        self._lbl_oct  = self._badge(bar, "NORMAALI",   "#ff8800")
        self._lbl_fx   = self._badge(bar, "CLEAN",      "#cc44ff")

        # Demo-indikaattori oikealle
        df = tk.Frame(bar, bg="#2a2a2a"); df.pack(side="right", padx=10)
        tk.Label(df, text="DEMO", bg="#2a2a2a", fg="#aaaaaa",
                 font=("Consolas", 9, "bold")).pack(side="left")
        self._lbl_demo = tk.Label(df, text=" OFF ", bg="#555555", fg="#ffffff",
                                  font=("Consolas", 10, "bold"), padx=6, pady=1)
        self._lbl_demo.pack(side="left", padx=4)

        # ── Frettinappien LED:t ───────────────────────────────────────────────
        fret_sec = tk.Frame(root, bg=SEC_BG, pady=10)
        fret_sec.pack(fill="x", padx=10, pady=(10, 4))

        tk.Label(fret_sec, text="FRETIT", bg=SEC_BG, fg="#ffffff",
                 font=("Consolas", 9, "bold")).pack()

        fret_row = tk.Frame(fret_sec, bg=SEC_BG)
        fret_row.pack(pady=(6, 2))

        self._fret_leds = []
        for i, (key, name, theme) in enumerate(
                zip(FRET_KEYS, FRET_NAMES, FRET_THEME)):
            col_on_bg, col_on_fg, col_off_bg, col_off_fg = theme
            cell = tk.Frame(fret_row, bg=SEC_BG)
            cell.pack(side="left", padx=8)
            led = tk.Label(cell, text=key.upper(), width=4, height=2,
                           bg=col_off_bg, fg=col_off_fg,
                           font=("Consolas", 18, "bold"), relief="flat")
            led.pack()
            tk.Label(cell, text=name, bg=SEC_BG, fg="#cccccc",
                     font=("Consolas", 9)).pack()
            self._fret_leds.append((led, col_on_bg, col_on_fg, col_off_bg, col_off_fg))

        # ── Näppäimistökaavio ─────────────────────────────────────────────────
        key_sec = tk.Frame(root, bg=SEC_BG)
        key_sec.pack(fill="x", padx=10, pady=(4, 10))

        tk.Label(key_sec, text="NÄPPÄIMET", bg=SEC_BG, fg="#ffffff",
                 font=("Consolas", 9, "bold")).pack(anchor="w", padx=6, pady=(6, 2))

        # Rivi 1
        r1 = tk.Frame(key_sec, bg=SEC_BG); r1.pack(fill="x", padx=6, pady=2)
        self._grp(r1, "STRUM",        [("VÄLI", "alas"), ("↑", "ylös")])
        self._div(r1)
        self._grp(r1, "TILA",         [("TAB", "sointu ↔ nuotti")])
        self._div(r1)
        self._grp(r1, "SOINTUSETTI",  [("ENTER", "seuraava")])
        self._div(r1)
        self._grp(r1, "NOLLAA KAIKKI",[("ENTER + VÄLI", "reset")])

        # Rivi 2
        r2 = tk.Frame(key_sec, bg=SEC_BG); r2.pack(fill="x", padx=6, pady=(4, 8))
        self._grp(r2, "OKTAAVI",      [("W", "ylös"), ("X", "alas")])
        self._div(r2)
        self._grp(r2, "EFEKTI",       [("Q", "edellinen"), ("E", "seuraava")])
        self._div(r2)
        self._grp(r2, "WHAMMY +5%",   [("Z", "pidä pohjassa")])
        self._div(r2)
        self._grp(r2, "DEMO",         [("1", "Demo 1"), ("2", "Demo 2")])
        self._div(r2)
        self._grp(r2, "LOPETA",       [("ESC", "")])

        self._poll()

    @staticmethod
    def _badge(parent, text, color):
        lbl = tk.Label(parent, text=text, bg=color, fg="#ffffff",
                       font=("Consolas", 10, "bold"), padx=10, pady=3)
        lbl.pack(side="left", padx=4, pady=4)
        return lbl

    @staticmethod
    def _div(parent):
        tk.Frame(parent, bg="#666666", width=1, height=40).pack(
            side="left", padx=10, fill="y")

    @staticmethod
    def _grp(parent, label, keys):
        """Näppäinryhmä: otsikko + yksi tai useampi key-box funktiolla."""
        grp = tk.Frame(parent, bg=SEC_BG)
        grp.pack(side="left", padx=2)
        if label:
            tk.Label(grp, text=label, bg=SEC_BG, fg="#ffcc00",
                     font=("Consolas", 8, "bold")).pack(anchor="w")
        row = tk.Frame(grp, bg=SEC_BG)
        row.pack()
        for key_txt, fn_txt in keys:
            cell = tk.Frame(row, bg=SEC_BG)
            cell.pack(side="left", padx=3)
            w = max(3, len(key_txt) + 1)
            tk.Label(cell, text=key_txt, bg=KEY_BG, fg=KEY_FG,
                     font=("Consolas", 11, "bold"),
                     width=w, pady=4, relief="raised", bd=2).pack()
            tk.Label(cell, text=fn_txt, bg=SEC_BG, fg="#aaaaaa",
                     font=("Consolas", 8)).pack()

    def _poll(self):
        s = self.synth

        self._lbl_mode.config(text="SOINTUTILA" if s.chord_mode else "NUOTTITILA")
        self._lbl_set.config( text=SET_NAMES[s.current_set].upper())
        self._lbl_oct.config( text=OCT_NAMES[s.current_oct].upper())
        self._lbl_fx.config(  text=FX_NAMES[s.current_fx].upper())

        if s.demo_mode == DEMO_OFF:
            self._lbl_demo.config(text=" OFF ", bg="#555555", fg="#ffffff")
        else:
            self._lbl_demo.config(
                text=f" DEMO {s.demo_mode} ", bg="#dd8800", fg="#ffffff")

        for i, (led, on_bg, on_fg, off_bg, off_fg) in enumerate(self._fret_leds):
            pressed = kb.is_pressed(FRET_KEYS[i]) and s.demo_mode == DEMO_OFF
            led.config(bg=on_bg if pressed else off_bg,
                       fg=on_fg if pressed else off_fg)

        self.root.after(50, self._poll)


# ── Pääohjelma ────────────────────────────────────────────────────────────────

def main():
    synth = Synth()
    inp   = InputHandler(synth)

    stream = sd.OutputStream(
        samplerate = SR,
        channels   = 1,
        dtype      = "float32",
        blocksize  = BLOCK_SIZE,
        callback   = lambda out, frames, t, st: out.__setitem__(
            (slice(None), 0), synth.generate_block(frames)
        ),
    )
    stream.start()

    root = tk.Tk()
    ui   = UI(root, synth)

    def on_close():
        inp.stop()
        stream.stop()
        stream.close()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)

    # Esc sulkee ikkunan
    def check_esc():
        if kb.is_pressed('esc'):
            on_close()
            return
        root.after(100, check_esc)

    root.after(100, check_esc)

    print("Syntetisaattori käynnissä — Sointutila / Pop / Normaali / Clean")
    root.mainloop()
    print("Loppu.")


if __name__ == "__main__":
    main()
