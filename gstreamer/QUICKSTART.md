# GStreamer whisper.cpp Plugin - Quick Start Guide (TDD)

This guide follows a **Test-Driven Development** approach to build and test the GStreamer plugin for whisper.cpp.

## Prerequisites

### Required Packages

**Ubuntu/Debian:**
```bash
sudo apt-get install \
    build-essential \
    cmake \
    meson \
    ninja-build \
    pkg-config \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    libjson-glib-dev
```

**Fedora:**
```bash
sudo dnf install \
    gcc gcc-c++ \
    cmake \
    meson \
    ninja-build \
    pkg-config \
    gstreamer1-devel \
    gstreamer1-plugins-base-devel \
    gstreamer1-tools \
    json-glib-devel
```

**Arch Linux:**
```bash
sudo pacman -S \
    base-devel \
    cmake \
    meson \
    ninja \
    pkg-config \
    gstreamer \
    gst-plugins-base \
    gst-plugins-good \
    json-glib
```

## TDD Workflow

### Phase 1: Set Up Test Infrastructure

#### Step 1: Build whisper.cpp Core

```bash
cd /path/to/whisper.cpp
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
cd ..
```

#### Step 2: Build GStreamer Plugin (with tests)

```bash
cd gstreamer
./build-standalone.sh
```

This script will:
- ✓ Check for required dependencies
- ✓ Verify GStreamer installation
- ✓ Build whisper.cpp if needed
- ✓ Configure and build the plugin with Meson
- ✓ Run all unit tests automatically
- ✓ Show installation instructions

### Phase 2: Run Tests (Red → Green → Refactor)

#### Test Level 1: Plugin Registration Tests

```bash
cd gstreamer
./test-plugin.sh
```

