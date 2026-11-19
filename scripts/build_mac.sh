#!/bin/bash
# Compila Vocalis su macOS (VST3 + AU + Standalone) e lo installa nelle cartelle dei plugin dell'utente.
set -e
cd "$(dirname "$0")/.."

command -v cmake >/dev/null || { echo "Manca CMake: installalo con  brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Mancano gli strumenti di Xcode: esegui  xcode-select --install"; exit 1; }

./scripts/setup_deps.sh
./scripts/fetch_neural.sh neural

# Generatore Makefile: basta il pacchetto "Command Line Tools", non serve tutto Xcode
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DVOCALIS_BUILD_TESTS=OFF -DVOCALIS_ORT_DIR="$PWD/neural/ort"
cmake --build build-mac --target Vocalis_VST3 Vocalis_AU Vocalis_Standalone -j "$(sysctl -n hw.ncpu)"

OUT=build-mac/Vocalis_artefacts/Release
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/Vocalis.vst3 ~/Library/Audio/Plug-Ins/Components/Vocalis.component
cp -R "$OUT/VST3/Vocalis.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "$OUT/AU/Vocalis.component" ~/Library/Audio/Plug-Ins/Components/

# Firma "ad hoc" locale: senza, macOS puo' rifiutarsi di caricare il plugin
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/VST3/Vocalis.vst3
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/Vocalis.component
killall -9 AudioComponentRegistrar 2>/dev/null || true

# modello neurale condiviso
mkdir -p ~/Library/Application\ Support/Vocalis/models
cp neural/models/* ~/Library/Application\ Support/Vocalis/models/

echo ""
echo "Fatto! Vocalis e' installato:"
echo "  VST3: ~/Library/Audio/Plug-Ins/VST3/Vocalis.vst3"
echo "  AU:   ~/Library/Audio/Plug-Ins/Components/Vocalis.component"
echo "  App standalone: $OUT/Standalone/Vocalis.app"
