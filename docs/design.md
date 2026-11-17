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
