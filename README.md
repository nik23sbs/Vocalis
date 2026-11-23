# Vocalis 3 — da testo a vocal (VST3 / AU)

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

## Novita' della versione 3

- **Voce gia' a tempo, senza tagli**: il modello neurale riceve la durata di ogni fonema e genera la frase intera
  con le vocali sulla griglia (le consonanti restano naturali). Niente micro-silenzi ne' artefatti di stretch.
- **Tre modalita'**: *Parlato* (tempi naturali), *Rap* (griglia, swing, groove), *Canto* (note).
- **Canto neurale**: una nota per sillaba, dalla melodia di un **file MIDI** (trascinalo sulla timeline),
  **registrata dalla DAW** (Canto → *Registra dalla DAW*) o disegnata nel piano roll. Vibrato, glide, vocali sostenute.
- **Armonie e doppio neurali**: terza, quarta, quinta, sesta, ottava (sopra/sotto) nella tonalita' scelta, doppio
  all'unisono aperto in stereo.
- **Sfera voce**: miscela fino a 4 voci trascinando un punto.
- **Varianti**: un'altra interpretazione della stessa frase (stessi tempi), o *Nuova* per provarne una a caso.
- **Editor della sillaba**: nota/intonazione, durata, volume, durata delle consonanti, vibrato, glide,
  sussurrato, ad-lib, muto; **pronuncia per parola** (IPA) direttamente dall'inspector.
- **Macro**: Espressivita', Swing/Flow, Calore, Respiro, Doppio/Coro, Spazio.
- **Nuova grafica "Studio"**: timeline con righello, corsia delle sillabe, piano roll con note,
  curva d'intonazione e forma d'onda.
- **Attacco della frase** (Ritmo): *Sul battere* (predefinito) = la clip e il WAV partono esattamente sulla battuta;
  se le consonanti iniziali non ci stanno, tutta la frase scala di uno step e ogni vocale resta sulla griglia.
  *Anticipato* = le consonanti partono prima del battere e la clip inizia 1 quarto prima (il WAV ha "_pre1q" nel nome).
- **Sposta vocal**: trascina un punto vuoto della timeline (o usa Ritmo > Sposta vocal) per spostare tutta la frase
  a passi di 1/32; doppio clic su un punto vuoto = rimette a zero.

## Come si usa

| Zona | Cosa fa |
|---|---|
| **Barra in alto** | Modalita' Parlato / Rap / Canto, anteprima, posizione e BPM, *Esporta WAV*, *Trascina nella DAW*. |
| **Voci** | Quattro voci (A-D) e la sfera per miscelarle; variante; motore *Neurale* o *Robot* (eSpeak); timbro (velocita', pitch, intonazione, formanti, robotizza). |
| **Testo** | Virgole e punti diventano pause a tempo. Lingua e *Dizionario* di pronuncia. |
| **Timeline** | Clic = seleziona; trascina = sposta (1/32, Alt = libero); bordo destro = durata (le sillabe dopo si spostano); Shift = intonazione; rotella = volume; doppio clic = muta; tasto destro = menu. Nel piano roll trascina una nota (su/giu' = nota, destra/sinistra = posizione). Punto vuoto = sposta tutto il vocal. Trascina qui un file MIDI. Vale in Parlato, Rap e Canto. |
| **Macro** | Sei manopole che agiscono su piu' parametri insieme (al 50% Espressivita' e Coro sono neutre). |
| **Inspector** | *Sillaba* (modifica della selezione + pronuncia), *Ritmo* (griglia, swing, legato, groove da sample, riproduzione), *Canto* (melodia, vibrato, glide, tonalita', armonie, doppio), *Effetti*. |

Il vocal si rigenera da solo a ogni modifica e segue il BPM del progetto.

## Struttura del codice

```
Source/
  Engine/   KokoroEngine (ONNX, durate/F0/energia imposte), KokoroVoices (miscela a 4), TokenSyllables,
            Phonemizer (misaki), ESpeakEngine (robot)
  DSP/      TimingPlanner (vocali sulla griglia, note, anticipo), PitchPlanner (intonazione, note, vibrato,
            armonie), TimeStretch (WSOLA + micro-allineamento), EffectsChain (+ Calore/Spazio), WORLD, Loudness
  Render/   Renderer (cache, catena v2 di riserva), TimedRender (motore v3)
  UI/       Widgets, Timeline, Inspector, LookAndFeel, GrooveView
Tests/      UnitTest, VocalisCLI, HostTest (DAW simulata + istantanea della grafica), tools/ (modello ctrl, server di prova)
docs/       design.md, superpowers/ (progetto e piano della v3)
```

Licenze: JUCE (GPLv3 / licenza personale), eSpeak-NG (GPLv3), Kokoro-82M (Apache 2.0). Vocalis e' pensato per uso personale.
