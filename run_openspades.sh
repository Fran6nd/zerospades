#!/bin/bash
# Run OpenSpades from the build directory

cd "$(dirname "$0")/build"
exec ./bin/OpenSpades.app/Contents/MacOS/OpenSpades "$@"
