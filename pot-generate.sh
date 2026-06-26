#!/bin/sh
# Generates the .pot file from scratch, discarding any stale
# header/metadata left over from previous generations.
# Run this from the repository root (e.g. ~/zerospades).

POT_FILE="Resources/Locales/pot/zerospades.pot"

rm -f "$POT_FILE"
mkdir -p "$(dirname "$POT_FILE")"
touch "$POT_FILE"

./pot-update.sh