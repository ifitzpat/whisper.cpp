#!/bin/bash
# Real-time transcription from microphone using whisper.cpp GStreamer plugin
#
# Usage: ./live-microphone.sh <model-path> [output-json]
#
# Example:
#   ./live-microphone.sh ../models/ggml-base.en.bin live-transcription.json

set -e

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <model-path> [output-json]"
    echo ""
    echo "Arguments:"
    echo "  model-path   Path to whisper model (.bin file)"
    echo "  output-json  Output JSON file (default: live-transcription.json)"
    echo ""
    echo "Example:"
    echo "  $0 ../models/ggml-base.en.bin live-output.json"
    echo ""
    echo "Note: This will transcribe live audio from your default microphone."
    echo "      Press Ctrl+C to stop."
    exit 1
fi

MODEL_PATH="$1"
OUTPUT_JSON="${2:-live-transcription.json}"

# Check if model exists
if [ ! -f "$MODEL_PATH" ]; then
    echo "Error: Model file not found: $MODEL_PATH"
    exit 1
fi

# Set plugin path if building locally
if [ -d "../build" ]; then
    export GST_PLUGIN_PATH="$(cd ../build && pwd)"
    echo "Using local plugin build: $GST_PLUGIN_PATH"
fi

echo "Starting live transcription from microphone..."
echo "Model: $MODEL_PATH"
echo "Output: $OUTPUT_JSON"
echo ""
echo "Speak into your microphone. Press Ctrl+C to stop."
echo ""

# Run GStreamer pipeline with PulseAudio source
gst-launch-1.0 -e \
    pulsesrc ! \
    audioconvert ! \
    audioresample ! \
    audio/x-raw,rate=16000,channels=1,format=F32LE ! \
    whispertranscribe \
        model="$MODEL_PATH" \
        language=en \
        n-threads=4 \
        window-duration-ms=3000 \
        step-duration-ms=1000 \
        enable-vad=true ! \
    filesink location="$OUTPUT_JSON"

echo ""
echo "Transcription stopped."
echo "Output saved to: $OUTPUT_JSON"
echo ""
echo "View results:"
echo "  cat $OUTPUT_JSON | jq '.segments[].text'"
