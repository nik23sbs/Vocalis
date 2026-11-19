# Vocalis — design

Plugin (VST3 / AU / Standalone, macOS come target principale) che trasforma un testo in un vocal,
con scelta della voce, modulazione, effetti, quantizzazione ritmica sul groove ed export WAV.
Uso personale: JUCE con licenza personale/GPL, eSpeak-NG (GPLv3) e Piper (MIT) senza vincoli.

## Obiettivi
- Testo → voce parlata, voci Uomo / Donna / Robot + tutte le varianti eSpeak, più lingue (default italiano).
- Modulazione: pitch, intonazione, velocità, formanti (carattere), volume.
- Ritmo: sillabe sulla griglia (1/4…1/32, terzine), swing, groove estratto da un sample audio trascinato.
- Effetti: robot (ring-mod + robotize), bitcrusher, distorsione, chorus, delay a tempo, riverbero, EQ, compressore.
- Riproduzione sincronizzata alla DAW (dalla battuta X, opzionale loop) oppure trigger da nota MIDI.
- Export WAV e drag&drop della clip nella DAW.

## Non obiettivi (per ora)
- Sintesi in tempo reale nota per nota; la voce è sempre una clip renderizzata.
- Lettura del groove pool della DAW (le API dei plugin non lo espongono).

## Approcci considerati
1. **Clip renderizzata offline + playback sincronizzato** (scelto): il testo viene sintetizzato in background,
   segmentato in sillabe, quantizzato, processato dagli effetti e tenuto in memoria. Robusto, export identico all'ascolto.
2. Sintesi live per blocco: impossibile con motori neurali e con la quantizzazione che deve "vedere" tutta la frase.
3. Plugin che chiama programmi esterni (CLI): più semplice ma fragile e bloccato dalle sandbox delle DAW.

## Architettura
- `VoiceEngine` (interfaccia) → `ESpeakEngine` (fase 1), `PiperEngine` (fase 2).
  Restituisce audio mono + lista fonemi con tempi. La sillabazione raggruppa i fonemi attorno ai nuclei vocalici.
- Formanti: l'audio viene ricampionato di un fattore r; velocità e pitch di eSpeak sono pre-compensati,
  così cambia solo il "timbro".
- `Quantizer`: ogni sillaba occupa N step; pause/punteggiatura diventano pause; time-stretch WSOLA per riempire lo step.
- `GrooveExtractor`: onset detection (spectral flux) sul sample, confronto con la griglia al BPM → offset e accento per step.
- `EffectsChain`: applicata al render, parametri automatizzabili (variazione → re-render veloce solo degli effetti).
- Stato salvato nel progetto: testo, parametri, groove.

## Fasi
1. eSpeak, voci, modulazione, ritmo + groove da sample, effetti, playback, export (questa fase).
2. Voci neurali Piper (onnxruntime) per Uomo/Donna realistiche.
3. Cantato via MIDI: sillabe sulle note, intonate con PSOLA; modalità vocoder/talkbox per il robot.
4. Groove via sidechain, preset salvabili, rifiniture per la distribuzione su Mac (firma).

## Lasciato al costruttore
Layout preciso dell'interfaccia, valori di default, algoritmo esatto di onset detection.

---

# Vocalis 2 — motore professionale (2026-11-19)

Approccio scelto: **A — Kokoro-82M** (voce neurale, Apache 2.0) + DSP rifatto. Alternative scartate per ora:
Kokoro + Piper (piu' voci italiane ma installer piu' pesante), Chatterbox (clonazione voce, 0,8–3 GB e lento su CPU).

Catena: testo → dizionario → fonetica eSpeak IPA con legature + regole misaki (identica all'addestramento di Kokoro)
→ Kokoro (ONNX Runtime, 24 kHz) con **durate per fonema** (frame da 25 ms) → sillabe con nucleo vocalico,
tagli rifiniti su minimi di energia e zero crossing, attacco vocale sul punto di massima salita d'energia,
doppie italiane divise a meta' → WORLD solo se servono pitch/intonazione/formanti/pitch per sillaba
→ robotizza → quantizzazione: **la vocale (P-center) cade sulla griglia**, **stretch solo del nucleo** (consonanti intatte),
modifiche manuali per sillaba → ricampionamento → **loudness LUFS (BS.1770)**.
Effetti in tempo reale: de-esser, ring, distorsione, bitcrusher, EQ, compressore, doubler, chorus, delay, riverbero, limiter.

Scelte del costruttore (da verificare con l'uso):
- la prima sillaba di una clip parte sul battere (non c'e' pre-roll prima della battuta di partenza);
- il nucleo si allunga al massimo 6x, oltre resta una pausa;
- modello fp32 (326 MB) per la massima qualita'; installato una sola volta in /Library/Application Support/Vocalis/models;
- ONNX Runtime 1.23 (ultima serie con macOS Intel), dylib dentro ogni bundle (Contents/Frameworks).

# Vocalis 3 — voce a tempo senza tagli (2026-11-19)

Progetto completo: `docs/superpowers/specs/2026-11-19-vocalis3-design.md`, piano: `docs/superpowers/plans/2026-11-19-vocalis3.md`.

- Il grafo ONNX di Kokoro viene modificato (`Tests/tools/kokoro_controllable.py`): ingressi `dur_override`, `f0_override`,
  `n_override` (+ `use_*`), uscite `f0` e `n`. Senza `use_*` il modello e' identico all'originale.
- Prima passata: durate e intonazione predette. `TimingPlanner` mette ogni vocale sulla griglia (o sulla nota),
  le consonanti tengono la durata naturale (nel Canto si accorciano), arrotondamento al frame da 25 ms con
  diffusione dell'errore; poi un warp WSOLA continuo (rapporti ~ +-10 %) porta le vocali al campione giusto (+-5 ms).
- Seconda passata: audio a tempo + F0 naturale. `PitchPlanner` costruisce la F0 finale (intonazione, modifiche,
  note con glide e vibrato, sussurrato = F0 a zero); nel Canto l'energia N viene sostenuta sulle vocali lunghe.
- Armonie e doppio = altre passate con le stesse durate e la F0 spostata (diatonica nella tonalita'), aperte in stereo.
- Se il modello installato non e' controllabile (o manca), resta la catena della v2 (Quantizer + WORLD) ed eSpeak.
