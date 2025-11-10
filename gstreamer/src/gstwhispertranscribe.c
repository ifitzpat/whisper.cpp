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
#include <whisper.h>
#include <json-glib/json-glib.h>

GST_DEBUG_CATEGORY_STATIC (gst_whisper_transcribe_debug);
#define GST_CAT_DEFAULT gst_whisper_transcribe_debug

/* Phase 5: Work item for async transcription */
typedef struct _WhisperWorkItem {
  gfloat *audio_data;
  gsize n_samples;
  gint64 pts;
  GstWhisperTranscribe *filter;  /* Reference to element for signal emission */
} WhisperWorkItem;

/* Forward declarations for Phase 5 */
static gpointer gst_whisper_transcribe_worker_thread (gpointer data);
static void gst_whisper_transcribe_start_worker (GstWhisperTranscribe *filter);
static void gst_whisper_transcribe_stop_worker (GstWhisperTranscribe *filter);

/* Phase 6: Audio format conversion helpers */
static gfloat * gst_whisper_transcribe_convert_audio (GstWhisperTranscribe *filter,
    const guint8 *data, gsize size, gsize *out_samples);

/* Phase 8: JSON output helpers */
static gchar * gst_whisper_transcribe_create_json (GstWhisperTranscribe *filter,
    struct whisper_context *ctx, gint64 pts);
static void gst_whisper_transcribe_push_json (GstWhisperTranscribe *filter,
    const gchar *json_str, gint64 pts);

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

/* Phase 9: State management method declarations */
static gboolean gst_whisper_transcribe_start (GstBaseTransform * trans);
static gboolean gst_whisper_transcribe_stop (GstBaseTransform * trans);

