#!/usr/bin/env python3
"""Prototipo del motore v3: Kokoro genera la voce GIA' a tempo imponendo la durata dei fonemi.
Le vocali (P-center) cadono sugli step della griglia; le consonanti tengono la durata naturale.
Opzionale: melodia (note MIDI per sillaba) -> F0 imposta = canto neurale.
Uso: kokoro_timed_demo.py <cli> "<testo>" <voce> <bpm> <out.wav> [note,note,...]"""
import sys, os, json, subprocess, numpy as np, onnxruntime as ort, soundfile as sf
cli, text, voice, bpm, out = sys.argv[1:6]
notes = [int(x) for x in sys.argv[6].split(",")] if len(sys.argv) > 6 else None
bpm = float(bpm)
M = os.environ.get("VOCALIS_MODELS", "/home/claude/models")
vocab = json.load(open(os.path.join(os.path.dirname(__file__), "kokoro_vocab.json")))
ps = subprocess.check_output([cli, "--phonemize", text]).decode().strip()
chars = [c for c in ps if c in vocab]; toks = [vocab[c] for c in chars]
so = ort.SessionOptions(); so.log_severity_level = 3
s = ort.InferenceSession(os.path.join(M, "kokoro-v1.0-ctrl.onnx"), so)
style = np.load(os.path.join(M, "voices-v1.0.bin"))[voice][len(toks) - 1]
ids = np.array([[0, *toks, 0]], np.int64); B = lambda b: np.array(b)
z1, z2 = np.zeros(1, np.int64), np.zeros((1, 1), np.float32)
_, pred = s.run(None, {"input_ids": ids, "style": style, "speed": np.array([1.0], np.float32),
                       "dur_override": z1, "use_dur": B(False), "f0_override": z2, "use_f0": B(False)})
VOW = set("aeiouyAEIOUYQWɑɐɒæɔəɘɚɛɜɝɞɤɨɪɯɵøœɶʉʊʌʏᵻ")
STRESS = set("ˈˌ"); PUNCT = set(";:,.!?—…\"()“”")
# nuclei: gruppi di vocali contigui (stress marks inclusi)
nuclei = []; i = 0
while i < len(chars):
    if chars[i] in VOW:
        j = i
        while j + 1 < len(chars) and (chars[j + 1] in VOW or chars[j+1] == "ː"): j += 1
        nuclei.append((i, j)); i = j + 1
    else: i += 1
fps = 24000 / 600; step = 0.25 * 60 / bpm * fps          # frame per sedicesimo
d = pred.astype(np.float64).copy()
# griglia: una sillaba per step, la punteggiatura aggiunge pause
anchors = []; st = 0
for k, (a, b) in enumerate(nuclei):
    anchors.append(st * step); st += 1
    nxt = nuclei[k + 1][0] if k + 1 < len(nuclei) else len(chars)
    seg = chars[b + 1:nxt]
    if any(c in ".!?" for c in seg): st += 2
    elif any(c in ",;:" for c in seg): st += 1
# durate: tra l'inizio di due nuclei lo spazio = distanza tra le ancore; consonanti naturali, vocale = resto
# (indice +1 perche' d[0] e' il silenzio iniziale)
lead = int(d[0]) + sum(int(max(1, d[c + 1])) for c in range(0, nuclei[0][0]))
target_abs = [lead + a for a in anchors]
new = d.copy(); new[0] = d[0]
for c in range(0, nuclei[0][0]): new[c + 1] = max(1, round(d[c + 1]))
pos = lead; err = 0.0
for k, (a, b) in enumerate(nuclei):
    nxt = nuclei[k + 1][0] if k + 1 < len(nuclei) else None
    cons = list(range(b + 1, nxt if nxt is not None else len(chars)))
    if nxt is None:
        span = step * 1.6 + sum(d[c + 1] for c in cons)
    else:
        span = target_abs[k + 1] - target_abs[k]
    exact = pos + span + err
    want = int(round(exact)) - pos; err = exact - (pos + want)
    cdur = [max(1, round(d[c + 1])) if chars[c] not in PUNCT else 0 for c in cons]
    punct = [c for c in cons if chars[c] in PUNCT]
    vow = list(range(a, b + 1))
    vmin = len([v for v in vow if chars[v] in VOW])
    while sum(cdur) + vmin > want and max(cdur, default=0) > 1:          # comprimi consonanti se serve
        m = int(np.argmax(cdur)); cdur[m] -= 1
    rest = max(vmin, want - sum(cdur))
    # se c'e' punteggiatura: la vocale dura ~2 step, il resto e' pausa
    if punct:
        vtot = min(rest, int(round(step * 1.8))); pdur = rest - vtot
    else:
        vtot, pdur = rest, 0
    vv = [v for v in vow if chars[v] in VOW]; w = np.array([d[v + 1] for v in vv]) + 1e-3
    alloc = np.maximum(1, np.floor(vtot * w / w.sum())).astype(int); alloc[0] += vtot - alloc.sum()
    for v in vow: new[v + 1] = 0
    for v, n in zip(vv, alloc): new[v + 1] = n
    for c, n in zip(cons, cdur): new[c + 1] = n
    for c in punct: new[c + 1] = pdur // max(1, len(punct))
    pos += want
new[-1] = 3
new = np.maximum(0, new).astype(np.int64)
f0, use_f0 = z2, False
if notes:
    tot = int(new.sum()); f0 = np.zeros((1, 2 * tot), np.float32)
    cum = np.cumsum(np.concatenate([[0], new]))
    hz = lambda n: 440 * 2 ** ((n - 69) / 12)
    for k, (a, b) in enumerate(nuclei):
        s0 = 2 * int(cum[a + 1]) - 4                                      # la nota parte poco prima della vocale
        s1 = 2 * int(cum[nuclei[k + 1][0] + 1]) - 4 if k + 1 < len(nuclei) else 2 * tot
        f0[0, max(0, s0):s1] = hz(notes[k % len(notes)])
    f0[0, :max(0, 2 * int(cum[nuclei[0][0] + 1]) - 4)] = hz(notes[0])
    use_f0 = True
wav, _ = s.run(None, {"input_ids": ids, "style": style, "speed": np.array([1.0], np.float32),
                      "dur_override": new, "use_dur": B(True), "f0_override": f0, "use_f0": B(use_f0)})
cut = int(lead * 600) - int(0.25 * 60 / bpm * 24000)                          # inizia uno step prima della prima vocale
wav = wav[max(0, cut):]
wav = wav / (np.abs(wav).max() + 1e-9) * 0.7
sf.write(out, wav, 24000)
print(len(nuclei), "sillabe, vocali sulla griglia a", bpm, "BPM ->", out)
