#!/bin/bash
# Bundle all non-system dylibs into the macOS .app bundle.
# Usage: bundle_macos_dylibs.sh <executable> <frameworks_dir>
# Recursively copies and fixes references for all third-party dylibs.

set -euo pipefail

EXECUTABLE="$1"
FRAMEWORKS_DIR="$2"

mkdir -p "$FRAMEWORKS_DIR"

# Fix all non-system dylib references in a binary.
# Copies missing dylibs to Frameworks/ and rewrites load paths.
fix_dylib_refs() {
    local binary="$1"
    otool -L "$binary" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case "$dep" in
            /usr/lib/*|/System/*) continue ;;
            @executable_path/*|@rpath/*|@loader_path/*) continue ;;
        esac

        local filename
        filename=$(basename "$dep")
        local dest="$FRAMEWORKS_DIR/$filename"

        # Copy the dylib if not already bundled
        if [ ! -f "$dest" ]; then
            local realpath
            realpath=$(perl -MCwd -e 'print Cwd::realpath($ARGV[0])' "$dep")
            if [ ! -f "$realpath" ]; then
                echo "WARNING: Cannot resolve $dep" >&2
                continue
            fi
            cp "$realpath" "$dest"
            chmod u+w "$dest"
            # Set the dylib's own ID to the bundle-relative path
            install_name_tool -id "@executable_path/../Frameworks/$filename" "$dest"
            # Recursively fix this dylib's own dependencies
            fix_dylib_refs "$dest"
        fi

        # Rewrite the reference in the current binary
        install_name_tool -change "$dep" "@executable_path/../Frameworks/$filename" "$binary"
    done
}

# Fix the main executable
fix_dylib_refs "$EXECUTABLE"

# Fix cross-references between bundled dylibs
for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    [ -f "$dylib" ] || continue
    fix_dylib_refs "$dylib"
done

echo "Bundle dylib fixup complete."