/* Phase 7: Control pad method declarations */
static gboolean gst_whisper_transcribe_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_whisper_transcribe_src_query (GstBaseTransform * trans,
    GstQuery * query);

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

  /* Phase 9: State management */
  trans_class->start = GST_DEBUG_FUNCPTR (gst_whisper_transcribe_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_whisper_transcribe_stop);

  /* Phase 7: Control pad - event and query handling */
  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_whisper_transcribe_sink_event);
  trans_class->src_query = GST_DEBUG_FUNCPTR (gst_whisper_transcribe_src_query);

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

  /* Phase 4: Initialize managers */
  filter->whisper_ctx = whisper_context_manager_new ();
  filter->audio_buffer = NULL;  /* Created in setup when we know sample rate */
  g_mutex_init (&filter->lock);
  filter->model_loaded = FALSE;
  filter->sample_rate = 0;

  /* Phase 5: Initialize worker thread */
  filter->worker_thread = NULL;
  filter->work_queue = g_async_queue_new ();
  filter->worker_running = FALSE;
  g_mutex_init (&filter->worker_lock);
  g_cond_init (&filter->worker_cond);

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

      /* Phase 4: Load model when path is set */
      if (filter->model_path && filter->whisper_ctx) {
        GError *error = NULL;
        gboolean success = whisper_context_manager_load_model (
            filter->whisper_ctx, filter->model_path, filter->use_gpu, &error);

        if (success) {
          g_mutex_lock (&filter->lock);
          filter->model_loaded = TRUE;
          g_mutex_unlock (&filter->lock);

          GST_INFO_OBJECT (filter, "Model loaded successfully: %s", filter->model_path);
          g_signal_emit (filter, gst_whisper_transcribe_signals[SIGNAL_MODEL_LOADED],
              0, filter->model_path);

          /* Phase 5: Start worker thread */
          gst_whisper_transcribe_start_worker (filter);
        } else {
          g_mutex_lock (&filter->lock);
          filter->model_loaded = FALSE;
          g_mutex_unlock (&filter->lock);

          GST_ERROR_OBJECT (filter, "Failed to load model: %s",
              error ? error->message : "Unknown error");
          g_signal_emit (filter, gst_whisper_transcribe_signals[SIGNAL_MODEL_LOAD_FAILED],
              0, error ? error->message : "Unknown error");

          if (error) {
            g_error_free (error);
          }
        }
      }
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

  /* Phase 5: Stop worker thread */
  gst_whisper_transcribe_stop_worker (filter);

  /* Phase 4: Cleanup managers */
  if (filter->whisper_ctx) {
    whisper_context_manager_free (filter->whisper_ctx);
    filter->whisper_ctx = NULL;
  }

  if (filter->audio_buffer) {
    audio_buffer_manager_free (filter->audio_buffer);
    filter->audio_buffer = NULL;
  }

  /* Phase 5: Cleanup worker resources */
  if (filter->work_queue) {
    g_async_queue_unref (filter->work_queue);
    filter->work_queue = NULL;
  }
  g_mutex_clear (&filter->worker_lock);
  g_cond_clear (&filter->worker_cond);

  g_mutex_clear (&filter->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Phase 6: Audio format conversion helper */
static gfloat *
gst_whisper_transcribe_convert_audio (GstWhisperTranscribe *filter,
    const guint8 *data, gsize size, gsize *out_samples)
{
  GstAudioFormat format = GST_AUDIO_INFO_FORMAT (&filter->audio_info);
  gint channels = GST_AUDIO_INFO_CHANNELS (&filter->audio_info);
  gsize n_frames;
  gfloat *converted = NULL;
  gsize i, j;

  if (format == GST_AUDIO_FORMAT_F32LE) {
    /* Already float format */
    n_frames = size / (sizeof (gfloat) * channels);
    converted = g_new (gfloat, n_frames);

    if (channels == 1) {
      /* Mono - direct copy */
      memcpy (converted, data, size);
    } else {
      /* Stereo or multi-channel - average to mono */
      const gfloat *in = (const gfloat *) data;
      for (i = 0; i < n_frames; i++) {
        gfloat sum = 0.0f;
        for (j = 0; j < channels; j++) {
          sum += in[i * channels + j];
        }
        converted[i] = sum / channels;
      }
    }
    *out_samples = n_frames;

  } else if (format == GST_AUDIO_FORMAT_S16LE) {
    /* S16LE - convert to float and average channels */
    n_frames = size / (sizeof (gint16) * channels);
    converted = g_new (gfloat, n_frames);
    const gint16 *in = (const gint16 *) data;

    if (channels == 1) {
      /* Mono - convert to float */
      for (i = 0; i < n_frames; i++) {
        converted[i] = in[i] / 32768.0f;
      }
    } else {
      /* Stereo or multi-channel - average to mono and convert */
      for (i = 0; i < n_frames; i++) {
        gfloat sum = 0.0f;
        for (j = 0; j < channels; j++) {
          sum += in[i * channels + j] / 32768.0f;
        }
        converted[i] = sum / channels;
      }
    }
    *out_samples = n_frames;

  } else {
    GST_ERROR_OBJECT (filter, "Unsupported audio format: %s",
        gst_audio_format_to_string (format));
    *out_samples = 0;
    return NULL;
  }

  return converted;
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

  /* Phase 6: Store audio format info */
  gst_audio_info_init (&whisper->audio_info);
  gst_audio_info_copy (&whisper->audio_info, info);

  /* Phase 4: Create audio buffer manager with configured parameters */
  g_mutex_lock (&whisper->lock);

  /* Clean up old buffer if it exists */
  if (whisper->audio_buffer) {
    audio_buffer_manager_free (whisper->audio_buffer);
  }

  /* Store sample rate for later use */
  whisper->sample_rate = GST_AUDIO_INFO_RATE (info);

  /* Create new audio buffer manager */
  whisper->audio_buffer = audio_buffer_manager_new (
      whisper->window_duration_ms,
      whisper->step_duration_ms,
      whisper->overlap_duration_ms,
      whisper->sample_rate);

  g_mutex_unlock (&whisper->lock);

  GST_INFO_OBJECT (whisper, "Audio buffer manager created: "
      "window=%dms, step=%dms, overlap=%dms, rate=%dHz, channels=%d, format=%s",
      whisper->window_duration_ms, whisper->step_duration_ms,
      whisper->overlap_duration_ms, whisper->sample_rate,
      GST_AUDIO_INFO_CHANNELS (info),
      gst_audio_format_to_string (GST_AUDIO_INFO_FORMAT (info)));

  return TRUE;
}

/* Phase 8: Create JSON from transcription results */
static gchar *
gst_whisper_transcribe_create_json (GstWhisperTranscribe *filter,
    struct whisper_context *ctx, gint64 pts)
{
  JsonBuilder *builder;
  JsonGenerator *generator;
  JsonNode *root;
  gchar *json_str;
  gint n_segments;
  gint i;

  builder = json_builder_new ();

  /* Start root object */
  json_builder_begin_object (builder);

  /* Add metadata */
  json_builder_set_member_name (builder, "timestamp");
  json_builder_add_int_value (builder, pts);

  json_builder_set_member_name (builder, "language");
  json_builder_add_string_value (builder,
      filter->language ? filter->language : "auto");

  /* Add segments array */
  json_builder_set_member_name (builder, "segments");
  json_builder_begin_array (builder);

  n_segments = whisper_full_n_segments (ctx);
  for (i = 0; i < n_segments; i++) {
    const gchar *text = whisper_full_get_segment_text (ctx, i);
    gint64 t0 = whisper_full_get_segment_t0 (ctx, i);
    gint64 t1 = whisper_full_get_segment_t1 (ctx, i);

    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "id");
    json_builder_add_int_value (builder, i);

    json_builder_set_member_name (builder, "start");
    json_builder_add_double_value (builder, t0 / 100.0);  /* centiseconds to seconds */

    json_builder_set_member_name (builder, "end");
    json_builder_add_double_value (builder, t1 / 100.0);

    json_builder_set_member_name (builder, "text");
    json_builder_add_string_value (builder, text ? text : "");

    json_builder_end_object (builder);
  }

  json_builder_end_array (builder);  /* segments */
  json_builder_end_object (builder);  /* root */

  /* Generate JSON string */
  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);
  json_str = json_generator_to_data (generator, NULL);

  /* Cleanup */
  json_node_free (root);
  g_object_unref (generator);
  g_object_unref (builder);

  return json_str;
}

