# Audio to MIDI: arranging a song from a recording

How to turn an MP3 (or any recording) into a MIDI file that
[song-playbook.md](song-playbook.md) can arrange for the bells. First done
2026-10-20 with Blink-182's "All the Small Things":
`tools/songs/small_things_mp3_rom.h`.

The script `minstrel-audio-to-midi` does the transcription. Its
output has the same shape as a hand-made MIDI: track 1 is the sung melody,
track 2 holds the chords. From there, the playbook takes over at step 2.

**Prefer a MIDI if one exists.** A transcription from audio is an
approximation. For All the Small Things, 72% of the notes in a published MIDI's
vocal track showed up, in order, in the transcription.

## Setup (once)

The tools need PyTorch, which has no wheels for the system Python 3.14. They
go in a Python 3.12 venv at `.venv-audio/` (git-ignored, about 3 GB), with the
`tools` package installed editable and its `audio` extra:

```bash
uv venv -p 3.12 .venv-audio
VIRTUAL_ENV=.venv-audio uv pip install -e 'tools[all]'
# basic-pitch pins TensorFlow < 2.15.1, which has no 3.12 wheels. Version 0.4
# also runs its model on ONNX, so install it without its dependencies:
VIRTUAL_ENV=.venv-audio uv pip install --no-deps basic-pitch==0.4.0
```

Tested with torch 2.14.1 (CUDA, RTX 3060 Ti), demucs 4.1.0, librosa 1.0.0,
basic-pitch 0.4.0 and onnxruntime 1.31.0. Basic Pitch prints warnings that
TensorFlow and tflite aren't installed; ignore them. The first run downloads
the Demucs model weights.

## 1. Transcribe

Copy the downloaded file into its own directory first, as with any download.

```bash
minstrel-audio-to-midi SONG.mp3 \
  --work media/audio/<song> --output media/audio/<song>/<song>.mid
```

It takes about 25 s on the GPU. Stems, beats and the raw vocal notes are cached
in `--work`, so re-running with other `--span`/`--gate` values takes seconds.
Delete `beats.npy` or `vocal_notes.npy` there to redo those steps.

Read what it prints. Here is All the Small Things:

```
separated 168.0 s into stems on cuda
tempo 148.72 BPM (202 ms eighths), 378 beats 0.36-153.09 s, local beat 398-409 ms
melody: 265 notes from 613 events (75 bleed, 25 harmony dips dropped)
key C major; chords: {'C': 81, 'G': 79, 'F': 46, 'Em': 1}
melody range G3-F4, first note at 14.5 s
```

- **Tempo:** the median beat, which becomes the ROM frame time. "Local beat"
  shows how far the drummer drifts. If the BPM is half or double what you
  hear, the tracker locked onto the wrong level, and the melody grid will be
  wrong too.
- **Beats span:** times outside the tracked beats continue at the median
  beat, so an outro without drums is still placed.
- **Melody notes:** about one per sung syllable is right. "Bleed" counts notes
  dropped because the vocal stem was nearly silent. A count that is high
  compared with the notes kept is fine; that's guitar in the instrumental
  parts.
- **Key and chords:** should be the song's real chords. Chords come only
  from the detected major key's six diatonic triads (I ii iii IV V vi), so a
  song that borrows chords from outside the key gets the nearest diatonic one.
  The detector can pick a neighbouring key, especially for a song in a minor
  key. Check the chord names against the song. If they're off, pass
  `--key` with the relative major's tonic, e.g. `--key C#` for B♭ minor. Only
  the chords are redone, so it takes seconds.
- **Intro:** the melody starts at the first sung note, so an instrumental
  intro is silent. If the intro carries a hook, pass `--intro`: until the
  vocals start, the melody is the top note per eighth of the other stem
  (guitars, keys), kept within `--span` of the vocal register. That stem's
  notes are cached as `other_notes.npy`.
- **Instrumental music:** pass `--melody-stem other` to take the melody from
  the instruments. If stray accompaniment notes come before the melody
  enters, `--melody-from SECONDS` drops them.
- **Not 4/4:** pass `--beats-per-bar 3` for a waltz. Then bars are tracked on
  the bass stem, which plays only on the downbeat. Each bar is split into 3
  beats and gets one chord, and the MIDI starts on a downbeat, so
  `harmonize.py --chord-every 3` puts chords on beat 1. Check the tempo
  against an autocorrelation of the mix's onset strength if in doubt: the
  beat tracker locked Waltz No. 2 onto 1.5 beats.
