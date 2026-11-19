#!/usr/bin/env python3
"""Verifica il modello controllabile (kokoro-v1.0-ctrl.onnx).
Uso: kokoro_ctrl_check.py <models dir>   (esce con 1 se un controllo fallisce)"""
import sys, os, json, numpy as np, onnxruntime as ort

M = sys.argv[1]
vocab = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "kokoro_vocab.json")))
ps = "kjˈaʊ, sˈono vokˈalis."
toks = [vocab[c] for c in ps if c in vocab]
so = ort.SessionOptions(); so.log_severity_level = 3
s = ort.InferenceSession(os.path.join(M, "kokoro-v1.0-ctrl.onnx"), so)
voices = np.load(os.path.join(M, "voices-v1.0.bin"))
ids = np.array([[0, *toks, 0]], np.int64)
z1, z2 = np.zeros(1, np.int64), np.zeros((1, 1), np.float32)
outs = [o.name for o in s.get_outputs()]
fails = []

def run(dur=None, f0=None, row=0, n=None):
    st = voices["if_sara"][len(toks) - 1 + row]
    feed = {"input_ids": ids, "style": st, "speed": np.array([1.0], np.float32),
            "dur_override": z1 if dur is None else dur, "use_dur": np.array(dur is not None),
            "f0_override": z2 if f0 is None else f0, "use_f0": np.array(f0 is not None)}
    if "n_override" in [i.name for i in s.get_inputs()]:
        feed.update({"n_override": z2 if n is None else n, "use_n": np.array(n is not None)})
    return dict(zip(outs, s.run(None, feed)))

def pitch(w):
    """F0 mediana (Hz) e voicing medio da autocorrelazione su finestre da 40 ms."""
    sr, n = 24000, 960; f, v = [], []
    for i in range(0, len(w) - n, n // 2):
        x = w[i:i + n] - w[i:i + n].mean()
        if np.sqrt((x ** 2).mean()) < 0.01: continue
        ac = np.correlate(x, x, "full")[n - 1:]; ac /= ac[0] + 1e-9
        lo, hi = sr // 600, sr // 70; k = lo + int(np.argmax(ac[lo:hi]))
        v.append(ac[k]); f.append(sr / k)
    return (float(np.median([a for a, b in zip(f, v) if b > 0.5] or [0])), float(np.mean(v or [0])))

def check(name, ok, info=""):
    print(("OK   " if ok else "FAIL ") + name, info)
    if not ok: fails.append(name)

check("uscita f0 presente", "f0" in outs, str(outs))
base = run()
dur = base["duration"].astype(np.int64)
if "f0" in outs:
    check("f0 lungo 2*durate", base["f0"].size == 2 * int(dur.sum()), f"{base['f0'].size} vs {2*int(dur.sum())}")

timed = dur.copy(); timed[1:-1] = np.maximum(1, timed[1:-1]) + 1
t = run(dur=timed)
check("durate imposte rispettate", abs(t["waveform"].size - 600 * int(timed.sum())) <= 600, f"{t['waveform'].size} vs {600*int(timed.sum())}")

if "f0" in outs:
    nat = t["f0"].reshape(1, -1).astype(np.float32)
    p0, _ = pitch(t["waveform"].ravel())
    h = run(dur=timed, f0=nat * 2 ** (4 / 12))
    p1, _ = pitch(h["waveform"].ravel())
    check("armonia +4 st", p0 > 0 and abs(12 * np.log2(p1 / p0) - 4) < 1.0, f"{p0:.0f} -> {p1:.0f} Hz")
    flat = np.full_like(nat, 220.0)
    p2, _ = pitch(run(dur=timed, f0=flat)["waveform"].ravel())
    check("nota 220 Hz", abs(12 * np.log2(max(p2, 1) / 220)) < 0.7, f"{p2:.0f} Hz")
    _, v0 = pitch(t["waveform"].ravel())
    _, v1 = pitch(run(dur=timed, f0=np.zeros_like(nat))["waveform"].ravel())
    check("sussurrato (f0=0) meno intonato", v1 < v0 - 0.15, f"voicing {v0:.2f} -> {v1:.2f}")

# vocale finale tenuta 1,5 s: con l'energia (N) sostenuta non deve spegnersi
check("uscita n presente", "n" in outs, str(outs))
if "n" in outs:
    long = dur.copy(); vi = [i for i, c in enumerate(ps) if c in "aeiouɛɔ"][-1]
    long[vi + 1] = 60
    a = run(dur=long)
    nn = a["n"].reshape(1, -1).astype(np.float32).copy()
    st = 2 * int(long[:vi + 1].sum()); en = st + 2 * 60
    nn[0, st + 8:en] = np.max(nn[0, st:st + 8])
    b = run(dur=long, f0=a["f0"].reshape(1, -1).astype(np.float32), n=nn)
    w = b["waveform"].ravel(); s0 = st * 300
    q1 = np.sqrt((w[s0 + 2400:s0 + 7200] ** 2).mean()); q2 = np.sqrt((w[s0 + 26000:s0 + 33000] ** 2).mean())
    check("vocale lunga sostenuta", q2 > 0.4 * q1, f"rms inizio {q1:.3f} fine {q2:.3f}")

var = run(row=12)
d2 = var["duration"].astype(np.int64)
check("variante diversa", int(np.abs(d2 - dur).sum()) > 0 or not np.allclose(var["waveform"][:5000], base["waveform"][:5000]), "")
sys.exit(1 if fails else 0)
