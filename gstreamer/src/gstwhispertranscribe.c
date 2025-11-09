/* GStreamer Whisper Transcribe Element
 * Copyright (C) 2024 whisper.cpp contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the MIT License
 */

/**
 * SECTION:element-whispertranscribe
 * @title: whispertranscribe
 * @short_description: Speech-to-text transcription using whisper.cpp
 *
 * The whispertranscribe element transcribes audio to text using OpenAI's
 * Whisper model via the whisper.cpp library.
 *
 * ## Example pipeline
 * |[
 * gst-launch-1.0 filesrc location=audio.wav ! wavparse ! audioconvert ! \
 *   whispertranscribe model=ggml-base.en.bin language=en ! \
 *   fakesink dump=true
 * ]|
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwhispertranscribe.h"
#include <gst/gst.h>
#include <gst/audio/audio.h>

GST_DEBUG_CATEGORY_STATIC (gst_whisper_transcribe_debug);
#define GST_CAT_DEFAULT gst_whisper_transcribe_debug

/* Pad templates - minimal for now */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        "audio/x-raw, "
        "format = (string) { F32LE, S16LE }, "
        "rate = (int) { 8000, 16000, 22050, 44100, 48000 }, "
        "channels = (int) { 1, 2 }"
    )
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-json-transcription, format=whisper")
);

/* GObject method declarations */
static void gst_whisper_transcribe_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_whisper_transcribe_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_whisper_transcribe_finalize (GObject * object);

/* GstAudioFilter method declarations */
static gboolean gst_whisper_transcribe_setup (GstAudioFilter * filter,
    const GstAudioInfo * info);
static GstFlowReturn gst_whisper_transcribe_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

/* Property enum */
enum
{
  PROP_0,
  PROP_MODEL,
  PROP_LANGUAGE,
  PROP_N_THREADS,
  PROP_TEMPERATURE,
  PROP_USE_GPU,
  PROP_ENABLE_VAD,
  /* TDD Cycle 3 - Additional properties */
  PROP_TRANSLATE,
  PROP_DETECT_LANGUAGE,
  PROP_SAMPLING_STRATEGY,
  PROP_BEAM_SIZE,
  PROP_ENTROPY_THRESHOLD,
  PROP_LOGPROB_THRESHOLD,
  PROP_NO_SPEECH_THRESHOLD,
  PROP_INITIAL_PROMPT,
  PROP_WINDOW_DURATION,
  PROP_STEP_DURATION,
  PROP_OVERLAP_DURATION,
};

/* Signal enum */
enum
{
  SIGNAL_MODEL_LOADED,
  SIGNAL_MODEL_UNLOADED,
  SIGNAL_MODEL_LOAD_FAILED,
  SIGNAL_SEGMENT_TRANSCRIBED,
  SIGNAL_LANGUAGE_DETECTED,
  SIGNAL_TRANSCRIPTION_STARTED,
  SIGNAL_TRANSCRIPTION_COMPLETED,
  SIGNAL_VAD_SPEECH_DETECTED,
  SIGNAL_BUFFER_OVERFLOW,
  SIGNAL_MODEL_INFO,
  LAST_SIGNAL
};

static guint gst_whisper_transcribe_signals[LAST_SIGNAL] = { 0 };

/* Boilerplate GObject type definition */
#define gst_whisper_transcribe_parent_class parent_class
G_DEFINE_TYPE (GstWhisperTranscribe, gst_whisper_transcribe, GST_TYPE_AUDIO_FILTER);

