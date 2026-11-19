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
  # versione piu' recente disponibile; su macOS preferisce l'universale (Intel + Apple Silicon) se esiste
  TAGS=$(curl -fsSL "https://api.github.com/repos/microsoft/onnxruntime/releases?per_page=30" \
         | grep -Eo '"tag_name": *"v1\.[0-9]+\.[0-9]+"' | grep -Eo 'v1\.[0-9]+\.[0-9]+' || true)
  [ -n "$TAGS" ] || TAGS="v1.32.0 v1.31.0 v1.30.1 v1.30.0"
  for T in $TAGS; do
    V=${T#v}
    for PLAT in $PLATS; do
      F=onnxruntime-$PLAT-$V.tgz
      if curl -fsSL -o "$DEST/$F" "https://github.com/microsoft/onnxruntime/releases/download/$T/$F"; then
        tar xzf "$DEST/$F" -C "$DEST" && rm "$DEST/$F"
        mv "$DEST/onnxruntime-$PLAT-$V" "$DEST/ort"
        echo "ONNX Runtime $V ($PLAT)"
        break 2
      fi
    done
  done
fi
test -f "$DEST/ort/include/onnxruntime_cxx_api.h" || { echo "ONNX Runtime non scaricato"; exit 1; }
for f in kokoro-v1.0.onnx voices-v1.0.bin; do
  [ -f "$DEST/models/$f" ] || curl -fsSL -o "$DEST/models/$f" "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.1/$f"
done
ls -la "$DEST/ort/lib" "$DEST/models"
