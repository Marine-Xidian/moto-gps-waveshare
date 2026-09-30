#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Personal build: upstream app and attribution are preserved. Signing happens
# on the user's Windows computer, never in this workflow.
xcodebuild -version
(cd platforms/ios && xcodegen generate)
xcodebuild -project platforms/ios/MotoGPS.xcodeproj \
  -scheme MotoGPS -configuration Release -sdk iphoneos \
  -destination 'generic/platform=iOS' -derivedDataPath build/ios \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO \
  PRODUCT_BUNDLE_IDENTIFIER=io.github.marine-xidian.motogps build

app='build/ios/Build/Products/Release-iphoneos/MOTO GPS.app'
test -f "$app/Info.plist"
test -f "$app/MOTO GPS"
mkdir -p build/ipa build/package/Payload
ditto "$app" 'build/package/Payload/MOTO GPS.app'
(cd build/package && zip -qry ../ipa/MotoGPS-unsigned.ipa Payload)
unzip -t build/ipa/MotoGPS-unsigned.ipa
shasum -a 256 build/ipa/MotoGPS-unsigned.ipa > build/ipa/SHA256SUMS.txt
{
  printf 'Source commit: '
  git rev-parse HEAD
  xcodebuild -version
  printf '\nUnsigned device build. Requires local signing before installation.\n'
  printf 'Personal LAN test gateway: http://192.168.31.57:8787/ (same router required).\n'
} > build/ipa/BUILD-INFO.txt
cp LICENSE.md NOTICE THIRD_PARTY_NOTICES.md build/ipa/
