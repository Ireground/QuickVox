# QuickVox

A simple vocoder VST3. Put it on a vocal track and you're done: no sidechain routing.
The track's own audio is the modulator; QuickVox makes its own synth carrier.

## Carrier
- **FOLLOW VOICE**: the carrier follows the pitch of your voice (SNAP ON = robotic steps on notes, OFF = smooth).
- **FIXED NOTE**: classic monotone robot on one note (drag / mouse wheel on the note box, double-click = C3).
- **MIDI**: put MIDI notes/chords on the same track; the vocoder plays them.
- **CHORD**: OFF / OCT (adds an octave below) / 5TH / MAJ / MIN (Follow and Fixed Note modes).

## Sound
- **BANDS**: 8–40. Fewer = lo-fi/robotic, more = clearer words.
- **FORMANT**: shifts the vowel character down (bigger/darker) or up (smaller/brighter).
- **AIR**: lets the original consonants and "s" sounds through for intelligibility.
- **RESPONSE**: how fast the bands follow the voice (short = tight and choppy, long = smooth/smeared).
- **THICK**: detuned extra saws for a wider, fatter carrier.

## Output
- **MIX** dry/wet, **OUTPUT** gain.

Sliders: drag (Shift = fine), mouse wheel nudge, double-click to reset.

## Build
GitHub Actions builds the Windows VST3 on every push (Actions tab → latest run → `QuickVox-VST3-Windows`).