- **Range:** should be a plausible singing range, about an octave. Two octaves
  or more means the harmony voice or octave errors got in: lower `--span`.
- **Verses an octave apart:** if a whole section is missing from the melody,
  check the raw `vocal_notes.npy` pitches per section. A singer who drops an
  octave for the verses sits outside `--span` of the song's median. Pass
  `--octave-window 4`: the span is centred on the median of the sung notes
  within 4 s, and each note moves by whole octaves to the song's register.
  The count "moved an octave" says how many notes it shifted.

Then go to [song-playbook.md](song-playbook.md) step 1 and run
`harmonize.py --analyze` on the MIDI. Use `--melody-track 1 --harmony-track 2`
and don't use `--intro`, because the chord track has no melody voice to lead
in with.

## How it works, and why

1. **Separate the stems (Demucs `htdemucs`).** A punk mix has distorted
   guitar in the same range as the voice, so a pitch tracker run on the full
   mix follows the guitar. Demucs splits the mix into drums, bass, other and
   vocals stems. Newer torchaudio can't save WAVs without torchcodec, so the
   script writes them with soundfile.
2. **Beat grid from the drum stem.** The ROM plays fixed frames, but a live
   band doesn't keep a fixed tempo. In All the Small Things, 30-second
   stretches average 399–409 ms beats. Against one constant tempo, notes ended
   up 230 ms off, more than an eighth. So the grid follows librosa's tracked
   beats, two eighth-note slots per beat, and fills any dropped beat by
   interpolation.

   Don't fit a single constant tempo instead. Its score peak was only a few
   ms wide in phase, so the "best" tempo changed with the search step size
   (146.4, 146.5 or 147.3 BPM).
3. **Melody (Basic Pitch on the vocal stem).** The vocal stem holds every
   singer. In the choruses there's a lower harmony part under the lead.
   - A single-pitch tracker (pYIN) gives low confidence on stacked voices
     (median 0.04), and then follows the lower voice. Only 30–52% of the
     reference melody matched.
   - Basic Pitch transcribes several notes at once, so the script keeps the
     highest note per slot within `--span` semitones (default 7) of the
     stem's median pitch. That matched 72–73%.
   - Two filters clean it up. Notes where the vocal stem's level is under
     `--gate` (default 0.3) of its loud level are bleed: Basic Pitch
     normalizes its input, so it transcribes faint guitar at full strength.
     And a one-slot note that dips 3+ semitones below both neighbours is the
     harmony voice showing through a gap in the lead. It's folded into the
     note before it.
4. **Chords from the bass and other stems.** For each half bar, the script
   averages the chroma of bass + other and scores each diatonic triad, with
   the bass note counting extra for the root. The bass counts because the
   guitars here play power chords, which have no third. The key comes from a
   major-key profile matched against the whole song's chroma.

## What's left to improve

- **Rhythm is quantized to eighths.** Sixteenth-note pickups land on the
  nearest eighth, and two notes in one slot keep only the higher one.
- **Scoops show up as grace notes.** A singer sliding up into a note
  sometimes gives an extra one-slot note a step above or below.
- **The real singer adds syllables.** About 90 notes in the transcription
  aren't in the published MIDI. Most are real sung syllables that the MIDI
  simplifies away. At bell speed, the 800 ms re-strike gap drops many of the
  repeats anyway.
- **To score a transcription,** compare it with a MIDI of the same song if
  one exists. Use the note sequence (difflib's `SequenceMatcher` on the
  onset pitches); the tempos usually differ, so don't compare times.

## Results