/* Phase 8: Push JSON buffer to src pad */
static void
gst_whisper_transcribe_push_json (GstWhisperTranscribe *filter,
    const gchar *json_str, gint64 pts)
{
  GstBuffer *buffer;
  GstMapInfo map;
  gsize json_len;

  if (!json_str) {
    GST_WARNING_OBJECT (filter, "NULL JSON string, skipping push");
    return;
  }

  json_len = strlen (json_str);

  /* Create buffer */
  buffer = gst_buffer_new_allocate (NULL, json_len, NULL);
  if (!buffer) {
    GST_ERROR_OBJECT (filter, "Failed to allocate buffer for JSON output");
    return;
  }

  /* Set buffer metadata */
  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_DTS (buffer) = pts;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;

  /* Copy JSON string to buffer */
  if (gst_buffer_map (buffer, &map, GST_MAP_WRITE)) {
    memcpy (map.data, json_str, json_len);
    gst_buffer_unmap (buffer, &map);
  } else {
    GST_ERROR_OBJECT (filter, "Failed to map buffer for writing");
    gst_buffer_unref (buffer);
    return;
  }

  /* Push to src pad */
  GstFlowReturn ret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (filter), buffer);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (filter, "Failed to push JSON buffer: %s",
        gst_flow_get_name (ret));
  } else {
    GST_DEBUG_OBJECT (filter, "Pushed JSON buffer (%zu bytes, PTS: %"
        GST_TIME_FORMAT ")", json_len, GST_TIME_ARGS (pts));
  }
}

