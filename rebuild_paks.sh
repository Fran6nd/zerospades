#!/bin/bash
# Script to rebuild PAK files after modifying AngelScript or resource files

set -e

cd "$(dirname "$0")"

echo "Cleaning old PAK files..."
rm -f build/Resources/*.pak
rm -f build/bin/OpenSpades.app/Contents/Resources/*.pak

echo "Rebuilding PAK files..."
cd build
make OpenSpades_Resources

echo "PAK files rebuilt successfully!"
echo "You can now run the project with ./run_openspades.sh"
