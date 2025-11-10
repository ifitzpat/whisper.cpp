# GStreamer whisper.cpp Plugin Examples

This directory contains practical examples for using the whisper.cpp GStreamer plugin.

## Prerequisites

All examples require:
- GStreamer whisper.cpp plugin installed (see `../README.md`)
- Whisper model file (download with `../models/download-ggml-model.sh`)
- Audio input (file or microphone)

## Examples

### 1. transcribe-file.sh

Basic transcription of an audio file to JSON.

**Usage:**
```bash
./transcribe-file.sh <audio-file> <model-path> [output-json]
```

**Example:**
```bash
# Transcribe MP3 file
./transcribe-file.sh recording.mp3 ../models/ggml-base.en.bin output.json

# View results
cat output.json | jq '.segments[].text'
```

**Features:**
- Supports any audio format (mp3, wav, flac, ogg, etc.)
- Automatic format conversion to 16kHz
- English language transcription
- JSON output with timestamps

### 2. live-microphone.sh

Real-time transcription from your microphone.

**Usage:**
```bash
./live-microphone.sh <model-path> [output-json]
```

**Example:**
```bash
# Start live transcription
./live-microphone.sh ../models/ggml-base.en.bin live-output.json

# Press Ctrl+C to stop
```

**Features:**
- Real-time audio capture from default microphone
- 3-second sliding windows with 1-second steps
- Voice Activity Detection (VAD) enabled
- Continuous transcription until stopped

**Requirements:**
- PulseAudio or PipeWire audio system
- Working microphone

### 3. transcribe-gpu.sh

GPU-accelerated transcription for faster processing.

**Usage:**
```bash
./transcribe-gpu.sh <audio-file> <model-path> [output-json]
```

**Example:**
```bash
# Transcribe with GPU
./transcribe-gpu.sh large-file.wav ../models/ggml-base.en.bin output.json
```

**Features:**
- CUDA GPU acceleration
- Optimized for large files
- 5-second windows for throughput
- 8 CPU threads for preprocessing

**Requirements:**
- NVIDIA GPU with CUDA support
- whisper.cpp built with CUDA (`cmake -DWHISPER_CUDA=ON`)

### 4. python-signals.py

Python example demonstrating signal handling for real-time monitoring.

**Usage:**
```bash
python3 python-signals.py <audio-file> <model-path>
```

**Example:**
```bash
# Monitor transcription with Python
python3 python-signals.py recording.wav ../models/ggml-base.en.bin
```

**Features:**
- Real-time segment display as they're transcribed
- Model loading status
- Transcription progress monitoring
- Buffer level tracking
- Performance statistics
- Complete transcript summary

**Signals Demonstrated:**
- `model-loaded` / `model-load-failed`
- `transcription-started` / `transcription-completed` / `transcription-failed`
- `segment-available` (real-time results)
- `language-detected`
- `buffer-level-changed`
- `processing-stats`

**Requirements:**
```bash
pip install PyGObject
```

## Quick Start

1. **Download a model:**
```bash
cd /path/to/whisper.cpp
bash ./models/download-ggml-model.sh base.en
```

2. **Make scripts executable:**
```bash
chmod +x *.sh
chmod +x *.py
```

3. **Run an example:**
```bash
# File transcription
./transcribe-file.sh test-audio.wav ../models/ggml-base.en.bin

# Live microphone (speak after starting)
./live-microphone.sh ../models/ggml-base.en.bin

# Python with signals
python3 python-signals.py test-audio.wav ../models/ggml-base.en.bin
```

## Advanced Examples

### Custom Pipeline

You can create custom pipelines by combining GStreamer elements:

```bash
# Transcribe with custom parameters
gst-launch-1.0 \
    filesrc location=audio.mp3 ! \
    decodebin ! \
    audioconvert ! \
    audioresample ! \
    audio/x-raw,rate=16000,channels=1 ! \
    whispertranscribe \
        model=models/ggml-base.en.bin \
        language=en \
        window-duration-ms=5000 \
        step-duration-ms=2000 \
        n-threads=8 \
        temperature=0.0 \
        beam-size=5 ! \
    filesink location=output.json
```