/* Phase 5: Worker thread function for async transcription */
static gpointer
gst_whisper_transcribe_worker_thread (gpointer data)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (data);
  WhisperWorkItem *work_item;

  GST_INFO_OBJECT (filter, "Worker thread started");

  g_mutex_lock (&filter->worker_lock);
  while (filter->worker_running) {
    g_mutex_unlock (&filter->worker_lock);

    /* Wait for work with timeout */
    work_item = g_async_queue_timeout_pop (filter->work_queue, 100000); /* 100ms */

    if (!work_item) {
      g_mutex_lock (&filter->worker_lock);
      continue;
    }

    GST_DEBUG_OBJECT (filter, "Worker processing window: %zu samples, PTS: %"
        GST_TIME_FORMAT, work_item->n_samples, GST_TIME_ARGS (work_item->pts));

    /* Emit transcription started signal */
    g_signal_emit (filter, gst_whisper_transcribe_signals[SIGNAL_TRANSCRIPTION_STARTED],
        0, work_item->pts);

    /* Perform transcription */
    GError *error = NULL;
    struct whisper_full_params params = whisper_full_default_params (
        WHISPER_SAMPLING_GREEDY);

    /* Configure params from element properties */
    params.n_threads = filter->n_threads;
    params.language = filter->language && g_strcmp0 (filter->language, "auto") != 0
        ? filter->language : NULL;
    params.translate = filter->translate;
    params.temperature = filter->temperature;
    params.initial_prompt = filter->initial_prompt;

    gboolean success = whisper_context_manager_transcribe (
        filter->whisper_ctx, work_item->audio_data, work_item->n_samples,
        &params, &error);

    if (success) {
      /* Get the transcribed text from whisper context */
      struct whisper_context *ctx = whisper_context_manager_get_context (
          filter->whisper_ctx);

      if (ctx) {
        gint n_segments = whisper_full_n_segments (ctx);

        for (gint i = 0; i < n_segments; i++) {
          const gchar *text = whisper_full_get_segment_text (ctx, i);
          gint64 t0 = whisper_full_get_segment_t0 (ctx, i);
          gint64 t1 = whisper_full_get_segment_t1 (ctx, i);

          /* Create GstStructure with segment data */
          GstStructure *segment = gst_structure_new ("whisper-segment",
              "text", G_TYPE_STRING, text,
              "start-time", G_TYPE_INT64, t0 * 10000000LL,  /* Convert to ns */
              "end-time", G_TYPE_INT64, t1 * 10000000LL,
              "pts", G_TYPE_INT64, work_item->pts,
              NULL);

          GST_INFO_OBJECT (filter, "Segment %d: [%ld-%ld] %s", i, t0, t1, text);

          /* Emit segment transcribed signal */
          g_signal_emit (filter,
              gst_whisper_transcribe_signals[SIGNAL_SEGMENT_TRANSCRIBED],
              0, segment);

          gst_structure_free (segment);
        }

        /* Phase 8: Create and push JSON output */
        gchar *json_str = gst_whisper_transcribe_create_json (filter, ctx,
            work_item->pts);
        if (json_str) {
          gst_whisper_transcribe_push_json (filter, json_str, work_item->pts);
          g_free (json_str);
        }
      }

      /* Emit transcription completed signal */
      gint64 duration = work_item->n_samples * GST_SECOND / filter->sample_rate;
      g_signal_emit (filter,
          gst_whisper_transcribe_signals[SIGNAL_TRANSCRIPTION_COMPLETED],
          0, work_item->pts, duration);

    } else {
      GST_WARNING_OBJECT (filter, "Transcription failed: %s",
          error ? error->message : "Unknown error");
      if (error) {
        g_error_free (error);
      }
    }

    /* Free work item */
    g_free (work_item->audio_data);
    g_free (work_item);

    g_mutex_lock (&filter->worker_lock);
  }
  g_mutex_unlock (&filter->worker_lock);

  GST_INFO_OBJECT (filter, "Worker thread stopped");
  return NULL;
}

/* Phase 5: Start worker thread */
static void
gst_whisper_transcribe_start_worker (GstWhisperTranscribe *filter)
{
  g_mutex_lock (&filter->worker_lock);

  if (filter->worker_thread) {
    GST_DEBUG_OBJECT (filter, "Worker thread already running");
    g_mutex_unlock (&filter->worker_lock);
    return;
  }

  filter->worker_running = TRUE;
  filter->worker_thread = g_thread_new ("whisper-worker",
      gst_whisper_transcribe_worker_thread, filter);

  g_mutex_unlock (&filter->worker_lock);

  GST_INFO_OBJECT (filter, "Worker thread started");
}

/* Phase 5: Stop worker thread */
static void
gst_whisper_transcribe_stop_worker (GstWhisperTranscribe *filter)
{
  GThread *thread;

  g_mutex_lock (&filter->worker_lock);

  if (!filter->worker_thread) {
    g_mutex_unlock (&filter->worker_lock);
    return;
  }

  GST_INFO_OBJECT (filter, "Stopping worker thread");

  filter->worker_running = FALSE;
  thread = filter->worker_thread;
  filter->worker_thread = NULL;

  g_mutex_unlock (&filter->worker_lock);

  /* Wait for worker to finish */
  g_thread_join (thread);

  /* Drain remaining work items */
  WhisperWorkItem *work_item;
  while ((work_item = g_async_queue_try_pop (filter->work_queue)) != NULL) {
    g_free (work_item->audio_data);
    g_free (work_item);
  }

  GST_INFO_OBJECT (filter, "Worker thread stopped");
}

