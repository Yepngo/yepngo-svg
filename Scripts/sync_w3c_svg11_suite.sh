#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite"
TMP_DIR="$ROOT/.tmp/w3c"
ARCHIVE_URL="https://www.w3.org/Graphics/SVG/Test/20061213/archives/W3C_SVG_11_FullTestSuite.tar.gz"
ARCHIVE_PATH="$TMP_DIR/W3C_SVG_11_FullTestSuite.tar.gz"

mkdir -p "$TMP_DIR"
mkdir -p "$OUT_DIR"

echo "Downloading W3C SVG 1.1 full suite archive..."
curl -L --fail --silent --show-error -o "$ARCHIVE_PATH" "$ARCHIVE_URL"

echo "Extracting svggen/, png/, and htmlEmbedHarness/full-index.html..."
TMP_EXTRACT="$TMP_DIR/extract_$(date +%s)"
mkdir -p "$TMP_EXTRACT"

tar -xzf "$ARCHIVE_PATH" -C "$TMP_EXTRACT" svggen png htmlEmbedHarness/full-index.html

mkdir -p "$OUT_DIR/svggen" "$OUT_DIR/png" "$OUT_DIR/htmlEmbedHarness"
cp -R "$TMP_EXTRACT/svggen/." "$OUT_DIR/svggen/"
cp -R "$TMP_EXTRACT/png/." "$OUT_DIR/png/"
cp "$TMP_EXTRACT/htmlEmbedHarness/full-index.html" "$OUT_DIR/htmlEmbedHarness/full-index.html"

echo "Done. Suite synced to: $OUT_DIR"
