#!/bin/bash

# Simple CI monitoring script
# Since gh CLI is not available, this provides a basic framework

echo "=== CI Monitoring ==="
echo "Branch: claude/gstreamer-whisper-plan-011CUxys8upnQuZaQTXuTAcb"
echo ""
echo "Latest commits:"
git log --oneline -5
echo ""
echo "To check CI status, visit:"
echo "https://github.com/ifitzpat/whisper.cpp/actions"
echo ""
echo "Or ask the user to check the GitHub Actions tab"
echo ""
echo "Expected CI stages:"
echo "  1. Install dependencies (cmake, libunwind-dev, etc.)"
echo "  2. Build whisper.cpp with CMake"
echo "  3. Configure GStreamer plugin with meson"
echo "  4. Build GStreamer plugin"
echo "  5. Run tests and inspections"
echo ""
echo "Monitoring for common issues..."
echo ""

# Verify local build still works
echo "Verifying local whisper.cpp build..."
if [ -f "build/src/libwhisper.so" ]; then
    echo "✓ whisper.cpp library exists: $(ls -lh build/src/libwhisper.so | awk '{print $5}')"
else
    echo "✗ whisper.cpp library not found"
fi

echo ""
echo "Recent changes to workflow:"
git diff HEAD~2 .github/workflows/gstreamer-plugin.yml | head -50