| Song | Source | MIDI notes | Matched vs published MIDI | ROM |
|------|--------|-----------|---------------------------|-----|
| All the Small Things | MP3, 168 s | 265 melody, 207 chords | 170/237 (72%) | 719 frames at 202 ms, 217 strikes (36 chords), `--transpose 31 --min-gap-ms 800` |
| Night Tapes, "television" | MP3, 212 s | 346 melody, 582 chords | no reference MIDI | 720 frames at 263 ms, 251 strikes (28 chords), `--transpose 20 --min-gap-ms 600` |
| Daft Punk, "Instant Crush" | MP3, 338 s, `--key C# --intro` | 602 melody (86 intro), 930 chords | no reference MIDI | 1100 frames at 273 ms, 593 strikes (160 chords), `--transpose 30 --min-gap-ms 273` |
| Shostakovich, Waltz No. 2 | MP3, 230 s, `--melody-stem other --beats-per-bar 3 --melody-from 6` | 766 melody, 239 chords | no reference MIDI | 1355 frames at 159 ms, 623 strikes (155 chords), `--transpose 28 --chord-every 3 --strum-intro x.x.x.` |
| Gotye, "Somebody That I Used to Know" | MP3, 244 s, `--key F`, melody from pYIN (`media/audio/somebody/scripts/pyin_midi.py`), intro riff by hand | 411 melody (60 intro riff, 316 pYIN, 35 Basic Pitch fill), 774 chords | no reference MIDI | 1041 frames at 232 ms, 411 strikes (66 chords), `--transpose 24 --min-gap-ms 0` |
| Daft Punk, "Get Lucky" | MP3, 249 s, `--key A` | 364 melody, 723 chords | no reference MIDI | 1737 frames at 129 ms, 393 strikes (114 chords), `--transpose 32 --frames-per-quarter 4 --strum-intro x......x..xx..x. --min-gap-ms 258` |
| Coldplay, "Clocks" | MP3, 256 s, `--key G#`, piano riff by hand (`media/audio/clocks/scripts/riff_midi.py`) | 659 melody (384 riff, 275 vocal), 771 chords | no reference MIDI | 1098 frames at 229 ms, 652 strikes (105 chords), `--transpose 33 --min-gap-ms 229` |

Instant Crush notes: the key was detected as E♭ major, but the stems are
D♭ major / B♭ minor, hence `--key C#`. The intro hook is repeated eighths
(E♭ E♭ E♭ D♭ · D♭ D♭ C C C). Any longer gap turns it into a chord-tone
arpeggio, so the minimum gap is down to 546 ms re-strikes, below what composing.md
measured as safe. Expect more weak strikes. The fallback is the default
`--min-gap-ms 400`, which re-strikes at 819 ms.
By ear (2026-10-20), the 546 ms version plays very well. It has no microphone take yet.

Get Lucky notes: detected as D major, which turned the loop's E major into
Em. The loop Bm–D–F#m–E is ii–IV–vi–V in A major, hence `--key A`. A major
doesn't fit the bells, so `--transpose 32` plays it in F major (D6–E7). 30 of
the 364 melody notes are snapped, mostly C naturals and D#s, which look like
scoops and blue notes rather than the tune.

The intro hook is Nile Rodgers' strummed guitar, not a tune. `--intro` gives
its top note, which is F# almost throughout, so it plays as one repeated bell.
Instead, the intro uses harmonize.py's `--strum-intro`. Onset strength of the
other stem on a 16th grid put the strum's accents at 16ths 0, 7, 10, 11 and
14 of each bar (1, 2a, 3&, 3a, 4&). That needs 16th frames, so the whole song
uses `--frames-per-quarter 4`. At 2 bells per hit and the default 400 ms gap
(645 ms re-strikes), the 3a hit usually found no free bell, because Gm, B♭
and Dm have only four bells each. `--min-gap-ms 258` (387 ms re-strikes)
fills every hit. That's below the 546 ms Instant Crush plays well at, so
expect weak strikes; the fallback is `--min-gap-ms 387` (516 ms).
By ear (2026-10-20), the 387 ms version sounds fine. It has no microphone take yet.

Somebody That I Used to Know notes: Gotye sings the verses around D3–A3 and
the choruses around F4–A4. The default span kept only the choruses (269
notes, verses nearly empty), hence `--octave-window 4`. The key was detected
as C major, giving Dm–C but no B♭. The melody has B♭ and almost no B, so it is
D minor, hence `--key F` (Dm–C, B♭–C in the chorus). It plays in D minor
(`--transpose 24`, two octaves up). The range G3–C5 is 17 semitones, one more
than the bells, so 9 notes fold, all low notes in the outro (210–234 s).
17 notes snap, mostly G# and C#, which look like scoops. There is also a lot
of F# (19 notes) in the verses; the recording is about 20 cents flat, which
may push sung Gs down.

