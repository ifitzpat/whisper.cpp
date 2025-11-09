/* Audio Buffer Manager
 * Copyright (C) 2024 whisper.cpp contributors
 *
 * Manages audio buffering and sliding window mechanism
 */

#ifndef __AUDIO_BUFFER_MANAGER_H__
#define __AUDIO_BUFFER_MANAGER_H__

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _AudioBufferManager AudioBufferManager;
typedef struct _AudioChunk AudioChunk;

/**
 * AudioChunk:
 *
 * Represents a chunk of audio data with metadata.
 */
struct _AudioChunk {
  gfloat *data;     /* Audio samples (F32LE format) */
  gsize size;       /* Number of samples */
  gint64 pts;       /* Presentation timestamp in nanoseconds */
};

/**
 * AudioBufferManager:
 *
 * Manages audio chunks in a queue and provides sliding window
 * access for progressive transcription.
 */
struct _AudioBufferManager {
  GQueue *audio_chunks;        /* Queue of AudioChunk */
  gfloat *window_buffer;       /* Pre-allocated window buffer */
  gsize window_capacity;       /* Window size in samples */
  gsize overlap_size;          /* Overlap size in samples */
  gsize window_size;           /* Current window fill */
  gint sample_rate;            /* Audio sample rate (e.g., 16000) */
  GMutex mutex;                /* Thread safety */
};

/**
 * audio_buffer_manager_new:
 * @window_duration_ms: Window duration in milliseconds
 * @step_duration_ms: Step between windows in milliseconds
 * @overlap_duration_ms: Overlap between windows in milliseconds
 * @sample_rate: Audio sample rate (e.g., 16000 Hz)
 *
 * Creates a new AudioBufferManager instance.
 *
 * Returns: A new #AudioBufferManager. Free with audio_buffer_manager_free().
 */
AudioBufferManager * audio_buffer_manager_new (
    gint window_duration_ms,
    gint step_duration_ms,
    gint overlap_duration_ms,
    gint sample_rate);

/**
 * audio_buffer_manager_push:
 * @mgr: An #AudioBufferManager
 * @data: Audio sample data (F32LE format)
 * @size: Number of samples
 * @pts: Presentation timestamp in nanoseconds
 *
 * Pushes audio data into the buffer queue.
 */
void audio_buffer_manager_push (
    AudioBufferManager *mgr,
    const gfloat *data,
    gsize size,
    gint64 pts);

/**
 * audio_buffer_manager_get_window:
 * @mgr: An #AudioBufferManager
 * @out_data: (out): Pointer to window data
 * @out_size: (out): Number of samples in window
 * @out_pts: (out): Timestamp of window start
 *
 * Extracts a window of audio data if enough samples are available.
 * The window is filled from queued chunks and the queue is advanced
 * by step_duration samples.
 *
 * Returns: %TRUE if a window was extracted, %FALSE if not enough data
 */
gboolean audio_buffer_manager_get_window (
    AudioBufferManager *mgr,
    gfloat **out_data,
    gsize *out_size,
    gint64 *out_pts);

/**
 * audio_buffer_manager_clear:
 * @mgr: An #AudioBufferManager
 *
 * Clears all buffered audio data.
 */
void audio_buffer_manager_clear (AudioBufferManager *mgr);

/**
 * audio_buffer_manager_get_buffered_duration:
 * @mgr: An #AudioBufferManager
 *
 * Gets the duration of buffered audio in nanoseconds.
 *
 * Returns: Buffered duration in nanoseconds
 */
gint64 audio_buffer_manager_get_buffered_duration (AudioBufferManager *mgr);

/**
 * audio_buffer_manager_free:
 * @mgr: An #AudioBufferManager
 *
 * Frees an AudioBufferManager and all associated resources.
 */
void audio_buffer_manager_free (AudioBufferManager *mgr);

G_END_DECLS

#endif /* __AUDIO_BUFFER_MANAGER_H__ */
