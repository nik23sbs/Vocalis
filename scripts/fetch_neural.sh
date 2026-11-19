#!/bin/bash
# Scarica ONNX Runtime (per la piattaforma corrente) e il modello Kokoro-82M con le voci.
# Uso: scripts/fetch_neural.sh <cartella destinazione>   -> <dest>/ort  e  <dest>/models
set -e
DEST="${1:-neural}"
mkdir -p "$DEST/models"
if [ ! -d "$DEST/ort" ]; then
  # Pacchetto ufficiale Microsoft.ML.OnnxRuntime da NuGet: header + librerie native per tutte le piattaforme.
  # Serie 1.23 = ultima con binari macOS Intel (x86_64): unendola ad arm64 si ottiene una libreria universale.
  IDX=$(curl -fsSL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/index.json || true)
  V=$(echo "$IDX" | grep -Eo '"1\.23\.[0-9]+"' | tr -d '"' | sort -t. -k3 -n | tail -1)
  [ -n "$V" ] || V=1.23.2
  echo "ONNX Runtime $V (NuGet)"
  curl -fsSL -o "$DEST/ort.nupkg" "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/$V/microsoft.ml.onnxruntime.$V.nupkg"
  rm -rf "$DEST/nupkg" && mkdir -p "$DEST/nupkg" && (cd "$DEST/nupkg" && unzip -q ../ort.nupkg)
  mkdir -p "$DEST/ort/include" "$DEST/ort/lib"
  cp "$DEST/nupkg/build/native/include/"*.h "$DEST/ort/include/"
  case "$(uname -s)" in
    Darwin)
      lipo -create "$DEST/nupkg/runtimes/osx-arm64/native/libonnxruntime.dylib" \
                   "$DEST/nupkg/runtimes/osx-x64/native/libonnxruntime.dylib" \
           -output "$DEST/ort/lib/libonnxruntime.$V.dylib" 2>/dev/null \
      || cp "$DEST/nupkg/runtimes/osx-arm64/native/libonnxruntime.dylib" "$DEST/ort/lib/libonnxruntime.$V.dylib"
      lipo -info "$DEST/ort/lib/libonnxruntime.$V.dylib" ;;
    Linux)
      cp "$DEST/nupkg/runtimes/linux-x64/native/libonnxruntime.so" "$DEST/ort/lib/libonnxruntime.so.$V" ;;
    MINGW*|MSYS*|CYGWIN*)
      cp "$DEST/nupkg/runtimes/win-x64/native/onnxruntime.dll" "$DEST/ort/lib/onnxruntime.dll" ;;
  esac
  rm -rf "$DEST/nupkg" "$DEST/ort.nupkg"
fi
test -f "$DEST/ort/include/onnxruntime_cxx_api.h" || { echo "ONNX Runtime non scaricato"; exit 1; }
for f in voices-v1.0.bin $( [ -f "$DEST/models/kokoro-v1.0-ctrl.onnx" ] || echo kokoro-v1.0.onnx ); do
  [ -f "$DEST/models/$f" ] || curl -fsSL -o "$DEST/models/$f" "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.1/$f"
done
# Vocalis 3: modello "controllabile" (durate e F0 imposte, uscita f0) generato dal modello originale.
# L'installer distribuisce solo questo (il motore accetta comunque anche l'originale).
if [ ! -f "$DEST/models/kokoro-v1.0-ctrl.onnx" ]; then
  HERE="$(cd "$(dirname "$0")/.." && pwd)"
  PYSYS=$(command -v python3 || command -v python)
  [ -d "$DEST/venv" ] || "$PYSYS" -m venv "$DEST/venv"
  PY="$DEST/venv/bin/python"; [ -x "$PY" ] || PY="$DEST/venv/Scripts/python.exe"   # Windows
  "$PY" -m pip install -q onnx==1.23.2 numpy
  "$PY" "$HERE/Tests/tools/kokoro_controllable.py" "$DEST/models/kokoro-v1.0.onnx" "$DEST/models/kokoro-v1.0-ctrl.onnx"
  # verifica con onnxruntime (Python) se disponibile per questa piattaforma; il motore C++ viene comunque provato dai test
  if "$PY" -m pip install -q onnxruntime 2>/dev/null; then
    "$PY" "$HERE/Tests/tools/kokoro_ctrl_check.py" "$DEST/models"
  else
    echo "onnxruntime (Python) non disponibile qui: salto la verifica Python del modello"
  fi
fi
rm -f "$DEST/models/kokoro-v1.0.onnx"
ls -la "$DEST/ort/lib" "$DEST/models"
