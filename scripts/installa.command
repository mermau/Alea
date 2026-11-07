#!/bin/bash
# Installa Collateral Effects ALEA nelle cartelle di sistema dei plugin
# (/Library/Audio/Plug-Ins) e rimuove la quarantena. Chiede la password di amministratore.
cd "$(dirname "$0")"
set -e
VST3="/Library/Audio/Plug-Ins/VST3"
AU="/Library/Audio/Plug-Ins/Components"
echo "Installazione di ALEA (serve la password di amministratore)..."
sudo mkdir -p "$VST3" "$AU"
sudo rm -rf "$VST3/ALEA.vst3" "$AU/ALEA.component" "/Applications/ALEA.app"
sudo cp -R "ALEA.vst3" "$VST3/"
sudo cp -R "ALEA.component" "$AU/"
sudo cp -R "ALEA.app" "/Applications/"
sudo xattr -dr com.apple.quarantine "$VST3/ALEA.vst3" "$AU/ALEA.component" "/Applications/ALEA.app" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo ""
echo "Collateral Effects ALEA installato:"
echo "  VST3: $VST3/ALEA.vst3"
echo "  AU:   $AU/ALEA.component"
echo "  App:  /Applications/ALEA.app"
echo "Riavvia la DAW per vederlo."
