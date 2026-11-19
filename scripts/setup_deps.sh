#!/bin/bash
# Scarica le librerie esterne alle versioni testate: JUCE, eSpeak-NG, WORLD.
set -e
cd "$(dirname "$0")/.."
mkdir -p third_party

if [ ! -d third_party/JUCE ]; then
  git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git third_party/JUCE
fi

if [ ! -d third_party/espeak-ng ]; then
  git clone https://github.com/espeak-ng/espeak-ng.git third_party/espeak-ng
  git -C third_party/espeak-ng checkout 688cb60368d6e0998f3632af8d1c3c2ad4191fc2
fi

if [ ! -d third_party/World ]; then
  git clone https://github.com/mmorise/World.git third_party/World
  git -C third_party/World checkout c794856b537758a3d300757db2e5e31685a822c2
fi
echo "Dipendenze pronte."