/* Phase 9: Start - called when going to PAUSED or PLAYING */
static gboolean
gst_whisper_transcribe_start (GstBaseTransform * trans)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (trans);

  GST_INFO_OBJECT (filter, "Starting element");

  /* Element is starting - ensure clean state */
  g_mutex_lock (&filter->lock);

  /* Audio buffer will be created in setup when caps are negotiated */
  /* Worker thread will be started when model is loaded */

  g_mutex_unlock (&filter->lock);

  GST_INFO_OBJECT (filter, "Element started successfully");
  return TRUE;
}

/* Phase 9: Stop - called when going to READY or NULL */
static gboolean
gst_whisper_transcribe_stop (GstBaseTransform * trans)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (trans);

  GST_INFO_OBJECT (filter, "Stopping element");

  /* Stop worker thread if running */
  gst_whisper_transcribe_stop_worker (filter);

  /* Clean up audio buffer */
  g_mutex_lock (&filter->lock);
  if (filter->audio_buffer) {
    GST_DEBUG_OBJECT (filter, "Cleaning up audio buffer manager");
    audio_buffer_manager_free (filter->audio_buffer);
    filter->audio_buffer = NULL;
  }
  g_mutex_unlock (&filter->lock);

  GST_INFO_OBJECT (filter, "Element stopped successfully");
  return TRUE;
}

/* Transform in-place - process audio buffers */
static GstFlowReturn
gst_whisper_transcribe_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (trans);
  GstMapInfo map;
  gfloat *audio_data;
  gsize n_samples;
  gint64 pts;

  GST_LOG_OBJECT (filter, "Processing buffer of size %" G_GSIZE_FORMAT,
      gst_buffer_get_size (buf));

  /* Check if model is loaded */
  g_mutex_lock (&filter->lock);
  gboolean model_ready = filter->model_loaded;
  g_mutex_unlock (&filter->lock);

  if (!model_ready) {
    GST_DEBUG_OBJECT (filter, "Model not loaded yet, skipping transcription");
    return GST_FLOW_OK;
  }

  /* Check if audio buffer manager is ready */
  if (!filter->audio_buffer) {
    GST_WARNING_OBJECT (filter, "Audio buffer manager not initialized");
    return GST_FLOW_OK;
  }

  /* Map the buffer to access audio data */
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (filter, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  /* Phase 6: Convert audio to mono F32LE format */
  audio_data = gst_whisper_transcribe_convert_audio (filter,
      map.data, map.size, &n_samples);

  if (!audio_data) {
    GST_ERROR_OBJECT (filter, "Failed to convert audio format");
    gst_buffer_unmap (buf, &map);
    return GST_FLOW_ERROR;
  }

  pts = GST_BUFFER_PTS (buf);

  GST_LOG_OBJECT (filter, "Pushing %zu samples to buffer (PTS: %" GST_TIME_FORMAT ")",
      n_samples, GST_TIME_ARGS (pts));

  /* Phase 4: Push audio data to buffer manager */
  audio_buffer_manager_push (filter->audio_buffer, audio_data, n_samples, pts);

  /* Free converted audio data */
  g_free (audio_data);

  /* Phase 5: Extract windows and queue for async transcription */
  gfloat *window_data = NULL;
  gsize window_size = 0;
  gint64 window_pts = 0;

  while (audio_buffer_manager_get_window (filter->audio_buffer,
          &window_data, &window_size, &window_pts)) {

    GST_DEBUG_OBJECT (filter, "Queueing window for transcription: %zu samples, PTS: %"
        GST_TIME_FORMAT, window_size, GST_TIME_ARGS (window_pts));

    /* Create work item with copy of audio data */
    WhisperWorkItem *work_item = g_new0 (WhisperWorkItem, 1);
    work_item->audio_data = g_memdup2 (window_data, window_size * sizeof (gfloat));
    work_item->n_samples = window_size;
    work_item->pts = window_pts;
    work_item->filter = filter;

    /* Push to async queue for worker thread to process */
    g_async_queue_push (filter->work_queue, work_item);

    GST_LOG_OBJECT (filter, "Work item queued, queue length: %d",
        g_async_queue_length (filter->work_queue));
  }

  gst_buffer_unmap (buf, &map);

  /* Pass through the buffer */
  return GST_FLOW_OK;
}

