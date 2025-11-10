#!/bin/bash
# Transcribe with GPU acceleration using whisper.cpp GStreamer plugin
#
# Usage: ./transcribe-gpu.sh <audio-file> <model-path> [output-json]
#
# Example:
#   ./transcribe-gpu.sh recording.wav ../models/ggml-base.en.bin output.json
#
# Requirements: CUDA-enabled whisper.cpp build

set -e

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <audio-file> <model-path> [output-json]"
    echo ""
    echo "Arguments:"
    echo "  audio-file   Path to audio file"
    echo "  model-path   Path to whisper model (.bin file)"
    echo "  output-json  Output JSON file (default: transcription.json)"
    echo ""
    echo "Requirements:"
    echo "  - CUDA-enabled GPU"
    echo "  - whisper.cpp built with CUDA support"
    echo ""
    echo "Example:"
    echo "  $0 recording.wav ../models/ggml-base.en.bin output.json"
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

# Check for CUDA
if ! command -v nvidia-smi &> /dev/null; then
    echo "Warning: nvidia-smi not found. GPU acceleration may not work."
    echo "Continuing anyway..."
fi

# Set plugin path if building locally
if [ -d "../build" ]; then
    export GST_PLUGIN_PATH="$(cd ../build && pwd)"
    echo "Using local plugin build: $GST_PLUGIN_PATH"
fi

echo "Transcribing with GPU acceleration: $AUDIO_FILE"
echo "Model: $MODEL_PATH"
echo "Output: $OUTPUT_JSON"
echo ""

# Run GStreamer pipeline with GPU enabled
gst-launch-1.0 -q \
    filesrc location="$AUDIO_FILE" ! \
    decodebin ! \
    audioconvert ! \
    audioresample ! \
    audio/x-raw,rate=16000 ! \
    whispertranscribe \
        model="$MODEL_PATH" \
        language=en \
        use-gpu=true \
        n-threads=8 \
        window-duration-ms=5000 ! \
    filesink location="$OUTPUT_JSON"

echo ""
echo "Transcription complete!"
echo "Output saved to: $OUTPUT_JSON"
echo ""
echo "View results:"
echo "  cat $OUTPUT_JSON | jq '.segments[].text'"
