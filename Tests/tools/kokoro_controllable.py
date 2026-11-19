#!/usr/bin/env python3
"""Rende Kokoro "controllabile": aggiunge al grafo ONNX la possibilita' di imporre
   - la durata di ogni fonema (in frame da 600 campioni)  -> voce gia' a tempo, senza tagli
   - la curva di F0 (Hz, 2 valori per frame)               -> intonazione/canto neurale
Nuovi ingressi: dur_override int64[T] + use_dur bool, f0_override float[1,F] + use_f0 bool.
n_override float[1,F] + use_n bool: energia (N) imposta, per tenere le vocali lunghe del canto.
Nuove uscite: f0 e n (curve usate dal generatore).
Con use_* = false il modello si comporta esattamente come l'originale.
Uso: kokoro_controllable.py kokoro-v1.0.onnx kokoro-v1.0-ctrl.onnx"""
import sys, onnx
from onnx import helper, TensorProto

src, dst = sys.argv[1], sys.argv[2]
m = onnx.load(src)
g = m.graph

def walk(graph):
    for n in graph.node:
        yield n
        for a in n.attribute:
            if a.type == onnx.AttributeProto.GRAPH:
                yield from walk(a.g)

# --- tensori F0 e N (energia) = uscite dei nodi If che contengono lo Squeeze di /F0_proj e /N_proj
def if_output_for(proj):
    for n in g.node:
        if n.op_type == "If":
            inner = [x for a in n.attribute if a.type == onnx.AttributeProto.GRAPH for x in walk(a.g)]
            if any(any(proj + "/Conv_output_0" in i for i in x.input) for x in inner):
                return n.output[0]
f0_tensor, n_tensor = if_output_for("F0_proj"), if_output_for("N_proj")
assert f0_tensor and n_tensor, "F0/N non trovati"
dur_tensor = "duration"

g.input.extend([
    helper.make_tensor_value_info("dur_override", TensorProto.INT64, ["dur_len"]),
    helper.make_tensor_value_info("use_dur", TensorProto.BOOL, []),
    helper.make_tensor_value_info("f0_override", TensorProto.FLOAT, [1, "f0_len"]),
    helper.make_tensor_value_info("use_f0", TensorProto.BOOL, []),
    helper.make_tensor_value_info("n_override", TensorProto.FLOAT, [1, "n_len"]),
    helper.make_tensor_value_info("use_n", TensorProto.BOOL, []),
])

new_nodes = [
    helper.make_node("Where", ["use_dur", "dur_override", dur_tensor], ["dur_final"], name="/ctrl/dur_where"),
    helper.make_node("Where", ["use_f0", "f0_override", f0_tensor], ["f0_final"], name="/ctrl/f0_where"),
    helper.make_node("Where", ["use_n", "n_override", n_tensor], ["n_final"], name="/ctrl/n_where"),
]

# sostituisce gli usi (tranne nei nodi appena creati e nelle uscite del grafo)
for n in g.node:
    if n.name.startswith("/ctrl/"):
        continue
    for k, i in enumerate(n.input):
        if i == dur_tensor:
            n.input[k] = "dur_final"
        elif i == f0_tensor and n.op_type != "If":
            n.input[k] = "f0_final"
        elif i == n_tensor and n.op_type != "If":
            n.input[k] = "n_final"

# inserisce i nuovi nodi subito dopo i produttori (ordine topologico)
order = list(g.node)
def index_of_producer(t):
    for idx, n in enumerate(order):
        if t in n.output:
            return idx
    return -1
for node, t in sorted(zip(new_nodes, [dur_tensor, f0_tensor, n_tensor]), key=lambda p: -index_of_producer(p[1])):
    order.insert(index_of_producer(t) + 1, node)
del g.node[:]
g.node.extend(order)

# uscita "f0": la curva effettivamente usata (naturale se use_f0 = false), 2 valori per frame
g.node.append(helper.make_node("Identity", ["f0_final"], ["f0"], name="/ctrl/f0_out"))
g.output.append(helper.make_tensor_value_info("f0", TensorProto.FLOAT, [1, "f0_out_len"]))
g.node.append(helper.make_node("Identity", ["n_final"], ["n"], name="/ctrl/n_out"))
g.output.append(helper.make_tensor_value_info("n", TensorProto.FLOAT, [1, "n_out_len"]))

entry = m.metadata_props.add(); entry.key = "vocalis_controllable"; entry.value = "1"
onnx.checker.check_model(m)
onnx.save(m, dst)
print("scritto", dst, "F0 =", f0_tensor, "N =", n_tensor)