/* Phase 7: Sink event handler - handles events on the sink pad */
static gboolean
gst_whisper_transcribe_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (trans);
  gboolean ret = TRUE;

  GST_LOG_OBJECT (filter, "Received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (filter, "Handling FLUSH_START event");
      /* Stop worker thread from processing during flush */
      g_mutex_lock (&filter->worker_lock);
      g_mutex_unlock (&filter->worker_lock);
      break;

    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (filter, "Handling FLUSH_STOP event");
      /* Clear audio buffer on flush */
      if (filter->audio_buffer) {
        audio_buffer_manager_clear (filter->audio_buffer);
        GST_DEBUG_OBJECT (filter, "Audio buffer cleared after flush");
      }
      /* Clear work queue */
      if (filter->work_queue) {
        WhisperWorkItem *work_item;
        while ((work_item = g_async_queue_try_pop (filter->work_queue)) != NULL) {
          g_free (work_item->audio_data);
          g_free (work_item);
        }
        GST_DEBUG_OBJECT (filter, "Work queue cleared after flush");
      }
      break;

    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (filter, "Handling EOS event");
      /* Allow remaining buffered audio to be processed */
      /* The worker thread will finish processing queued items */
      break;

    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);
      GST_DEBUG_OBJECT (filter, "Received SEGMENT event: format=%s, "
          "start=%" GST_TIME_FORMAT ", stop=%" GST_TIME_FORMAT,
          gst_format_get_name (segment->format),
          GST_TIME_ARGS (segment->start),
          GST_TIME_ARGS (segment->stop));
      break;
    }

    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      GST_DEBUG_OBJECT (filter, "Received CAPS event: %" GST_PTR_FORMAT, caps);
      break;
    }

    default:
      break;
  }

  /* Chain up to parent class to handle the event */
  ret = GST_BASE_TRANSFORM_CLASS (gst_whisper_transcribe_parent_class)->sink_event (trans, event);

  return ret;
}

/* Phase 7: Source query handler - handles queries on the source pad */
static gboolean
gst_whisper_transcribe_src_query (GstBaseTransform * trans, GstQuery * query)
{
  GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE (trans);
  gboolean ret = TRUE;

  GST_LOG_OBJECT (filter, "Received %s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min_latency, max_latency;
      gboolean live;

      /* Chain up to get upstream latency first */
      ret = GST_BASE_TRANSFORM_CLASS (gst_whisper_transcribe_parent_class)->src_query (trans, query);

      if (ret) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);

        /* Add our processing latency (window duration) */
        GstClockTime our_latency = filter->window_duration_ms * GST_MSECOND;
        min_latency += our_latency;
        if (max_latency != GST_CLOCK_TIME_NONE) {
          max_latency += our_latency;
        }

        gst_query_set_latency (query, live, min_latency, max_latency);

        GST_DEBUG_OBJECT (filter, "Latency query: live=%d, min=%" GST_TIME_FORMAT
            ", max=%" GST_TIME_FORMAT " (added %dms)",
            live, GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency),
            filter->window_duration_ms);
      }
      break;
    }

    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gst_query_parse_position (query, &format, NULL);

      if (format == GST_FORMAT_TIME) {
        /* Query upstream for position */
        ret = GST_BASE_TRANSFORM_CLASS (gst_whisper_transcribe_parent_class)->src_query (trans, query);

        if (ret) {
          gint64 position;
          gst_query_parse_position (query, NULL, &position);
          GST_LOG_OBJECT (filter, "Position query: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (position));
        }
      } else {
        ret = FALSE;
      }
      break;
    }

    case GST_QUERY_DURATION:
    {
      /* Chain up to parent - we don't modify duration */
      ret = GST_BASE_TRANSFORM_CLASS (gst_whisper_transcribe_parent_class)->src_query (trans, query);

      if (ret) {
        GstFormat format;
        gint64 duration;
        gst_query_parse_duration (query, &format, &duration);
        GST_LOG_OBJECT (filter, "Duration query: format=%s, duration=%" GST_TIME_FORMAT,
            gst_format_get_name (format), GST_TIME_ARGS (duration));
      }
      break;
    }

    default:
      /* Chain up to parent class for other queries */
      ret = GST_BASE_TRANSFORM_CLASS (gst_whisper_transcribe_parent_class)->src_query (trans, query);
      break;
  }

  return ret;
}
