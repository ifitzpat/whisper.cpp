#!/bin/bash
# Standalone build script for gst-whisper plugin
# Builds the GStreamer plugin without Guix

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
WHISPER_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========================================"
echo "GStreamer whisper.cpp Plugin - Build"
echo "========================================"
echo ""

# Check for required tools
echo "Checking for required tools..."
for tool in meson ninja pkg-config cmake make; do
    if ! command -v $tool &> /dev/null; then
        echo -e "${RED}✗ $tool not found${NC}"
        echo "Please install required dependencies. See QUICKSTART.md"
        exit 1
    fi
    echo -e "${GREEN}✓${NC} $tool found"
done
echo ""

# Check for GStreamer
echo "Checking for GStreamer..."
if pkg-config --exists gstreamer-1.0; then
    GST_VERSION=$(pkg-config --modversion gstreamer-1.0)
    echo -e "${GREEN}✓${NC} GStreamer $GST_VERSION found"
else
    echo -e "${RED}✗ GStreamer not found${NC}"
    echo "Please install GStreamer development packages. See QUICKSTART.md"
    exit 1
fi

if pkg-config --exists gstreamer-audio-1.0; then
    echo -e "${GREEN}✓${NC} GStreamer Audio library found"
else
    echo -e "${RED}✗ GStreamer Audio library not found${NC}"
    echo "Please install gstreamer-plugins-base-devel"
    exit 1
fi

if pkg-config --exists json-glib-1.0; then
    echo -e "${GREEN}✓${NC} json-glib found"
else
    echo -e "${RED}✗ json-glib not found${NC}"
    echo "Please install json-glib-devel or libjson-glib-dev"
    exit 1
fi
echo ""

# Check if whisper.cpp is built
echo "Checking for whisper.cpp library..."
WHISPER_LIB="$WHISPER_ROOT/build/src/libwhisper.so"
if [ ! -f "$WHISPER_LIB" ] && [ ! -f "$WHISPER_ROOT/build/src/libwhisper.a" ]; then
    echo -e "${YELLOW}⚠ whisper.cpp library not found${NC}"
    echo ""
    echo "Building whisper.cpp first..."
    cd "$WHISPER_ROOT"

    if [ ! -d "build" ]; then
        mkdir build
    fi

    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
    make -j$(nproc)
    cd "$SCRIPT_DIR"

    if [ -f "$WHISPER_LIB" ] || [ -f "$WHISPER_ROOT/build/src/libwhisper.a" ]; then
        echo -e "${GREEN}✓${NC} whisper.cpp built successfully"
    else
        echo -e "${RED}✗ Failed to build whisper.cpp${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}✓${NC} whisper.cpp library found"
fi
echo ""

# Configure with Meson
echo "Configuring build with Meson..."
if [ -d "$BUILD_DIR" ]; then
    echo "Removing existing build directory..."
    rm -rf "$BUILD_DIR"
fi

meson setup "$BUILD_DIR" \
    --buildtype=debugoptimized \
    -Dtests=enabled

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓${NC} Configuration successful"
else
    echo -e "${RED}✗${NC} Configuration failed"
    exit 1
fi
echo ""

# Build
echo "Building plugin..."
meson compile -C "$BUILD_DIR"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓${NC} Build successful"
else
    echo -e "${RED}✗${NC} Build failed"
    exit 1
fi
echo ""

# Run tests
echo "Running tests..."
meson test -C "$BUILD_DIR" -v

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓${NC} All tests passed"
else
    echo -e "${YELLOW}⚠${NC} Some tests failed (check output above)"
fi
echo ""

# Summary
echo "========================================"
echo "Build Summary"
echo "========================================"
echo ""
echo -e "${GREEN}✓ Plugin built successfully!${NC}"
echo ""
echo "Plugin location:"
echo "  $BUILD_DIR/libgstwhisper.so"
echo ""
echo "To use the plugin:"
echo "  export GST_PLUGIN_PATH=$BUILD_DIR:\$GST_PLUGIN_PATH"
echo ""
echo "To verify:"
echo "  gst-inspect-1.0 --gst-plugin-path=$BUILD_DIR whispertranscribe"
echo ""
echo "To install system-wide (optional):"
echo "  cd $BUILD_DIR && sudo meson install"
echo ""
echo "Next steps:"
echo "  1. Run plugin tests: ./test-plugin.sh"
echo "  2. Download a model: bash ../models/download-ggml-model.sh base.en"
echo "  3. Test transcription: ./test-pipeline.sh /path/to/ggml-base.en.bin"
echo "  4. See QUICKSTART.md for more examples"
echo ""
