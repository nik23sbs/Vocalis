# Vocalis 2 — da testo a vocal (VST3 / AU)

Scrivi un testo, scegli la voce (Uomo, Donna o Robot), modulala, mettila a tempo sul groove
di un loop, aggiungi gli effetti ed esporta in WAV oppure trascinala nella DAW.

## Installazione su Mac (passo per passo)

1. **Apri il Terminale** (Applicazioni → Utility → Terminale).
2. **Installa gli strumenti di sviluppo Apple** (una sola volta):
   ```
   xcode-select --install
   ```
   Si apre una finestra: clicca *Installa* e attendi la fine.
3. **Installa Homebrew e CMake** (una sola volta). Se non hai Homebrew, segui le istruzioni su https://brew.sh, poi:
   ```
   brew install cmake
   ```
4. **Compila e installa Vocalis**: scompatta lo zip, poi trascina la cartella `Vocalis` nel Terminale dopo aver scritto `cd ` e premi Invio. Infine:
   ```
   ./scripts/build_mac.sh   # scarica anche ONNX Runtime e il modello neurale (~350 MB)
   ```
   La prima volta impiega 5–10 minuti (scarica JUCE ed eSpeak e compila tutto).
5. **Apri la DAW** e fai una nuova scansione dei plugin. Vocalis è uno **strumento**: in Ableton Live
   lo trovi in *Plug-in → Vocalis* e va messo su una traccia MIDI.

## Novita' della versione 2

- **Voce neurale Kokoro**: Sara e Nicola in italiano + oltre 40 voci in altre lingue, **miscelabili** tra loro.
- **Tempi precisi**: le durate dei fonemi arrivano dal modello e i tagli vengono rifiniti sull'audio.
- **La vocale cade sulla griglia** (controllo *Attacco*), come canta un cantante vero.
- **Stretch solo sulle vocali**: consonanti nitide anche a tempi lenti.
- **Pitch, intonazione e formanti** con il vocoder WORLD (qualita' molto superiore).
- **Editor delle sillabe**: sposta, allunga, intona, alza/abbassa o muta ogni sillaba.
- **Dizionario di pronuncia** (`parola = come si legge` oppure `parola = /ipa/`).
- **Catena da studio**: de-esser, doubler, loudness in LUFS, limiter finale.
- Voci robot/formant (eSpeak) sempre disponibili con il pulsante *ROBOT*.

## Come si usa

| Sezione | Cosa fa |
|---|---|
| **Testo** | Scrivi il testo; virgole e punti diventano pause a tempo. Lingua: italiano di default, più altre 13. |
| **Voce** | *Neurale* (Kokoro) o *Robot* (eSpeak). Voce + seconda voce da miscelare, velocita', pitch (semitoni), intonazione (0% = monotona), formanti, robotizza + Robot Hz. |
| **Editor sillabe** | Trascina = sposta, bordo destro = durata, Shift + su/giu' = intonazione, rotella = volume, doppio clic = muta, tasto destro = menu. |
| **Ritmo e groove** | *Griglia*: ogni sillaba parte su uno step (1/4 … 1/32, terzine). Swing, legato, step per sillaba, "Adatta allo step" (stira/accorcia le sillabe). *Libera*: voce naturale. |
| **Groove da sample** | Trascina un loop (wav/aif/flac/mp3) nel riquadro: Vocalis rileva i colpi e sposta le sillabe con lo stesso micro-timing e gli stessi accenti. *Groove* = quanto lo segue, *Accenti* = quanto il volume segue i colpi. |
| **Riproduzione** | *Sync DAW*: suona a tempo con la DAW a partire dalla battuta scelta (con loop opzionale). *Trigger MIDI*: ogni nota fa ripartire il vocal. |
| **Effetti** | Ring mod, distorsione, bitcrusher, EQ, compressore, chorus, delay a tempo (ping-pong), riverbero, volume. Tutti automatizzabili. |
| **Esporta** | *ESPORTA WAV* salva un WAV stereo 24 bit con gli effetti, oppure **trascina la waveform** direttamente su una traccia audio. |

Il vocal si rigenera da solo a ogni modifica (in genere in meno di 100 ms) e segue il BPM del progetto.

## Struttura del codice

```
Source/
  Engine/   ESpeakEngine (sintesi + fonemi), Syllabifier (fonemi → sillabe)
  DSP/      TimeStretch (WSOLA), Quantizer (griglia/swing/groove), GrooveExtractor (onset detection),
            EffectsChain (effetti + robotize)
  Render/   Renderer (thread di generazione, cache)
  UI/       LookAndFeel, WaveformView, GrooveView
  PluginProcessor / PluginEditor
Tests/      VocalisCLI (render da riga di comando), HostTest (simula una DAW)
docs/design.md  progetto e fasi successive
```

## Prossime fasi
2. Voci neurali **Piper** (uomo/donna molto più realistiche).
3. **Cantato via MIDI**: ogni nota canta la sillaba successiva, intonata sulla nota (PSOLA) + modalità vocoder per il robot.
4. Groove via sidechain, preset salvabili.

Licenze: JUCE (GPLv3 / licenza personale), eSpeak-NG (GPLv3). Vocalis è pensato per uso personale.
