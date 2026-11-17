#!/bin/bash
# Crea l'installer macOS di Vocalis: dist/Vocalis-<versione>-macOS.pkg
# Uso: installer/make_pkg.sh <cartella artefatti Release>
set -euo pipefail
ART="$1"
cd "$(dirname "$0")/.."
VERSION=$(grep -Eo 'project\(Vocalis VERSION [0-9.]+' CMakeLists.txt | awk '{print $3}')
WORK=$(mktemp -d)
mkdir -p dist

component () {   # nome  sorgente  destinazione  [script]
  local name="$1" src="$2" dest="$3" scripts="${4:-}"
  local root="$WORK/root-$name"
  mkdir -p "$root$dest"
  cp -R "$src" "$root$dest/"
  # i bundle non devono essere "spostati" dall'Installer se ne esiste una copia altrove
  local rel="${dest#/}/$(basename "$src")"
  cat > "$WORK/$name.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><array><dict>
  <key>BundleHasStrictIdentifier</key><false/>
  <key>BundleIsRelocatable</key><false/>
  <key>BundleIsVersionChecked</key><false/>
  <key>BundleOverwriteAction</key><string>upgrade</string>
  <key>RootRelativeBundlePath</key><string>$rel</string>
</dict></array></plist>
PLIST
  # (bash 3.2 di macOS: niente array vuoti con set -u)
  if [ -n "$scripts" ]; then
    pkgbuild --root "$root" --component-plist "$WORK/$name.plist" --identifier "com.vocalis.$name" \
             --version "$VERSION" --install-location / --scripts "$scripts" "$WORK/$name.pkg"
  else
    pkgbuild --root "$root" --component-plist "$WORK/$name.plist" --identifier "com.vocalis.$name" \
             --version "$VERSION" --install-location / "$WORK/$name.pkg"
  fi
}

component vst3 "$ART/VST3/Vocalis.vst3"         /Library/Audio/Plug-Ins/VST3
component au   "$ART/AU/Vocalis.component"      /Library/Audio/Plug-Ins/Components installer/scripts-au
component app  "$ART/Standalone/Vocalis.app"    /Applications

sed "s/@VERSION@/$VERSION/g" installer/Distribution.xml > "$WORK/Distribution.xml"
productbuild --distribution "$WORK/Distribution.xml" --resources installer/resources \
             --package-path "$WORK" "dist/Vocalis-$VERSION-macOS.pkg"

echo "Creato: dist/Vocalis-$VERSION-macOS.pkg"
ls -la dist
