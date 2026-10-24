#!/bin/sh
# Builds the release files into dist/ and the web installer into site/:
#   ups-watcher-<ver>.bin         full image (bootloader, partition table, app):
#                                 flashed at offset 0 by the web installer or
#                                 esptool; erases settings
#   ups-watcher-<ver>-update.bin  the app alone, for Update on the device's web
#                                 page; keeps settings
# The version comes from UPSW_VERSION in platformio.ini.
# Usage: tools/release.sh            (then tools/publish_site.sh to push Pages)
set -e
cd "$(dirname "$0")/.."
ver=$(sed -n 's/.*UPSW_VERSION=\\"\([^\\]*\)\\".*/\1/p' platformio.ini)
[ -n "$ver" ] || { echo "no UPSW_VERSION in platformio.ini" >&2; exit 1; }
~/.platformio/penv/bin/pio run -e app
rm -rf dist site && mkdir -p dist site
cp .pio/build/app/firmware.factory.bin "dist/ups-watcher-$ver.bin"
cp .pio/build/app/firmware.bin "dist/ups-watcher-$ver-update.bin"
cp "dist/ups-watcher-$ver.bin" "site/ups-watcher-$ver.bin"
sed -e "s/@VER@/$ver/g" web-installer/index.html > site/index.html
sed -e "s/@VER@/$ver/g" web-installer/manifest.json > site/manifest.json
touch site/.nojekyll
ls -la dist site
