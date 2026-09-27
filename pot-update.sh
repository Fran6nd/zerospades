#!/bin/sh
# This file should be run from the repository root (e.g. ~/zerospades)

SEARCH_DIRS="Resources Sources"

# Make sure we're running from the repo root
for d in $SEARCH_DIRS; do
	if [ ! -d "$d" ]; then
		echo "Error: directory '$d' not found." >&2
		echo "Please run this script from the repository root (e.g. ~/zerospades)." >&2
		exit 1
	fi
done

# Single find pass (all extensions at once), pre-filtered with grep so
# xgettext only parses files that actually contain a translatable string.
find $SEARCH_DIRS \( -iname '*.h' -o -iname '*.cpp' -o -iname '*.c' -o -iname '*.as' \) \
	-exec grep -l -E '_Tr(N)?\(' {} + > .translate.this

if [ ! -s .translate.this ]; then
	echo "Error: no translatable source files found in ${SEARCH_DIRS}." >&2
	rm -f .translate.this
	exit 1
fi

OPTIONS_OUTPUT="-o Resources/Locales/pot/zerospades.pot"
OPTIONS_CPP="--c++"
OPTIONS_KEYWORD="-k_Tr:2,1c -k_TrN:2,1c,3" # Have no idea how this works
OPTIONS_COMMENTS="-c!" # comments for translators
OPTIONS="-j ${OPTIONS_OUTPUT} ${OPTIONS_CPP} ${OPTIONS_KEYWORD} ${OPTIONS_COMMENTS}"

META_PKG="--package-name=ZeroSpades"
META_COPYRIGHT="--copyright-holder=yvt"
META_BUGS="--msgid-bugs-address=i@yvt.jp"
METADATA="$META_PKG $META_COPYRIGHT $META_BUGS --omit-header"

xgettext $OPTIONS $METADATA -f .translate.this

if [ $? -ne 0 ]; then
	echo "Error: xgettext failed." >&2
	rm -f .translate.this
	exit 1
fi

rm -f .translate.this

echo "Gettext template file is now up-to-date."
echo
echo "Now you can run 'crowdin upload sources' to upload it to Crowdin!"
echo "(provided that you have an API key to do that)"
