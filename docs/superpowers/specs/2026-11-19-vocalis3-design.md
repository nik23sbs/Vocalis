# Vocalis 3 — design

Approvato dall'utente il 2026-11-19 ("aggiungi tutte le opzioni consigliate, risolvi il motore, grafica A").

## Perché
La v2 genera la frase, la taglia in sillabe e le sposta/stira sulla griglia: tagli udibili, micro-silenzi,
artefatti WSOLA, prima sillaba in ritardo. L'utente la sente "imprecisa". Vuole anche le funzioni dei
generatori commerciali (SynthV, ACE Studio, VOCALOID, Emvoice, Vocoflex) e una grafica moderna (opzione A "Studio").

## Obiettivi
1. Motore "a tempo": Kokoro con durate imposte (vocali sulla griglia, consonanti naturali), nessun taglio.
2. Modalità Parlato / Rap / Canto.
3. Canto neurale da note (F0 imposta), note per sillaba, file MIDI trascinabile, cattura MIDI dalla DAW.
4. Editor note + curva d'intonazione, vibrato e glide per sillaba, stile sillaba (vibrato/glide/sussurrato/ad-lib).
5. Varianti (riprese): 3+ interpretazioni della stessa frase scelte con un seme.
6. Armonie / doppi / coro neurali (terza, quinta, ottava diatoniche, doppio all'unisono).
7. Sfera voce: miscela bilineare di 4 voci neurali (pad XY).
8. Macro: Espressività, Flow, Calore, Respiro, Coro, Spazio.
9. Editor pronuncia per parola (scrive nel dizionario) e durata delle consonanti per sillaba.
10. Grafica A: header con modalità e trasporto, libreria voci + sfera a sinistra, testo + timeline
    (righello, corsia sillabe, piano roll con curva e forma d'onda) al centro, macro in basso,
    inspector a schede (Sillaba / Ritmo / Armonie / Effetti) e meter a destra.

## Non obiettivi
- Clonazione della voce, nuove lingue oltre a quelle di Kokoro/eSpeak, ARA, Windows.
- Disegno libero a mano della curva di pitch (si modifica per sillaba: nota, glide, vibrato).

## Approcci considerati
- **A (scelto)**: modello ONNX modificato con ingressi `dur_override`/`f0_override` e uscita `f0`; durate
  calcolate in frame (25 ms) con diffusione dell'errore, poi un warp WSOLA continuo (rapporti ~±10%, senza
  tagli) porta ogni vocale sul campione esatto. Pitch/intonazione/canto/armonie via F0 neurale.
- B: tenere taglia-e-stira migliorando le dissolvenze. Scartato: il limite è strutturale.
- C: modello cantante dedicato (DiffSinger ecc.). Scartato: pesante, voci/licenze, niente italiano.

## Decisioni (mie, segnalate all'utente)
- Il modello installato è `kokoro-v1.0-ctrl.onnx`, generato in CI da `kokoro_controllable.py`
  (il motore accetta anche il modello originale, senza controllo dei tempi → ritorno al vecchio Quantizer).
- Anticipo (preroll): se le consonanti iniziali precedono il battere, la clip parte 1 quarto prima;
  in Sync DAW lo gestisce il plugin; il WAV esportato lo dichiara nel nome ("_pre1q").
- La sfera miscela 4 voci neurali; il robot resta un motore/una macro, non un angolo della sfera.
- Sussurrato = F0 a zero sulla sillaba (generatore non intonato di Kokoro). Ad-lib = copia della sillaba
  un'ottava sopra, ritardata di 1/8 e aperta di lato.
- Respiro = rumore filtrato con inviluppo inserito prima delle frasi (nelle pause di punteggiatura).
- eSpeak (robot/formant) usa ancora la catena v2 (Quantizer + WORLD).
- Clip stereo: voce principale al centro, armonie/doppi aperti.

## Lasciato al costruttore
Dettagli di layout, colori finali (palette dell'opzione A), valori di default delle macro.