`--intro` missed the intro hook. The hook is the plucked guitar riff (the
Luiz Bonfá "Seville" sample) around C3–A3, an octave below the vocal span,
so the other stem's top line picked up stray high notes instead. Basic Pitch
also scattered it. Averaging the other stem's CQT onset energy per eighth
position over the intro bars reads it cleanly: per bar, eighths 2–4 and 6–8
are F D C · G C D for two bars, then A D C · G C D (a Dm pluck with A on top)
until the vocals enter after bar 10. `media/audio/somebody/scripts/riff_midi.py`
writes that riff an octave up into the melody, ahead of the vocals, and makes
`somebody_riff.mid`. On the bells it plays F6/A6 D6 C6 · G6 C6 D6.

By ear (2026-10-20), the user found the 696 ms and 464 ms re-strike versions
still bad and asked for no re-strike limit at all. That was still "so
wrong" that they asked whether the bell mapping was mixed up. It wasn't: the
ROM matched the MIDI note for note. The Basic Pitch melody was the problem.
Gotye's verses are quiet, so most of their notes fell under `--gate` 0.3 and
the verses came out as fragments. The rest split into F#/G# scoops that
clash with D minor.

pYIN, which failed on All the Small Things' stacked voices, reads the solo
verses cleanly and continuously ("Now and then I think of when we were
together": G G G A C G G F F · E D). `pyin_midi.py` takes the median pYIN
pitch per eighth slot, corrected for the recording being 22 cents flat, with
a gate of 0.08. It snaps chromatic notes to D minor by their measured pitch,
folds one-slot chromatic scoops into the next note, and starts a new strike
on a pitch change, a gap or a vocal-stem onset. Sections move by octaves to
a G4 centre and then into C4–E5. Basic Pitch fills only where pYIN loses the
voice, mostly the duet harmonies at 185–200 s. That leaves no snapped or
folded notes in harmonize.py. If this works by ear, it belongs in
`audio_to_midi.py` as an option.

Before that: `--min-gap-ms 0` now
allows a bell on consecutive frames (232 ms here); before, harmonize.py
always kept at least one frame between strikes. Every melody note is struck.
Not yet played on the bells.

Clocks notes: the key was detected as B♭ major, giving B♭ instead of B♭m.
The loop is E♭ | B♭m | B♭m | Fm, which is V–ii–ii–vi in A♭ major, hence
`--key G#`. The hook is the piano's 3+3+2 arpeggio (E♭5 B♭4 G4 E♭5 B♭4 G4 E♭5
B♭4 per bar). `--intro` gave its pitches but scrambled their order, so
`riff_midi.py` writes it by hand from 6.13 s, where the other stem's top notes
place bar 1, into every bar without vocals: the intro, the breaks at bars
40–47 and 96–103, and the outro (E♭ | B♭m | B♭m A♭ | A♭). F major is the only
bell key where the riff fits whole: C7 G6 E6 · B♭6 G6 D6 · A6 F6 D6. In C or
G major its range lands on missing bells. The riff re-strikes G6 two eighths
apart across each bar line, so `--min-gap-ms 229` (458 ms); at the default
400, 38 riff notes go missing.

At `--transpose 33` the vocals (A♭3–A♭4) sit at F6–F7, so the chorus peak
A♭4 needs F7, one bell too high. Folded, "G G♯ G♯ F E♭" plays E7 F6 F6 D7 C7.
`clocks_rom.h` instead drops each run of notes from E♭4 up around an A♭4 an
octave (93 notes), so it plays E6 F6 F6 D6 C6. The chorus then sits below
the verses. `clocks_fold_rom.h` keeps the plain fold for comparison. 31 notes
snap, mostly B and D/E naturals, which look like scoops.

Not yet played on the bells.

Waltz No. 2 notes: there are no vocals, so the melody comes from the other
stem (sax, then strings). The drum-stem beat tracker found 127 BPM with
328–647 ms beats, about 1.5 real beats. Onset autocorrelation showed a
0.322 s beat and a 0.966 s bar (the bass plays only on beat 1), hence
`--beats-per-bar 3`. The key is C minor (detected as E♭ major, correct). It plays
in E minor (D6–E7). Its V chord, B major, needs a D#, which the bells lack.
The diatonic chord set makes it Bm, and the 39 snapped melody notes are mostly
that leading tone. The other stem also holds the accompaniment's
"pah-pah", so some sustained notes come out as repeated eighths (e.g. bars
43–59). harmonize drops most of those repeats. The intro is struck as
oom-pah-pah chords.

Not yet played on the bells. After a microphone take, add its counts here and
a row in [composing.md](composing.md).
