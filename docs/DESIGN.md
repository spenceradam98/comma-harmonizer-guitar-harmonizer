# Comma: guitar harmonizer / microtonal simulator (design plan, v0.2)

Stack: JUCE 8.0.4, C++17, CMake, VST3 + Standalone, same layout as Ferro Echo and Crate12
(JUCE-free engine with its own tests, thin plugin wrapper). Scalar DSP only, so it runs
on macOS 11 Intel machines without AVX2. About 2% of one core with all four voices.

## Signal flow

```
guitar in -> pitch tracker (YIN) ----------------------------+
          |                                                  | detected pitch
          +-> lead shifter (only when Retune is on) -> DRY   v
          +-> voice 1..4 shifters -> level/pan/delay -> HARMONY -> output
```

- **Pitch tracking:** YIN on a low-passed, decimated (~16 kHz) copy of the input,
  60 Hz to 1.4 kHz, a new reading every ~4 ms, with a level gate. Monophonic: single
  notes and double-stops where one note dominates. Chords are not tracked (see decisions).
- **Pitch shifting:** two-head delay-line shifter with sin² crossfades. The grain length is
  locked to a whole number of the detected period, so the heads stay phase-aligned. This
  removes most of the warble cheap shifters have, keeps latency low (~11-25 ms on the
  harmony, 0 on the dry signal when Retune is off) and is light on CPU.

## Harmony

Each of the 4 voices has: on, mode, interval, detune (±100 cents), level, pan, delay
(0-150 ms, for doubling and slap) plus a global Humanize (slow random pitch and timing drift).

- **Interval mode:** a fixed shift in semitones (±24), like a classic pitch shifter.
- **Scale steps mode (intelligent harmony):** the played note is placed on the nearest
  degree of the chosen key/scale and the voice moves N scale steps from it, so "3rd up" is
  a major or minor third as the key requires. Works in every scale, microtonal ones included.
- **Glide** smooths interval changes between notes.

## Microtonal

One concept covers everything: a scale is a list of pitches in cents with a period,
the same as a Scala `.scl` file. 40 built-in scales: 15 common 12-TET scales and modes,
7 just-intonation scales (5-limit major/minor/chromatic, Pythagorean, septimal blues,
harmonic series, Partch otonal hexad), 9 equal divisions (24-EDO quarter tones, 17, 19, 22,
31, 53-EDO, Bohlen-Pierce, Carlos Alpha and Beta), 9 world / quarter-tone scales (maqam
Rast, Bayati, Saba, Hijaz, Sikah, neutral major, slendro, pelog, raga Todi). **Load .scl**
loads any Scala file, saved inside the DAW session.

**Retune** modes act on the dry guitar (and the harmonies follow it):

- **Snap to Scale:** each note is pulled to the nearest pitch of the tuning, by Amount and
  Speed. Play a normal guitar in just intonation or with maqam quarter tones. Bends and
  vibrato survive when Speed is above 0.
- **Remap Frets:** each fret above the key's root becomes the next step of the tuning, so a
  standard guitar behaves like a microtonal fretboard (24-EDO = quarter-tone guitar,
  19-EDO fretboard, Carlos Alpha, Bohlen-Pierce).

## MIDI control (v0.2)

Comma takes MIDI input (route a keyboard or a MIDI track to it in the DAW). The **MIDI** menu:

- **MIDI Off:** ignored.
- **MIDI Sets Key:** the last note pressed sets the key, so you can change key per song
  section from a MIDI clip. The scale stays as chosen.
- **MIDI Plays Harmony:** voices 1-4 sing the notes you hold, lowest note on voice 1 (the four
  most recent notes if you hold more). A voice with no note held is silent. Level, pan, delay,
  detune and Glide (as portamento) still apply. Notes go through the tuning the same way
  frets do, so with Snap or Remap a keyboard plays the microtonal scale.

## UI

Header with preset browser (23 factory presets in Harmony, Intervals and Microtonal
groups) and theme picker. Pitch panel: note readout and a **tuning wheel** showing the
scale's steps on a circle, the incoming note, the retuned note and the four voices live.
Key & Tuning panel, Mix panel with meters, and four colour-coded voice strips.
Six themes: Midnight, Sunburst, Neon Fret, Stompbox, Studio Grey, Manuscript. Each
changes the knob style as well as the colours. Theme is saved with the session.

## Not in v0.1 (candidates for next steps)

- Formant preservation (big intervals sound "chipmunk"/"giant"), and a higher-quality
  PSOLA shifter mode.
- Polyphonic (chord) harmonizing, which needs a different, heavier approach. On the roadmap,
  not planned for the next version.
- Resizable UI, user preset save/load, .kbm keyboard mapping files, AU format.