This will verify (currently will FAIL as we haven't implemented yet):
- ✗ Plugin is discoverable by GStreamer
- ✗ Element 'whispertranscribe' can be created
- ✗ All properties are defined
- ✗ Sink and source pads exist
- ✗ Signals are registered

**Expected output at this stage:** Tests fail because implementation is missing.

#### Test Level 2: Unit Tests (Meson)

```bash
cd gstreamer/build
meson test -v
```

Tests include:
- `test_plugin` - C-based plugin registration test

#### Test Level 3: Integration Tests

```bash
cd gstreamer
./test-pipeline.sh /path/to/ggml-base.en.bin
```

This will test (will also FAIL initially):
- ✗ Pipeline construction
- ✗ Property configuration
- ✗ Audio processing
- ✗ Transcription output

## TDD Development Cycle

### Cycle 1: Minimal Plugin Registration

**Goal:** Make test-plugin.sh pass basic discovery tests

1. **Write test** (already done - see test-plugin.sh)
2. **Run test** - should FAIL:
   ```bash
   ./test-plugin.sh
   ```
3. **Write minimal code:**
   - Create `src/gstwhisperplugin.c` - plugin entry point
   - Create `src/gstwhispertranscribe.c` - minimal element stub
4. **Run test** - should PASS for discovery
5. **Refactor** if needed

### Cycle 2: Properties and Pads

**Goal:** Make property and pad tests pass

1. **Tests** check for properties (already in test-plugin.sh)
2. **Run test** - should FAIL for properties:
   ```bash
   ./test-plugin.sh
   ```
3. **Implement:**
   - Add property definitions in `class_init()`
   - Add pad templates
4. **Run test** - should PASS for properties
5. **Refactor**

### Cycle 3: Signals

**Goal:** Make signal tests pass

1. **Tests** check for signals (already in test-plugin.sh)
2. **Run test** - should FAIL for signals
3. **Implement:**
   - Add signal definitions
   - Add signal registration code
4. **Run test** - should PASS
5. **Refactor**

### Cycle 4: Core Components

Continue TDD for each component:
- Whisper Context Manager
- Audio Buffer Manager
- Sliding Window Transcription
- JSON Output Generation

## Testing Commands

### Run All Tests

```bash
cd gstreamer
./build-standalone.sh  # Builds and runs unit tests
./test-plugin.sh       # Runs integration tests
```

### Run Specific Tests

```bash
# Unit tests only
cd gstreamer/build
meson test -v

# Plugin discovery only
cd gstreamer
./test-plugin.sh 2>&1 | grep "Test 1/6" -A 5

# Property tests only
./test-plugin.sh 2>&1 | grep "Test 3/6" -A 20
```

### Debug Failed Tests

```bash
# Enable GStreamer debug output
export GST_DEBUG=3
./test-plugin.sh

# Check specific element
export GST_PLUGIN_PATH=build
gst-inspect-1.0 whispertranscribe

# View build logs
cat build/meson-logs/meson-log.txt
```

## Current Test Coverage

### Implemented Tests

- ✓ Plugin registration test framework
- ✓ Element creation test framework
- ✓ Property verification test framework
- ✓ Pad template verification
- ✓ Signal verification framework
- ✓ Pipeline construction tests

### Tests TODO (Red phase - will fail until implemented)

- ✗ Actual plugin registration (needs gstwhisperplugin.c)
- ✗ Element class definition (needs gstwhispertranscribe.c)
- ✗ Property implementation (needs class_init)
- ✗ Pad template implementation
- ✗ Signal registration
- ✗ Audio buffer tests
- ✗ Context manager tests
- ✗ Transcription tests

## Example TDD Session

Here's a complete example of one TDD cycle:

```bash
# 1. RED: Run tests (they fail)
cd gstreamer
./test-plugin.sh
# Expected output: "ERROR: Plugin 'whisper' not found in registry"

# 2. GREEN: Write minimal code to pass
# Create src/gstwhisperplugin.c with basic plugin registration
# Create src/gstwhispertranscribe.c with minimal element

# 3. Build
./build-standalone.sh

# 4. Test again
./test-plugin.sh
# Expected output: "✓ Plugin 'whisper' found in registry"

# 5. REFACTOR: Clean up code, add documentation

# 6. Commit
git add src/
git commit -m "feat: Add basic plugin registration (TDD cycle 1)"

# 7. Move to next test
# Run test-plugin.sh again - next test will fail
# Repeat cycle for properties...
```

## Troubleshooting

### Build Fails

```bash
# Clean build
rm -rf gstreamer/build
cd gstreamer
./build-standalone.sh

# Check dependencies
pkg-config --modversion gstreamer-1.0
pkg-config --modversion gstreamer-audio-1.0
pkg-config --modversion json-glib-1.0
```

### Tests Fail

```bash
# Check if plugin file exists
ls -lh gstreamer/build/libgstwhisper.so

# Verify plugin path
echo $GST_PLUGIN_PATH

# Try manual inspection
gst-inspect-1.0 --gst-plugin-path=gstreamer/build whispertranscribe
```

### Plugin Not Found

```bash
# Set plugin path explicitly
export GST_PLUGIN_PATH=$(pwd)/gstreamer/build
gst-inspect-1.0 whispertranscribe

# Check GST registry
rm ~/.cache/gstreamer-1.0/registry.*
gst-inspect-1.0 --gst-plugin-path=gstreamer/build whispertranscribe
```

## Next Steps (TDD Approach)

1. **Start with failing tests** - All tests currently fail
2. **Implement plugin registration** (simplest test)
3. **Make basic tests pass** (one at a time)
4. **Refactor and commit** after each passing test
5. **Add more tests** as you implement features
6. **Keep tests passing** - never commit broken tests

## Resources

- **Implementation Plan:** See `GSTREAMER_IMPLEMENTATION_PLAN.md`
- **GStreamer Docs:** https://gstreamer.freedesktop.org/documentation/plugin-development/
- **whisper.cpp API:** See `include/whisper.h`
- **Example:** Based on llama.cpp GStreamer plugin (tools/gstreamer in llama.cpp repo)

## TDD Best Practices for This Project

1. **Write tests first** - Tests define the interface
2. **Minimal implementation** - Pass tests with least code
3. **One test at a time** - Focus on single functionality
4. **Refactor often** - Keep code clean
5. **Run tests frequently** - After every small change
6. **Commit when green** - Only commit passing tests

---

*Follow the Red → Green → Refactor cycle for each component!*