/* Class initialization - called once when type is registered */
static void
gst_whisper_transcribe_class_init (GstWhisperTranscribeClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstAudioFilterClass *audio_filter_class = GST_AUDIO_FILTER_CLASS (klass);

  gobject_class->set_property = gst_whisper_transcribe_set_property;
  gobject_class->get_property = gst_whisper_transcribe_get_property;
  gobject_class->finalize = gst_whisper_transcribe_finalize;

  /* Properties - Phase 1 Step 1.3 */
  g_object_class_install_property (gobject_class, PROP_MODEL,
      g_param_spec_string ("model", "Model",
          "Path to whisper model file (.bin)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LANGUAGE,
      g_param_spec_string ("language", "Language",
          "Language code (e.g., 'en', 'fr', 'auto')",
          "auto", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_N_THREADS,
      g_param_spec_int ("n-threads", "Threads",
          "Number of CPU threads to use (-1 = auto)",
          -1, 128, 4, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TEMPERATURE,
      g_param_spec_float ("temperature", "Temperature",
          "Sampling temperature (0.0 - 1.0)",
          0.0, 1.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USE_GPU,
      g_param_spec_boolean ("use-gpu", "Use GPU",
          "Enable GPU acceleration if available",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENABLE_VAD,
      g_param_spec_boolean ("enable-vad", "Enable VAD",
          "Enable Voice Activity Detection",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Additional Properties - TDD Cycle 3 */

  g_object_class_install_property (gobject_class, PROP_TRANSLATE,
      g_param_spec_boolean ("translate", "Translate",
          "Translate from source language to English",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETECT_LANGUAGE,
      g_param_spec_boolean ("detect-language", "Detect Language",
          "Automatically detect the spoken language",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SAMPLING_STRATEGY,
      g_param_spec_int ("sampling-strategy", "Sampling Strategy",
          "Sampling strategy (0=GREEDY, 1=BEAM_SEARCH)",
          0, 1, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BEAM_SIZE,
      g_param_spec_int ("beam-size", "Beam Size",
          "Beam size for beam search sampling",
          1, 10, 5, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENTROPY_THRESHOLD,
      g_param_spec_float ("entropy-threshold", "Entropy Threshold",
          "Entropy threshold for decoder fallback",
          0.0, 10.0, 2.4, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOGPROB_THRESHOLD,
      g_param_spec_float ("logprob-threshold", "Log Probability Threshold",
          "Log probability threshold for decoder fallback",
          -10.0, 0.0, -1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NO_SPEECH_THRESHOLD,
      g_param_spec_float ("no-speech-threshold", "No Speech Threshold",
          "Probability threshold for no-speech detection",
          0.0, 1.0, 0.6, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INITIAL_PROMPT,
      g_param_spec_string ("initial-prompt", "Initial Prompt",
          "Optional text to provide as context for transcription",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_WINDOW_DURATION,
      g_param_spec_int ("window-duration", "Window Duration",
          "Sliding window duration in milliseconds",
          1000, 60000, 10000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STEP_DURATION,
      g_param_spec_int ("step-duration", "Step Duration",
          "Step between windows in milliseconds",
          100, 30000, 3000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OVERLAP_DURATION,
      g_param_spec_int ("overlap-duration", "Overlap Duration",
          "Overlap between windows in milliseconds",
          0, 5000, 200, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Signals - Phase 1 TDD Cycle 2 */

  /**
   * GstWhisperTranscribe::model-loaded:
   * @whispertranscribe: the whispertranscribe instance
   * @model_path: path to the loaded model file
   *
   * Emitted when a model has been successfully loaded.
   */
  gst_whisper_transcribe_signals[SIGNAL_MODEL_LOADED] =
      g_signal_new ("model-loaded",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          1, G_TYPE_STRING);

  /**
   * GstWhisperTranscribe::model-unloaded:
   * @whispertranscribe: the whispertranscribe instance
   *
   * Emitted when a model has been unloaded.
   */
  gst_whisper_transcribe_signals[SIGNAL_MODEL_UNLOADED] =
      g_signal_new ("model-unloaded",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          0);

  /**
   * GstWhisperTranscribe::model-load-failed:
   * @whispertranscribe: the whispertranscribe instance
   * @error_message: description of the error
   *
   * Emitted when model loading fails.
   */
  gst_whisper_transcribe_signals[SIGNAL_MODEL_LOAD_FAILED] =
      g_signal_new ("model-load-failed",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          1, G_TYPE_STRING);

  /**
   * GstWhisperTranscribe::segment-transcribed:
   * @whispertranscribe: the whispertranscribe instance
   * @segment_data: GstStructure containing segment information
   *
   * Emitted for each transcribed segment. The structure contains:
   * - text (string): transcribed text
   * - start-time (int64): start timestamp in nanoseconds
   * - end-time (int64): end timestamp in nanoseconds
   * - confidence (double): average confidence score
   * - language (string): detected language
   * - tokens (GstValueArray): array of token structures
   */
  gst_whisper_transcribe_signals[SIGNAL_SEGMENT_TRANSCRIBED] =
      g_signal_new ("segment-transcribed",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          1, GST_TYPE_STRUCTURE);

  /**
   * GstWhisperTranscribe::language-detected:
   * @whispertranscribe: the whispertranscribe instance
   * @language: detected language code (e.g., "en", "fr")
   * @probability: detection confidence (0.0 - 1.0)
   *
   * Emitted when language auto-detection completes.
   */
  gst_whisper_transcribe_signals[SIGNAL_LANGUAGE_DETECTED] =
      g_signal_new ("language-detected",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          2, G_TYPE_STRING, G_TYPE_FLOAT);

  /**
   * GstWhisperTranscribe::transcription-started:
   * @whispertranscribe: the whispertranscribe instance
   * @timestamp: start timestamp in nanoseconds
   *
   * Emitted when transcription of a new window begins.
   */
  gst_whisper_transcribe_signals[SIGNAL_TRANSCRIPTION_STARTED] =
      g_signal_new ("transcription-started",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          1, G_TYPE_INT64);

  /**
   * GstWhisperTranscribe::transcription-completed:
   * @whispertranscribe: the whispertranscribe instance
   * @timestamp: start timestamp in nanoseconds
   * @duration: processing duration in nanoseconds
   *
   * Emitted when transcription of a window completes.
   */
  gst_whisper_transcribe_signals[SIGNAL_TRANSCRIPTION_COMPLETED] =
      g_signal_new ("transcription-completed",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          2, G_TYPE_INT64, G_TYPE_INT64);

  /**
   * GstWhisperTranscribe::vad-speech-detected:
   * @whispertranscribe: the whispertranscribe instance
   * @timestamp: timestamp in nanoseconds
   * @is_speech: TRUE if speech detected, FALSE if silence
   *
   * Emitted when Voice Activity Detection state changes.
   */
  gst_whisper_transcribe_signals[SIGNAL_VAD_SPEECH_DETECTED] =
      g_signal_new ("vad-speech-detected",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          2, G_TYPE_INT64, G_TYPE_BOOLEAN);

  /**
   * GstWhisperTranscribe::buffer-overflow:
   * @whispertranscribe: the whispertranscribe instance
   * @dropped_samples: number of audio samples dropped
   *
   * Warning emitted when internal audio buffer overflows.
   */
  gst_whisper_transcribe_signals[SIGNAL_BUFFER_OVERFLOW] =
      g_signal_new ("buffer-overflow",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          1, G_TYPE_UINT64);

  /**
   * GstWhisperTranscribe::model-info:
   * @whispertranscribe: the whispertranscribe instance
   * @info: GstStructure containing model information
   *
   * Emitted after model is loaded with details. Structure contains:
   * - model-type (string): e.g., "base.en", "small", "large"
   * - is-multilingual (boolean): supports multiple languages
   * - sample-rate (int): required sample rate (16000)
   * - n-vocab (int): vocabulary size
   * - gpu-enabled (boolean): whether GPU acceleration is active
   */
  gst_whisper_transcribe_signals[SIGNAL_MODEL_INFO] =
      g_signal_new ("model-info",
          G_TYPE_FROM_CLASS (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE,
          1, GST_TYPE_STRUCTURE);

  /* Element metadata */
  gst_element_class_set_static_metadata (element_class,
      "Whisper Speech Transcriber",
      "Filter/Audio/Transcription",
      "Transcribes speech to text using whisper.cpp",
      "whisper.cpp contributors");

  /* Pad templates - Phase 1 Step 1.4 */
  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  /* BaseTransform configuration */
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_whisper_transcribe_transform_ip);
  trans_class->passthrough_on_same_caps = FALSE;

  /* AudioFilter configuration */
  audio_filter_class->setup = GST_DEBUG_FUNCPTR (gst_whisper_transcribe_setup);

  GST_DEBUG_CATEGORY_INIT (gst_whisper_transcribe_debug, "whispertranscribe",
      0, "Whisper speech transcription element");
}

/* Instance initialization - called for each new element instance */
static void
gst_whisper_transcribe_init (GstWhisperTranscribe * filter)
{
  /* Initialize basic properties to defaults */
  filter->model_path = NULL;
  filter->language = g_strdup ("auto");
  filter->n_threads = 4;
  filter->temperature = 0.0;
  filter->use_gpu = FALSE;
  filter->enable_vad = TRUE;

  /* Initialize additional properties (TDD Cycle 3) */
  filter->translate = FALSE;
  filter->detect_language = TRUE;
  filter->sampling_strategy = 0;  /* GREEDY */
  filter->beam_size = 5;
  filter->entropy_threshold = 2.4;
  filter->logprob_threshold = -1.0;
  filter->no_speech_threshold = 0.6;
  filter->initial_prompt = NULL;
  filter->window_duration_ms = 10000;  /* 10 seconds */
  filter->step_duration_ms = 3000;     /* 3 seconds */
  filter->overlap_duration_ms = 200;   /* 200 ms */

  GST_DEBUG_OBJECT (filter, "Initialized whispertranscribe element");
}

/* Property setter */
static void
gst_whisper_transcribe_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_free (filter->model_path);
      filter->model_path = g_value_dup_string (value);
      GST_DEBUG_OBJECT (filter, "Model path set to: %s", filter->model_path);
      break;
    case PROP_LANGUAGE:
      g_free (filter->language);
      filter->language = g_value_dup_string (value);
      GST_DEBUG_OBJECT (filter, "Language set to: %s", filter->language);
      break;
    case PROP_N_THREADS:
      filter->n_threads = g_value_get_int (value);
      GST_DEBUG_OBJECT (filter, "Threads set to: %d", filter->n_threads);
      break;
    case PROP_TEMPERATURE:
      filter->temperature = g_value_get_float (value);
      GST_DEBUG_OBJECT (filter, "Temperature set to: %f", filter->temperature);
      break;
    case PROP_USE_GPU:
      filter->use_gpu = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (filter, "Use GPU set to: %d", filter->use_gpu);
      break;
    case PROP_ENABLE_VAD:
      filter->enable_vad = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (filter, "Enable VAD set to: %d", filter->enable_vad);
      break;
    case PROP_TRANSLATE:
      filter->translate = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (filter, "Translate set to: %d", filter->translate);
      break;
    case PROP_DETECT_LANGUAGE:
      filter->detect_language = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (filter, "Detect language set to: %d", filter->detect_language);
      break;
    case PROP_SAMPLING_STRATEGY:
      filter->sampling_strategy = g_value_get_int (value);
      GST_DEBUG_OBJECT (filter, "Sampling strategy set to: %d", filter->sampling_strategy);
      break;
    case PROP_BEAM_SIZE:
      filter->beam_size = g_value_get_int (value);
      GST_DEBUG_OBJECT (filter, "Beam size set to: %d", filter->beam_size);
      break;
    case PROP_ENTROPY_THRESHOLD:
      filter->entropy_threshold = g_value_get_float (value);
      GST_DEBUG_OBJECT (filter, "Entropy threshold set to: %f", filter->entropy_threshold);
      break;
    case PROP_LOGPROB_THRESHOLD:
      filter->logprob_threshold = g_value_get_float (value);
      GST_DEBUG_OBJECT (filter, "Log prob threshold set to: %f", filter->logprob_threshold);
      break;
    case PROP_NO_SPEECH_THRESHOLD:
      filter->no_speech_threshold = g_value_get_float (value);
      GST_DEBUG_OBJECT (filter, "No-speech threshold set to: %f", filter->no_speech_threshold);
      break;
    case PROP_INITIAL_PROMPT:
      g_free (filter->initial_prompt);
      filter->initial_prompt = g_value_dup_string (value);
      GST_DEBUG_OBJECT (filter, "Initial prompt set to: %s", filter->initial_prompt);
      break;
    case PROP_WINDOW_DURATION:
      filter->window_duration_ms = g_value_get_int (value);
      GST_DEBUG_OBJECT (filter, "Window duration set to: %d ms", filter->window_duration_ms);
      break;
    case PROP_STEP_DURATION:
      filter->step_duration_ms = g_value_get_int (value);
      GST_DEBUG_OBJECT (filter, "Step duration set to: %d ms", filter->step_duration_ms);
      break;
    case PROP_OVERLAP_DURATION:
      filter->overlap_duration_ms = g_value_get_int (value);
      GST_DEBUG_OBJECT (filter, "Overlap duration set to: %d ms", filter->overlap_duration_ms);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Property getter */
static void
gst_whisper_transcribe_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (object);

  switch (prop_id) {
    case PROP_MODEL:
      g_value_set_string (value, filter->model_path);
      break;
    case PROP_LANGUAGE:
      g_value_set_string (value, filter->language);
      break;
    case PROP_N_THREADS:
      g_value_set_int (value, filter->n_threads);
      break;
    case PROP_TEMPERATURE:
      g_value_set_float (value, filter->temperature);
      break;
    case PROP_USE_GPU:
      g_value_set_boolean (value, filter->use_gpu);
      break;
    case PROP_ENABLE_VAD:
      g_value_set_boolean (value, filter->enable_vad);
      break;
    case PROP_TRANSLATE:
      g_value_set_boolean (value, filter->translate);
      break;
    case PROP_DETECT_LANGUAGE:
      g_value_set_boolean (value, filter->detect_language);
      break;
    case PROP_SAMPLING_STRATEGY:
      g_value_set_int (value, filter->sampling_strategy);
      break;
    case PROP_BEAM_SIZE:
      g_value_set_int (value, filter->beam_size);
      break;
    case PROP_ENTROPY_THRESHOLD:
      g_value_set_float (value, filter->entropy_threshold);
      break;
    case PROP_LOGPROB_THRESHOLD:
      g_value_set_float (value, filter->logprob_threshold);
      break;
    case PROP_NO_SPEECH_THRESHOLD:
      g_value_set_float (value, filter->no_speech_threshold);
      break;
    case PROP_INITIAL_PROMPT:
      g_value_set_string (value, filter->initial_prompt);
      break;
    case PROP_WINDOW_DURATION:
      g_value_set_int (value, filter->window_duration_ms);
      break;
    case PROP_STEP_DURATION:
      g_value_set_int (value, filter->step_duration_ms);
      break;
    case PROP_OVERLAP_DURATION:
      g_value_set_int (value, filter->overlap_duration_ms);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Finalization */
static void
gst_whisper_transcribe_finalize (GObject * object)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (object);

  g_free (filter->model_path);
  g_free (filter->language);
  g_free (filter->initial_prompt);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* AudioFilter setup - called when audio format is negotiated */
static gboolean
gst_whisper_transcribe_setup (GstAudioFilter * filter,
    const GstAudioInfo * info)
{
  GstWhisperTranscribe *whisper = GST_WHISPER_TRANSCRIBE (filter);

  GST_DEBUG_OBJECT (whisper, "Audio setup: rate=%d, channels=%d, format=%s",
      GST_AUDIO_INFO_RATE (info),
      GST_AUDIO_INFO_CHANNELS (info),
      gst_audio_format_to_string (GST_AUDIO_INFO_FORMAT (info)));

  /* TODO: Initialize whisper context, audio buffers, etc. in later phases */

  return TRUE;
}

/* Transform in-place - process audio buffers */
static GstFlowReturn
gst_whisper_transcribe_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (trans);

  /* TODO: Actual audio processing will be implemented in later phases */
  GST_DEBUG_OBJECT (filter, "Processing buffer of size %" G_GSIZE_FORMAT,
      gst_buffer_get_size (buf));

  /* For now, just pass through */
  return GST_FLOW_OK;
}
