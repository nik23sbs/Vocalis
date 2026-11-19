#!/bin/bash
# Scarica ONNX Runtime (per la piattaforma corrente) e il modello Kokoro-82M con le voci.
# Uso: scripts/fetch_neural.sh <cartella destinazione>   -> <dest>/ort  e  <dest>/models
set -e
DEST="${1:-neural}"
mkdir -p "$DEST/models"
case "$(uname -s)" in
  Darwin) PLATS="osx-universal2 osx-arm64" ;;
  Linux)  PLATS="linux-x64" ;;
esac
if [ ! -d "$DEST/ort" ]; then
  # Elenco reale degli asset delle ultime release (i nomi cambiano tra le versioni)
  AUTH=()
  [ -n "${GITHUB_TOKEN:-}" ] && AUTH=(-H "Authorization: Bearer $GITHUB_TOKEN")
  curl -fsSL ${AUTH[@]+"${AUTH[@]}"} "https://api.github.com/repos/microsoft/onnxruntime/releases?per_page=15" -o "$DEST/releases.json"
  URL=""
  for PLAT in $PLATS; do
    URL=$(python3 - "$DEST/releases.json" "$PLAT" <<'PY'
import json, re, sys
rel = json.load(open(sys.argv[1])); plat = sys.argv[2]
pat = re.compile(r"^onnxruntime-" + re.escape(plat) + r"-(\d+\.\d+\.\d+)\.tgz$")
for r in rel:
    if r.get("prerelease") or r.get("draft"): continue
    for a in r.get("assets", []):
        if pat.match(a["name"]):
            print(a["browser_download_url"]); sys.exit(0)
PY
)
    [ -n "$URL" ] && break
  done
  [ -n "$URL" ] || { echo "Nessun pacchetto ONNX Runtime trovato per $PLATS"; python3 -c "import json;[print(r['tag_name'],[a['name'] for a in r['assets']][:12]) for r in json.load(open('$DEST/releases.json'))[:3]]"; exit 1; }
  echo "Scarico $URL"
  F=$(basename "$URL")
  curl -fsSL -o "$DEST/$F" "$URL"
  tar xzf "$DEST/$F" -C "$DEST" && rm "$DEST/$F"
  mv "$DEST/${F%.tgz}" "$DEST/ort"
fi
test -f "$DEST/ort/include/onnxruntime_cxx_api.h" || { echo "ONNX Runtime non scaricato"; exit 1; }
for f in kokoro-v1.0.onnx voices-v1.0.bin; do
  [ -f "$DEST/models/$f" ] || curl -fsSL -o "$DEST/models/$f" "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.1/$f"
done
ls -la "$DEST/ort/lib" "$DEST/models"
