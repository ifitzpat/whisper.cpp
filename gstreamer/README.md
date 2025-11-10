# GStreamer whisper.cpp Plugin

A production-ready GStreamer plugin for real-time speech recognition using [whisper.cpp](https://github.com/ggerganov/whisper.cpp). This plugin enables seamless integration of OpenAI's Whisper speech recognition model into GStreamer pipelines.

## Features

### Core Capabilities

- **Real-time Transcription**: Process audio streams with configurable sliding window mechanism
- **Async Processing**: Non-blocking worker thread for continuous audio transcription
- **Multiple Audio Formats**: Supports F32LE and S16LE formats, mono and stereo
- **JSON Output**: Structured transcription results with timestamps and segments
- **GPU Support**: Optional CUDA acceleration for faster inference
- **VAD Support**: Voice Activity Detection for improved efficiency
- **Language Detection**: Automatic language detection or explicit language selection
- **Flexible Configuration**: 17 configurable properties for fine-tuned control

### GStreamer Integration

- **Full Pipeline Support**: Event handling (flush, EOS, segment)
- **Query Support**: Latency, position, and duration queries
- **State Management**: Proper lifecycle management (start/stop)
- **Signal System**: 10 signals for monitoring transcription events
- **Thread-Safe**: Mutex-protected operations for concurrent access

## Quick Start

### Prerequisites

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

### Building

```bash
# Build whisper.cpp core library
cd /path/to/whisper.cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DWHISPER_BUILD_TESTS=OFF \
    -DWHISPER_BUILD_EXAMPLES=OFF \
    -DBUILD_SHARED_LIBS=ON
cmake --build build --config Release -j$(nproc)

# Build GStreamer plugin
cd gstreamer
./build-standalone.sh
```

### Installation

```bash
# Install plugin to user directory
mkdir -p ~/.local/lib/gstreamer-1.0
cp build/libgstwhisper.so ~/.local/lib/gstreamer-1.0/

# Or install system-wide
sudo cp build/libgstwhisper.so /usr/lib/x86_64-linux-gnu/gstreamer-1.0/

# Verify installation
gst-inspect-1.0 whispertranscribe
```

### Download Whisper Model

```bash
# Download a Whisper model (e.g., base.en)
cd /path/to/whisper.cpp
bash ./models/download-ggml-model.sh base.en

# Models will be in: models/ggml-base.en.bin
```

## Usage Examples

### Basic Transcription

```bash
# Transcribe audio file to JSON
gst-launch-1.0 \
    filesrc location=audio.wav ! \
    decodebin ! \
    audioconvert ! \
    whispertranscribe model=models/ggml-base.en.bin language=en ! \
    filesink location=transcription.json
```

### Live Microphone Transcription

```bash
# Real-time transcription from microphone
gst-launch-1.0 \
    pulsesrc ! \
    audioconvert ! \
    audioresample ! \
    'audio/x-raw,rate=16000,channels=1,format=F32LE' ! \
    whispertranscribe \
        model=models/ggml-base.en.bin \
        language=en \
        window-duration-ms=5000 \
        step-duration-ms=1000 ! \
    filesink location=live-transcription.json
```

### With GPU Acceleration

```bash
# Use GPU for faster inference
gst-launch-1.0 \
    pulsesrc ! \
    audioconvert ! \
    whispertranscribe \
        model=models/ggml-base.en.bin \
        use-gpu=true \
        n-threads=4 ! \
    filesink location=output.json
```

### WebRTC Integration

```bash
# Transcribe WebRTC audio stream
gst-launch-1.0 \
    webrtcbin name=webrtc ! \
    audioconvert ! \
    audioresample ! \
    whispertranscribe \
        model=models/ggml-base.en.bin \
        enable-vad=true \
        window-duration-ms=3000 ! \
    filesink location=webrtc-transcription.json
```

### Multi-Language Detection

```bash
# Automatic language detection
gst-launch-1.0 \
    filesrc location=multilingual.mp3 ! \
    decodebin ! \
    audioconvert ! \
    whispertranscribe \
        model=models/ggml-base.bin \
        detect-language=true ! \
    filesink location=output.json
```

### With Translation

```bash
# Transcribe and translate to English
gst-launch-1.0 \
    filesrc location=spanish.wav ! \
    decodebin ! \
    audioconvert ! \
    whispertranscribe \
        model=models/ggml-base.bin \
        language=es \
        translate=true ! \
    filesink location=translated.json
```

## Properties

The `whispertranscribe` element supports 17 configurable properties:

### Model Configuration

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `model` | String | NULL | Path to whisper model file (.bin) **[Required]** |
| `language` | String | "en" | Language code (e.g., 'en', 'fr', 'auto') |
| `detect-language` | Boolean | FALSE | Enable automatic language detection |

### Performance

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `n-threads` | Integer | 4 | Number of threads for inference |
| `use-gpu` | Boolean | FALSE | Enable GPU acceleration (requires CUDA) |

### Audio Processing

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `window-duration-ms` | Integer | 5000 | Sliding window duration (milliseconds) |
| `step-duration-ms` | Integer | 1000 | Step between windows (milliseconds) |
| `overlap-duration-ms` | Integer | 1000 | Overlap between windows (milliseconds) |

### Speech Detection

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `enable-vad` | Boolean | FALSE | Enable Voice Activity Detection |

### Translation

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `translate` | Boolean | FALSE | Translate to English |

### Sampling Strategy

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `sampling-strategy` | Integer | 0 | Sampling strategy (0=greedy, 1=beam_search) |
| `beam-size` | Integer | 5 | Beam size for beam search sampling |

### Quality Control

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `temperature` | Float | 0.0 | Temperature for sampling (0.0 = deterministic) |
| `entropy-threshold` | Float | 2.4 | Entropy threshold for failed decoding |
| `logprob-threshold` | Float | -1.0 | Log probability threshold |
| `no-speech-threshold` | Float | 0.6 | No-speech probability threshold |

### Context

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `initial-prompt` | String | NULL | Initial prompt to guide transcription |

## Signals

The element emits 10 signals for monitoring:

```c
// Model lifecycle
"model-loaded" (model_path: string)
"model-load-failed" (error_message: string)
"model-unloaded" ()

// Transcription events
"transcription-started" (pts: int64)
"transcription-completed" (pts: int64, duration_ms: int)
"transcription-failed" (pts: int64, error_message: string)

// Results
"segment-available" (segment_id: int, start_time: double, end_time: double, text: string)
"language-detected" (language: string, probability: float)

// Progress
"buffer-level-changed" (buffer_duration_ms: int)
"processing-stats" (windows_processed: int, avg_time_ms: int)
```

### Signal Usage Example (Python)

```python
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib

Gst.init(None)

pipeline = Gst.parse_launch('''
    pulsesrc ! audioconvert !
    whispertranscribe name=whisper model=models/ggml-base.en.bin !
    filesink location=output.json
''')

whisper = pipeline.get_by_name('whisper')

def on_segment(element, seg_id, start, end, text):
    print(f"[{start:.2f}s - {end:.2f}s]: {text}")

def on_model_loaded(element, model_path):
    print(f"Model loaded: {model_path}")

def on_language_detected(element, language, probability):
    print(f"Detected language: {language} ({probability:.2%})")

whisper.connect('segment-available', on_segment)
whisper.connect('model-loaded', on_model_loaded)
whisper.connect('language-detected', on_language_detected)

pipeline.set_state(Gst.State.PLAYING)
# ... run main loop
```

## Output Format

The plugin outputs JSON with the following structure:

```json
{
  "timestamp": 1234567890,
  "language": "en",
  "segments": [
    {
      "id": 0,
      "start": 0.0,
      "end": 2.5,
      "text": "Hello world"
    },
    {
      "id": 1,
      "start": 2.5,
      "end": 5.0,
      "text": "This is a test"
    }
  ]
}
```

### Field Descriptions

- `timestamp`: PTS (Presentation Timestamp) in nanoseconds
- `language`: Detected or specified language code
- `segments`: Array of transcription segments
  - `id`: Segment index
  - `start`: Start time in seconds
  - `end`: End time in seconds
  - `text`: Transcribed text

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────┐
│         GstWhisperTranscribe Element            │
├─────────────────────────────────────────────────┤
│                                                 │
│  ┌──────────────┐      ┌──────────────────┐   │
│  │ Audio Buffer │      │ Whisper Context  │   │
│  │   Manager    │◄─────┤    Manager       │   │
│  │              │      │                  │   │
│  │ - Sliding    │      │ - Model Loading  │   │
│  │   Window     │      │ - Transcription  │   │
│  │ - Buffering  │      │ - Thread Safety  │   │
│  └──────┬───────┘      └────────▲─────────┘   │
│         │                       │              │
│         │    ┌──────────────────┴────┐        │
│         └───►│   Worker Thread       │        │
│              │                       │        │
│              │ - Async Queue         │        │
│              │ - Non-blocking        │        │
│              │ - JSON Generation     │        │
│              └───────────────────────┘        │
│                                                 │
│  Sink Pad          Transform          Src Pad  │
│  (Audio)     ──────────────────►    (JSON)     │
│  F32LE/S16LE                                    │
└─────────────────────────────────────────────────┘
```

### Processing Flow

1. **Audio Input**: Audio arrives on sink pad (F32LE or S16LE, mono or stereo)
2. **Format Conversion**: Convert to mono F32LE if necessary
3. **Buffering**: AudioBufferManager accumulates audio in sliding windows
4. **Async Processing**: Windows queued to worker thread
5. **Transcription**: Worker calls whisper.cpp inference
6. **JSON Generation**: Results formatted as JSON
7. **Output**: JSON pushed to source pad

### Thread Safety

- **Main Thread**: Handles GStreamer events, properties, state changes
- **Worker Thread**: Performs whisper.cpp inference asynchronously
- **Synchronization**: GMutex, GAsyncQueue for thread-safe communication
- **Signals**: Emitted from main thread context

## Performance Tuning

### Latency vs Quality

```bash
# Low latency (500ms windows, 250ms steps)
whispertranscribe \
    window-duration-ms=500 \
    step-duration-ms=250 \
    overlap-duration-ms=100

# High quality (10s windows, 5s steps)
whispertranscribe \
    window-duration-ms=10000 \
    step-duration-ms=5000 \
    overlap-duration-ms=2000
```

### GPU Optimization

```bash
# Maximize GPU utilization
whispertranscribe \
    use-gpu=true \
    n-threads=8 \
    window-duration-ms=5000
```

### CPU Optimization

```bash
# Optimize for multi-core CPU
whispertranscribe \
    use-gpu=false \
    n-threads=8 \  # Match your CPU core count
    window-duration-ms=3000
```

## Debugging

### Enable GStreamer Debug Logging

```bash
# All debug output
export GST_DEBUG=3
gst-launch-1.0 ...

# Plugin-specific debug
export GST_DEBUG=whispertranscribe:5
gst-launch-1.0 ...

# Debug categories
export GST_DEBUG=whispertranscribe:5,GST_STATES:4,GST_EVENT:4
gst-launch-1.0 ...
```

### Inspect Element

```bash
# View all properties and signals
gst-inspect-1.0 whispertranscribe

# Check pad templates
gst-inspect-1.0 whispertranscribe | grep -A 10 "Pad Templates"

# View property values
gst-launch-1.0 whispertranscribe model=test.bin ! fakesink -v
```

### Common Issues

#### Plugin Not Found
```bash
# Set plugin path
export GST_PLUGIN_PATH=/path/to/whisper.cpp/gstreamer/build
gst-inspect-1.0 whispertranscribe

# Clear registry cache
rm ~/.cache/gstreamer-1.0/registry.*
gst-inspect-1.0 --gst-plugin-path=/path/to/build whispertranscribe
```

#### Model Loading Fails
```bash
# Verify model file exists
ls -lh models/ggml-base.en.bin

# Check model path in pipeline
gst-launch-1.0 ... whispertranscribe model=$(pwd)/models/ggml-base.en.bin ...

# Monitor model-load-failed signal
```

#### No Output
```bash
# Check if audio is flowing
gst-launch-1.0 ... ! whispertranscribe ... ! fakesink dump=true

# Verify audio format
gst-launch-1.0 ... ! audioconvert ! audio/x-raw,rate=16000 ! whispertranscribe ...

# Check model is loaded (look for model-loaded signal)
export GST_DEBUG=whispertranscribe:5
```

## Testing

### Unit Tests

```bash
cd gstreamer
./build-standalone.sh  # Builds and runs tests
```

### Integration Tests

```bash
# Test plugin registration
cd gstreamer
./test-plugin.sh

# Test with real audio
gst-launch-1.0 \
    audiotestsrc num-buffers=100 ! \
    audioconvert ! \
    whispertranscribe model=models/ggml-base.en.bin ! \
    filesink location=test-output.json
```

## CI/CD

The plugin includes GitHub Actions CI/CD:

- **Builds**: Ubuntu 22.04 and 24.04
- **Tests**: Automated unit and integration tests
- **Validation**: Plugin registration and property checks

See `.github/workflows/gstreamer-plugin.yml`

## Development

### Project Structure

```
gstreamer/
├── src/
│   ├── gstwhisperplugin.c          # Plugin entry point
│   ├── gstwhispertranscribe.c      # Main element implementation
│   ├── gstwhispertranscribe.h      # Element header
│   ├── whispercontextmanager.c     # Whisper model management
│   ├── whispercontextmanager.h
│   ├── audiobuffermanager.c        # Audio buffering
│   └── audiobuffermanager.h
├── tests/
│   └── test_plugin.c               # Unit tests
├── meson.build                     # Build configuration
├── build-standalone.sh             # Build script
├── test-plugin.sh                  # Integration test script
├── README.md                       # This file
├── QUICKSTART.md                   # TDD guide
├── TDD_STATUS.md                   # Implementation status
└── guix.scm                        # Guix package definition
```

### Implementation Status

All 9 phases complete:
- Phase 1: Basic Element Setup ✅
- Phase 2: WhisperContextManager ✅
- Phase 3: AudioBufferManager ✅
- Phase 4: Sliding Window Transcription ✅
- Phase 5: Worker Thread ✅
- Phase 6: Transform Implementation ✅
- Phase 7: Control Pad ✅
- Phase 8: JSON Output ✅
- Phase 9: State Management ✅

See `TDD_STATUS.md` for detailed status.

## Contributing

Contributions welcome! Please follow:

1. **Test-Driven Development**: Write tests first
2. **GStreamer Conventions**: Follow GStreamer coding style
3. **Documentation**: Update docs for new features
4. **Commit Messages**: Use conventional commits

## License

This plugin is part of whisper.cpp and follows the same MIT license.

## Acknowledgments

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) by Georgi Gerganov
- [OpenAI Whisper](https://github.com/openai/whisper) model
- GStreamer community

## Support

- **Issues**: https://github.com/ggerganov/whisper.cpp/issues
- **Discussions**: https://github.com/ggerganov/whisper.cpp/discussions
- **GStreamer Docs**: https://gstreamer.freedesktop.org/documentation/

## See Also

- `QUICKSTART.md` - Quick start guide with TDD workflow
- `TDD_STATUS.md` - Implementation status and phase details
- `CI_MONITORING.md` - CI/CD monitoring guide
- whisper.cpp documentation: https://github.com/ggerganov/whisper.cpp
