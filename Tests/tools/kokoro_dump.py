#!/usr/bin/env python3
"""Genera un "dump" di Kokoro (audio.wav, durations.txt, phonemes.txt) per testare la catena C++ senza ONNX Runtime C++.
Uso: kokoro_dump.py <cli> <testo> <voce> <cartella_uscita> [lingua]"""
import sys, os, json, subprocess
import numpy as np, onnxruntime as ort, soundfile as sf
cli, text, voice, out = sys.argv[1:5]
lang = sys.argv[5] if len(sys.argv) > 5 else "it"
models = os.environ.get("VOCALIS_MODELS", "/home/claude/models")
ps = subprocess.check_output([cli, "--phonemize", text, "--lang", lang]).decode().strip()
vocab = json.load(open(os.path.join(os.path.dirname(__file__), "kokoro_vocab.json")))
chars = [c for c in ps if c in vocab]
toks = [vocab[c] for c in chars]
so = ort.SessionOptions(); so.log_severity_level = 3
s = ort.InferenceSession(os.path.join(models, "kokoro-v1.0.onnx"), so)
style = np.load(os.path.join(models, "voices-v1.0.bin"))[voice][len(toks) - 1]
a, d = s.run(None, {"input_ids": np.array([[0, *toks, 0]], dtype=np.int64), "style": style, "speed": np.array([1.0], dtype=np.float32)})
os.makedirs(out, exist_ok=True)
sf.write(os.path.join(out, "audio.wav"), a, 24000, subtype="FLOAT")
open(os.path.join(out, "durations.txt"), "w").write(" ".join(str(int(x)) for x in d))
open(os.path.join(out, "phonemes.txt"), "w").write("".join(chars))
print(len(toks), "token,", len(a) / 24000, "s")
