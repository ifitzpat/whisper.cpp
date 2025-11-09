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

  /* Properties - to be implemented in next TDD cycle */
  gchar *model_path;
  gchar *language;
  gint n_threads;
  gfloat temperature;
  gboolean use_gpu;
  gboolean enable_vad;
};

struct _GstWhisperTranscribeClass {
  GstAudioFilterClass parent_class;
};

GType gst_whisper_transcribe_get_type (void);

G_END_DECLS

#endif /* __GST_WHISPER_TRANSCRIBE_H__ */
