#!/bin/bash
# Download the Rotenso Windmi manual
# Run this when network connectivity is available

set -e

MANUAL_URL="https://www.manualslib.com/manual/4051642/Rotenso-Windmi-Series.html"
OUTPUT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Downloading Rotenso Windmi manual..."
echo "Source: $MANUAL_URL"
echo "Destination: $OUTPUT_DIR/"

# Try to download the HTML version (manualslib provides HTML pages)
# Note: The actual PDF may require a different URL
curl -L -o "$OUTPUT_DIR/rotenso_windmi_manual.html" "$MANUAL_URL"
echo "Downloaded HTML manual to $OUTPUT_DIR/rotenso_windmi_manual.html"

# Try the PDF download link if available
PDF_URL="https://www.manualslib.com/download/4051642/Rotenso-Windmi-Series.html"
curl -L -o "$OUTPUT_DIR/rotenso_windmi_manual.pdf" "$PDF_URL" 2>/dev/null && \
    echo "Downloaded PDF manual to $OUTPUT_DIR/rotenso_windmi_manual.pdf" || \
    echo "PDF download failed (may require JavaScript rendering)"
