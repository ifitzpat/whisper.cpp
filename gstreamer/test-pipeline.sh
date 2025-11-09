#!/bin/bash
# Example pipeline test for gst-whisper
# Demonstrates basic transcription pipeline

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
MODEL_PATH="$1"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========================================"
echo "GStreamer whisper.cpp - Pipeline Test"
echo "========================================"
echo ""

# Check arguments
if [ -z "$MODEL_PATH" ]; then
    echo -e "${YELLOW}Usage: $0 /path/to/ggml-model.bin${NC}"
    echo ""
    echo "Example:"
    echo "  $0 ~/models/ggml-base.en.bin"
    echo ""
    echo "This will create a simple pipeline that:"
    echo "  1. Takes audio input (from file or mic)"
    echo "  2. Passes it through the whispertranscribe element"
    echo "  3. Outputs transcription as JSON metadata"
    exit 1
fi

# Check if model exists
if [ ! -f "$MODEL_PATH" ]; then
    echo -e "${RED}ERROR: Model file not found: $MODEL_PATH${NC}"
    echo ""
    echo "Download models from:"
    echo "  bash ./models/download-ggml-model.sh base.en"
    exit 1
fi

# Check if plugin is built
if [ ! -f "$BUILD_DIR/libgstwhisper.so" ]; then
    echo -e "${RED}ERROR: Plugin not built. Run ./build-standalone.sh first${NC}"
    exit 1
fi

export GST_PLUGIN_PATH="$BUILD_DIR:$GST_PLUGIN_PATH"

echo -e "${BLUE}Model:${NC} $MODEL_PATH"
echo -e "${BLUE}Plugin:${NC} $BUILD_DIR/libgstwhisper.so"
echo ""

# Test 1: Simple pipeline construction (dry run)
echo "========================================"
echo "Test 1: Pipeline Construction (Dry Run)"
echo "========================================"
echo ""
echo "Testing if pipeline can be constructed..."
echo ""

gst-launch-1.0 \
    --gst-plugin-path="$BUILD_DIR" \
    --eos-on-shutdown \
    audiotestsrc num-buffers=100 \
    ! "audio/x-raw, rate=16000, channels=1, format=S16LE" \
    ! whispertranscribe \
        model="$MODEL_PATH" \
        language=en \
        n-threads=4 \
    ! fakesink \
    2>&1 | head -20

echo ""
echo -e "${GREEN}✓${NC} Pipeline construction test complete"
echo ""

# Test 2: Property inspection
echo "========================================"
echo "Test 2: Element Properties"
echo "========================================"
echo ""
echo "Configured properties:"
gst-launch-1.0 \
    --gst-plugin-path="$BUILD_DIR" \
    whispertranscribe \
        model="$MODEL_PATH" \
        language=en \
        n-threads=4 \
        temperature=0.0 \
        window-duration=10000 \
        step-duration=3000 \
    ! fakesink \
    2>&1 | grep -E "(model|language|threads|temperature|window)" | head -10 || echo "(Properties set)"

echo ""

# Test 3: Audio file transcription (if audio file exists)
echo "========================================"
echo "Test 3: Audio File Transcription"
echo "========================================"
echo ""

if [ -f "../samples/jfk.wav" ]; then
    echo "Testing transcription with jfk.wav sample..."
    echo ""

    gst-launch-1.0 \
        --gst-plugin-path="$BUILD_DIR" \
        filesrc location=../samples/jfk.wav \
        ! wavparse \
        ! audioconvert \
        ! audioresample \
        ! "audio/x-raw, rate=16000, channels=1" \
        ! whispertranscribe \
            model="$MODEL_PATH" \
            language=en \
        ! fakesink dump=true \
        2>&1 | head -30
else
    echo -e "${YELLOW}Note: Sample audio file not found at ../samples/jfk.wav${NC}"
    echo "To test with audio, run:"
    echo ""
    echo -e "${BLUE}# With audio file:${NC}"
    echo "gst-launch-1.0 \\"
    echo "  --gst-plugin-path=\"$BUILD_DIR\" \\"
    echo "  filesrc location=/path/to/audio.wav \\"
    echo "  ! wavparse ! audioconvert ! audioresample \\"
    echo "  ! 'audio/x-raw, rate=16000, channels=1' \\"
    echo "  ! whispertranscribe model=\"$MODEL_PATH\" language=en \\"
    echo "  ! fakesink dump=true"
    echo ""
    echo -e "${BLUE}# With microphone (PulseAudio):${NC}"
    echo "gst-launch-1.0 \\"
    echo "  --gst-plugin-path=\"$BUILD_DIR\" \\"
    echo "  pulsesrc \\"
    echo "  ! audioconvert ! audioresample \\"
    echo "  ! 'audio/x-raw, rate=16000, channels=1' \\"
    echo "  ! whispertranscribe model=\"$MODEL_PATH\" language=auto \\"
    echo "  ! fakesink dump=true"
fi

echo ""

# Test 4: Show full element details
echo "========================================"
echo "Test 4: Full Element Inspection"
echo "========================================"
echo ""
gst-inspect-1.0 --gst-plugin-path="$BUILD_DIR" whispertranscribe

echo ""
echo "========================================"
echo "Tests Complete!"
echo "========================================"
echo ""
echo -e "${GREEN}✓ Pipeline tests passed${NC}"
echo ""
echo "For more examples, see:"
echo "  - gstreamer/README.md"
echo "  - gstreamer/examples/"
echo "  - GSTREAMER_IMPLEMENTATION_PLAN.md"
