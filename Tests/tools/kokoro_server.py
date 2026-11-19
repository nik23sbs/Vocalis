#!/usr/bin/env python3
"""Server di prova: esegue Kokoro (modello ctrl) per i test locali del renderer quando ONNX Runtime C++ non c'e'.
Uso: kokoro_server.py <models dir> <cartella scambio>
Protocollo (file nella cartella di scambio):
  req.txt  (scritto per intero e poi rinominato):
     riga 1 fonemi | riga 2 "voce:peso,voce:peso" | riga 3 riga_stile | riga 4 velocita'
     riga 5 use_dur durate... | riga 6 use_f0 f0...
  risposta: out.bin (float32 audio) + out.txt (riga durate, riga f0), poi done.txt"""
import sys, os, json, time, numpy as np, onnxruntime as ort

M, X = sys.argv[1], sys.argv[2]
os.makedirs(X, exist_ok=True)
vocab = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "kokoro_vocab.json"), encoding="utf-8"))
so = ort.SessionOptions(); so.log_severity_level = 3
s = ort.InferenceSession(os.path.join(M, "kokoro-v1.0-ctrl.onnx"), so)
voices = np.load(os.path.join(M, "voices-v1.0.bin"))
open(os.path.join(X, "ready.txt"), "w").write("1")
req = os.path.join(X, "req.txt")
while True:
    if not os.path.exists(req):
        time.sleep(0.005); continue
    lines = open(req, encoding="utf-8").read().split("\n")
    os.remove(req)
    if os.environ.get("VPY_LOG"): open(os.path.join(X, "log_%06d.txt" % len(os.listdir(X))), "w", encoding="utf-8").write("\n".join(lines))
    ps, mix, row, speed = lines[0], lines[1], int(lines[2]), float(lines[3])
    toks = [vocab[c] for c in ps if c in vocab]
    st = np.zeros((1, 256), np.float32); tot = 0.0
    for part in mix.split(","):
        if not part: continue
        name, w = part.rsplit(":", 1); w = float(w)
        v = voices[name]; r = min(max(len(toks) - 1 + row, 0), v.shape[0] - 1)
        st += v[r] * w; tot += w
    st /= max(tot, 1e-9)
    d = lines[4].split(); f = lines[5].split()
    use_dur, use_f0 = d[0] == "1", f[0] == "1"
    dur = np.array([int(x) for x in d[1:]], np.int64) if use_dur else np.zeros(1, np.int64)
    f0 = np.array([[float(x) for x in f[1:]]], np.float32) if use_f0 else np.zeros((1, 1), np.float32)
    e = lines[6].split() if len(lines) > 6 and lines[6].strip() else ["0"]
    use_n = e[0] == "1"
    nn = np.array([[float(x) for x in e[1:]]], np.float32) if use_n else np.zeros((1, 1), np.float32)
    wav, pred, f0o, no = s.run(None, {"input_ids": np.array([[0, *toks, 0]], np.int64), "style": st,
                                  "speed": np.array([speed], np.float32), "dur_override": dur, "use_dur": np.array(use_dur),
                                  "f0_override": f0, "use_f0": np.array(use_f0),
                                  "n_override": nn, "use_n": np.array(use_n)})
    wav.astype(np.float32).ravel().tofile(os.path.join(X, "out.bin"))
    used = dur if use_dur else pred
    with open(os.path.join(X, "out.txt"), "w") as o:
        o.write(" ".join(str(int(x)) for x in used.ravel()) + "\n" + " ".join(f"{x:.3f}" for x in f0o.ravel()) + "\n" + " ".join(f"{x:.3f}" for x in no.ravel()) + "\n")
    open(os.path.join(X, "done.txt"), "w").write("1")
