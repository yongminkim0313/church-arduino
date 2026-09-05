#!/usr/bin/env bash
# 회로도 PDF 다시 만들기 — 핀을 바꾸면 schematic.py 의 값도 함께 고칠 것.
# (핀의 원본은 TalentNfcReader.ino 와 build.sh 다)
set -euo pipefail
cd "$(dirname "$0")"
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
TMP="$(mktemp -d)"
python3 schematic.py "$TMP/schematic.html"
"$CHROME" --headless --disable-gpu --no-pdf-header-footer \
  --print-to-pdf="TalentNfcReader_회로도.pdf" "file://$TMP/schematic.html"
rm -rf "$TMP"
echo "생성: TalentNfcReader_회로도.pdf"
