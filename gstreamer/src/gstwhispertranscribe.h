/* GStreamer Whisper Transcribe Element
 * Copyright (C) 2024 whisper.cpp contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the MIT License
 */

#ifndef __GST_WHISPER_TRANSCRIBE_H__
#define __GST_WHISPER_TRANSCRIBE_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>
#include "whispercontextmanager.h"
#include "audiobuffermanager.h"

G_BEGIN_DECLS

#define GST_TYPE_WHISPER_TRANSCRIBE \
  (gst_whisper_transcribe_get_type())
#define GST_WHISPER_TRANSCRIBE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WHISPER_TRANSCRIBE,GstWhisperTranscribe))
#define GST_WHISPER_TRANSCRIBE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WHISPER_TRANSCRIBE,GstWhisperTranscribeClass))
#define GST_IS_WHISPER_TRANSCRIBE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WHISPER_TRANSCRIBE))
#define GST_IS_WHISPER_TRANSCRIBE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WHISPER_TRANSCRIBE))

typedef struct _GstWhisperTranscribe      GstWhisperTranscribe;
typedef struct _GstWhisperTranscribeClass GstWhisperTranscribeClass;

/**
 * GstWhisperTranscribe:
 *
 * Opaque data structure.
 */
struct _GstWhisperTranscribe {
  GstAudioFilter parent;

  /* Basic properties */
  gchar *model_path;
  gchar *language;
  gint n_threads;
  gfloat temperature;
  gboolean use_gpu;
  gboolean enable_vad;

  /* Language and translation properties (TDD Cycle 3) */
  gboolean translate;
  gboolean detect_language;

  /* Sampling properties (TDD Cycle 3) */
  gint sampling_strategy;
  gint beam_size;
  gfloat entropy_threshold;
  gfloat logprob_threshold;
  gfloat no_speech_threshold;

  /* Context properties (TDD Cycle 3) */
  gchar *initial_prompt;

  /* Sliding window properties (TDD Cycle 3) */
  gint window_duration_ms;
  gint step_duration_ms;
  gint overlap_duration_ms;

  /* Phase 2: Whisper context management */
  WhisperContextManager *whisper_ctx;

  /* Phase 3: Audio buffer management */
  AudioBufferManager *audio_buffer;

  /* Phase 5: Worker thread for async processing */
  GThread *worker_thread;
  GAsyncQueue *work_queue;
  gboolean worker_running;
  GMutex worker_lock;
  GCond worker_cond;

  /* Thread safety */
  GMutex lock;

  /* State tracking */
  gboolean model_loaded;
  gint sample_rate;
};

struct _GstWhisperTranscribeClass {
  GstAudioFilterClass parent_class;
};

GType gst_whisper_transcribe_get_type (void);

G_END_DECLS

#endif /* __GST_WHISPER_TRANSCRIBE_H__ */
