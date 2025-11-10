#!/usr/bin/env python3
"""
Real-time transcription with signal monitoring using GStreamer whisper.cpp plugin

This example demonstrates how to:
- Create a GStreamer pipeline with whispertranscribe element
- Connect to signals for real-time monitoring
- Process transcription results as they arrive

Usage:
    python3 python-signals.py <audio-file> <model-path>

Example:
    python3 python-signals.py recording.wav ../models/ggml-base.en.bin

Requirements:
    pip install PyGObject
"""

import sys
import argparse
from pathlib import Path

import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib


class TranscriptionMonitor:
    """Monitor whispertranscribe element signals"""

    def __init__(self, audio_file, model_path):
        self.audio_file = audio_file
        self.model_path = model_path
        self.segments = []
        self.loop = None
        self.pipeline = None

    def on_model_loaded(self, element, model_path):
        """Called when model is loaded"""
        print(f"✓ Model loaded: {model_path}")

    def on_model_load_failed(self, element, error_message):
        """Called when model loading fails"""
        print(f"✗ Model load failed: {error_message}")
        if self.loop:
            self.loop.quit()

    def on_transcription_started(self, element, pts):
        """Called when transcription starts"""
        timestamp_sec = pts / 1e9  # Convert ns to seconds
        print(f"⟳ Transcription started at {timestamp_sec:.2f}s")

    def on_transcription_completed(self, element, pts, duration_ms):
        """Called when transcription completes"""
        timestamp_sec = pts / 1e9
        print(f"✓ Transcription completed at {timestamp_sec:.2f}s (took {duration_ms}ms)")

    def on_transcription_failed(self, element, pts, error_message):
        """Called when transcription fails"""
        timestamp_sec = pts / 1e9
        print(f"✗ Transcription failed at {timestamp_sec:.2f}s: {error_message}")

    def on_segment_available(self, element, segment_id, start_time, end_time, text):
        """Called when a transcription segment is available"""
        self.segments.append({
            'id': segment_id,
            'start': start_time,
            'end': end_time,
            'text': text
        })
        print(f"[{start_time:6.2f}s - {end_time:6.2f}s] {text}")

    def on_language_detected(self, element, language, probability):
        """Called when language is detected"""
        print(f"🌍 Language detected: {language} (confidence: {probability:.1%})")

    def on_buffer_level_changed(self, element, buffer_duration_ms):
        """Called when audio buffer level changes"""
        print(f"📊 Buffer level: {buffer_duration_ms}ms")

    def on_processing_stats(self, element, windows_processed, avg_time_ms):
        """Called with processing statistics"""
        print(f"📈 Stats: {windows_processed} windows, avg {avg_time_ms}ms/window")

    def on_bus_message(self, bus, message):
        """Handle GStreamer bus messages"""
        t = message.type

        if t == Gst.MessageType.EOS:
            print("\n✓ End of stream")
            self.print_summary()
            self.loop.quit()
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"\n✗ Error: {err}")
            print(f"Debug: {debug}")
            self.loop.quit()
        elif t == Gst.MessageType.WARNING:
            warn, debug = message.parse_warning()
            print(f"⚠ Warning: {warn}")

    def print_summary(self):
        """Print transcription summary"""
        print("\n" + "="*60)
        print("TRANSCRIPTION SUMMARY")
        print("="*60)
        print(f"Total segments: {len(self.segments)}")
        if self.segments:
            total_duration = self.segments[-1]['end'] - self.segments[0]['start']
            print(f"Duration: {total_duration:.2f}s")
            print("\nFull transcript:")
            print("-"*60)
            for seg in self.segments:
                print(seg['text'])
        print("="*60)

    def run(self):
        """Run the transcription pipeline"""
        # Initialize GStreamer
        Gst.init(None)

        # Build pipeline
        pipeline_str = f"""
            filesrc location="{self.audio_file}" !
            decodebin !
            audioconvert !
            audioresample !
            audio/x-raw,rate=16000 !
            whispertranscribe name=whisper model="{self.model_path}" language=en !
            fakesink
        """

        print(f"Creating pipeline...")
        print(f"Audio: {self.audio_file}")
        print(f"Model: {self.model_path}")
        print("="*60)

        try:
            self.pipeline = Gst.parse_launch(pipeline_str)
        except GLib.Error as e:
            print(f"Error creating pipeline: {e}")
            return 1

        # Get whisper element
        whisper = self.pipeline.get_by_name('whisper')
        if not whisper:
            print("Error: Could not get whisper element")
            return 1

        # Connect signals
        whisper.connect('model-loaded', self.on_model_loaded)
        whisper.connect('model-load-failed', self.on_model_load_failed)
        whisper.connect('transcription-started', self.on_transcription_started)
        whisper.connect('transcription-completed', self.on_transcription_completed)
        whisper.connect('transcription-failed', self.on_transcription_failed)
        whisper.connect('segment-available', self.on_segment_available)
        whisper.connect('language-detected', self.on_language_detected)
        whisper.connect('buffer-level-changed', self.on_buffer_level_changed)
        whisper.connect('processing-stats', self.on_processing_stats)

        # Set up bus
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect('message', self.on_bus_message)

        # Start pipeline
        print("Starting transcription...")
        print("="*60 + "\n")

        ret = self.pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            print("Error: Unable to set pipeline to PLAYING state")
            return 1

        # Run main loop
        self.loop = GLib.MainLoop()
        try:
            self.loop.run()
        except KeyboardInterrupt:
            print("\n\nInterrupted by user")

        # Cleanup
        self.pipeline.set_state(Gst.State.NULL)
        return 0


def main():
    parser = argparse.ArgumentParser(
        description='Real-time transcription with signal monitoring',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Transcribe audio file
  %(prog)s recording.wav ../models/ggml-base.en.bin

  # With automatic language detection
  %(prog)s speech.mp3 ../models/ggml-base.bin
        """
    )
    parser.add_argument('audio_file', help='Path to audio file')
    parser.add_argument('model_path', help='Path to whisper model (.bin)')

    args = parser.parse_args()

    # Check files exist
    if not Path(args.audio_file).exists():
        print(f"Error: Audio file not found: {args.audio_file}")
        return 1

    if not Path(args.model_path).exists():
        print(f"Error: Model file not found: {args.model_path}")
        return 1

    # Run transcription
    monitor = TranscriptionMonitor(args.audio_file, args.model_path)
    return monitor.run()


if __name__ == '__main__':
    sys.exit(main())