### Multi-Language Detection

```bash
# Detect language automatically
gst-launch-1.0 \
    filesrc location=multilingual.mp3 ! \
    decodebin ! \
    audioconvert ! \
    whispertranscribe \
        model=models/ggml-base.bin \
        detect-language=true ! \
    filesink location=output.json
```

### Translation to English

```bash
# Transcribe Spanish and translate to English
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

### WebRTC Stream

```bash
# Transcribe WebRTC audio (requires webrtcbin setup)
gst-launch-1.0 \
    webrtcbin name=webrtc ! \
    audioconvert ! \
    audioresample ! \
    audio/x-raw,rate=16000 ! \
    whispertranscribe \
        model=models/ggml-base.en.bin \
        enable-vad=true \
        window-duration-ms=3000 ! \
    filesink location=webrtc-output.json
```

## Output Format

All examples produce JSON output with this structure:

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

## Processing Output

### Extract Text Only

```bash
# All text concatenated
cat output.json | jq -r '.segments[].text' | tr '\n' ' '

# Formatted transcript
cat output.json | jq -r '.segments[] | "[\(.start)s - \(.end)s] \(.text)"'
```

### Convert to SRT Subtitles

```python
import json

with open('output.json') as f:
    data = json.load(f)

for i, seg in enumerate(data['segments'], 1):
    start = seg['start']
    end = seg['end']
    text = seg['text']

    print(f"{i}")
    print(f"{format_time(start)} --> {format_time(end)}")
    print(f"{text}\n")
```

### Filter by Timestamp

```bash
# Get segments after 30 seconds
cat output.json | jq '.segments[] | select(.start > 30)'

# Get specific time range
cat output.json | jq '.segments[] | select(.start >= 10 and .end <= 20)'
```

## Troubleshooting

### Plugin Not Found

```bash
# Set plugin path for local builds
export GST_PLUGIN_PATH=/path/to/whisper.cpp/gstreamer/build

# Clear GStreamer cache
rm ~/.cache/gstreamer-1.0/registry.*
```

### No Audio Input

```bash
# List available audio sources
gst-device-monitor-1.0 Audio

# Test microphone
gst-launch-1.0 pulsesrc ! audioconvert ! autoaudiosink
```

### Model Loading Fails

```bash
# Verify model file
file models/ggml-base.en.bin  # Should show "data"

# Use absolute path
./transcribe-file.sh audio.wav $(pwd)/../models/ggml-base.en.bin
```

### GPU Not Working

```bash
# Check CUDA
nvidia-smi

# Check if whisper.cpp has CUDA support
ldd ../build/src/libwhisper.so | grep cuda

# Rebuild with CUDA
cd ../build
cmake .. -DWHISPER_CUDA=ON
cmake --build . --config Release
```

## Performance Tips

### For Low Latency

Use smaller windows and steps:
```bash
whispertranscribe \
    window-duration-ms=1000 \
    step-duration-ms=500 \
    overlap-duration-ms=200
```

### For High Accuracy

Use larger windows with more overlap:
```bash
whispertranscribe \
    window-duration-ms=10000 \
    step-duration-ms=5000 \
    overlap-duration-ms=3000 \
    beam-size=10
```

### For Speed

Use GPU and multiple threads:
```bash
whispertranscribe \
    use-gpu=true \
    n-threads=8 \
    window-duration-ms=5000
```

## Contributing

Have a useful example? Contributions welcome!

1. Create your example script or program
2. Add documentation to this README
3. Test with different models and inputs
4. Submit a pull request

## See Also

- Main README: `../README.md`
- Quick Start Guide: `../QUICKSTART.md`
- Implementation Status: `../TDD_STATUS.md`
- whisper.cpp documentation: https://github.com/ggerganov/whisper.cpp
