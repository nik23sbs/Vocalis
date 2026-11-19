#!/bin/bash
# Scarica ONNX Runtime (per la piattaforma corrente) e il modello Kokoro-82M con le voci.
# Uso: scripts/fetch_neural.sh <cartella destinazione>   -> <dest>/ort  e  <dest>/models
set -e
DEST="${1:-neural}"
mkdir -p "$DEST/models"
case "$(uname -s)" in
  Darwin) PLAT=osx-universal2 ;;
  Linux)  PLAT=linux-x64 ;;
esac
if [ ! -d "$DEST/ort" ]; then
  for V in 1.23.2 1.23.1 1.23.0 1.22.1 1.22.0; do
    F=onnxruntime-$PLAT-$V.tgz
    if curl -fsSL -o "$DEST/$F" "https://github.com/microsoft/onnxruntime/releases/download/v$V/$F"; then
      tar xzf "$DEST/$F" -C "$DEST" && rm "$DEST/$F"
      mv "$DEST/onnxruntime-$PLAT-$V" "$DEST/ort"
      echo "ONNX Runtime $V ($PLAT)"
      break
    fi
  done
fi
test -f "$DEST/ort/include/onnxruntime_cxx_api.h" || { echo "ONNX Runtime non scaricato"; exit 1; }
for f in kokoro-v1.0.onnx voices-v1.0.bin; do
  [ -f "$DEST/models/$f" ] || curl -fsSL -o "$DEST/models/$f" "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.1/$f"
done
ls -la "$DEST/ort/lib" "$DEST/models"
