#!/usr/bin/env bash
# The promises Play and the privacy policy rest on, read from the merged debug manifest
# (the main manifest is the same for every build type): INTERNET only with the Cast
# service, no AD_ID, Play Billing present, Android TV's declarations (no required hardware
# feature, the TV launcher entry, its banner), targetSdk at Play's floor. Run after
# :app:assembleDebug by build-lint.yml (every pull request) and android.yml.
set -euo pipefail
manifest=$(find app/build/intermediates -path '*debug*' -name AndroidManifest.xml \
  -path '*merged_manifest*' | head -n 1)
test -n "$manifest" || { echo "::error::merged debug manifest not found"; exit 1; }
echo "Merged manifest: $manifest"
if grep -q 'android.permission.INTERNET"' "$manifest" && ! grep -q 'dev.geode.cast.CastService' "$manifest"; then
  echo "::error::The merged manifest requests INTERNET without Cast; Cast to TV is the only internet use."; exit 1
fi
if grep -q 'com.google.android.gms.permission.AD_ID' "$manifest"; then
  echo "::error::The merged manifest carries AD_ID; the Advertising ID declaration says it is unused."; exit 1
fi
grep -q 'com.android.vending.BILLING' "$manifest" \
  || { echo "::error::Play Billing's BILLING permission is missing from the merged manifest"; exit 1; }
# Android TV (TV app quality, TV-ML and TV-MT): a library manifest must not make a hardware feature
# required, or Play hides the app from every TV; the TV launcher entry and its banner must survive
# the merge. TelevisionManifestTest holds the main manifest to the same before any build.
python3 - "$manifest" <<'PY'
import sys, xml.etree.ElementTree as ET
A = '{http://schemas.android.com/apk/res/android}'
root = ET.parse(sys.argv[1]).getroot()
required = [f.get(A + 'name') for f in root.iter('uses-feature')
            if f.get(A + 'name') and f.get(A + 'required', 'true') != 'false']
if required:
    sys.exit('::error::The merged manifest requires ' + ', '.join(required) + '; a TV lacks it, so Play would hide the app on TV.')
if not any(c.get(A + 'name') == 'android.intent.category.LEANBACK_LAUNCHER' for c in root.iter('category')):
    sys.exit('::error::The merged manifest has no LEANBACK_LAUNCHER activity; the app would not show on the TV launcher.')
if not root.find('application').get(A + 'banner'):
    sys.exit('::error::The merged manifest has no android:banner; the TV launcher needs one.')
print('TV declarations: no required hardware feature, LEANBACK_LAUNCHER and a banner')
PY
target=$(sed -n 's/.*android:targetSdkVersion="\([0-9]*\)".*/\1/p' "$manifest" | head -n 1)
echo "targetSdkVersion=$target"
[ "${target:-0}" -ge 36 ] || { echo "::error::targetSdkVersion $target is below Play's requirement (36)"; exit 1; }
