#!/bin/bash
# Builds a self-contained Keycast.app (Release) that can be copied to another Mac,
# and zips it.
#
# Usage: shell/apple/package_keycast.sh [output_dir]   (default: ~/Desktop)
#
# Requires the Xcode project from shell/apple/generate_xcode_project.command
# and Homebrew. Replaces any Keycast.app and Keycast.zip in the output folder.
# The app is prepared in build/package since codesign fails in folders synced
# with iCloud (such as the Desktop) because of the metadata iCloud adds to files.

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
cd "$script_dir/../../"

out_dir="${1:-$HOME/Desktop}"
work_dir="$PWD/build/package"
app="$work_dir/Keycast.app"

if [ ! -d build/flycast.xcodeproj ]; then
    echo 'No Xcode project found. Run shell/apple/generate_xcode_project.command first.' >&2
    exit 1
fi
if ! command -v dylibbundler >/dev/null 2>&1; then
    echo 'Installing dylibbundler with Homebrew'
    brew install dylibbundler
fi

echo '== Building Release'
cmake --build build --config Release

src="$(find build -maxdepth 3 -type d -name Keycast.app -path '*Release*' | head -n 1)"
if [ -z "$src" ]; then
    echo 'Release Keycast.app not found in build/' >&2
    exit 1
fi

echo "== Copying $src to $app"
rm -rf "$work_dir"
mkdir -p "$work_dir"
cp -R "$src" "$app"
# Finder metadata copied along with the app makes codesign fail
xattr -cr "$app"

# The local build links Homebrew's libomp and libpng dynamically. Copy them into
# the app so that it runs on Macs without Homebrew.
echo '== Bundling Homebrew libraries'
dylibbundler -of -b -x "$app/Contents/MacOS/Keycast" \
    -d "$app/Contents/Frameworks/" -p @executable_path/../Frameworks/

# Modified binaries must be signed again, from the inside out
echo '== Signing'
xattr -cr "$app"
shopt -s nullglob
for lib in "$app"/Contents/Frameworks/*.dylib; do
    codesign --force -s - "$lib"
done
for framework in "$app"/Contents/Frameworks/*.framework; do
    codesign --force -s - "$framework"
done
shopt -u nullglob
codesign --force -s - "$app"
codesign --verify --deep --strict "$app"

if otool -L "$app/Contents/MacOS/Keycast" | grep -q -E '/opt/homebrew/|/usr/local/(opt|Cellar)/'; then
    echo 'The app still depends on Homebrew libraries:' >&2
    otool -L "$app/Contents/MacOS/Keycast" | grep -E '/opt/homebrew/|/usr/local/(opt|Cellar)/' >&2
    exit 1
fi

echo "== Copying Keycast.app and Keycast.zip to $out_dir"
ditto -c -k --keepParent "$app" "$work_dir/Keycast.zip"
rm -rf "$out_dir/Keycast.app" "$out_dir/Keycast.zip"
ditto "$app" "$out_dir/Keycast.app"
cp "$work_dir/Keycast.zip" "$out_dir/Keycast.zip"

printf '\033[1;32m%s\033[0m\n' "Created $out_dir/Keycast.app and $out_dir/Keycast.zip"
