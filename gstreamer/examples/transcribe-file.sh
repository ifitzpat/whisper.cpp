#!/bin/bash
# Transcribe an audio file to JSON using whisper.cpp GStreamer plugin
#
# Usage: ./transcribe-file.sh <audio-file> <model-path> [output-json]
#
# Example:
#   ./transcribe-file.sh recording.mp3 ../models/ggml-base.en.bin output.json

set -e

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <audio-file> <model-path> [output-json]"
    echo ""
    echo "Arguments:"
    echo "  audio-file   Path to audio file (mp3, wav, flac, etc.)"
    echo "  model-path   Path to whisper model (.bin file)"
    echo "  output-json  Output JSON file (default: transcription.json)"
    echo ""
    echo "Example:"
    echo "  $0 recording.mp3 ../models/ggml-base.en.bin output.json"
    exit 1
fi

AUDIO_FILE="$1"
MODEL_PATH="$2"
OUTPUT_JSON="${3:-transcription.json}"

# Check if files exist
if [ ! -f "$AUDIO_FILE" ]; then
    echo "Error: Audio file not found: $AUDIO_FILE"
    exit 1
fi

if [ ! -f "$MODEL_PATH" ]; then
    echo "Error: Model file not found: $MODEL_PATH"
    exit 1
fi

# Set plugin path if building locally
if [ -d "../build" ]; then
    export GST_PLUGIN_PATH="$(cd ../build && pwd)"
    echo "Using local plugin build: $GST_PLUGIN_PATH"
fi

echo "Transcribing: $AUDIO_FILE"
echo "Model: $MODEL_PATH"
echo "Output: $OUTPUT_JSON"
echo ""

# Run GStreamer pipeline
gst-launch-1.0 -q \
    filesrc location="$AUDIO_FILE" ! \
    decodebin ! \
    audioconvert ! \
    audioresample ! \
    audio/x-raw,rate=16000 ! \
    whispertranscribe \
        model="$MODEL_PATH" \
        language=en \
        n-threads=4 ! \
    filesink location="$OUTPUT_JSON"

echo ""
echo "Transcription complete!"
echo "Output saved to: $OUTPUT_JSON"
echo ""
echo "View results:"
echo "  cat $OUTPUT_JSON | jq '.segments[].text'"
