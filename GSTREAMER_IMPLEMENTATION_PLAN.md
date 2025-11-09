# GStreamer Whisper.cpp Filter - Implementation Plan

## Overview

This document outlines the implementation plan for a GStreamer audio filter element that integrates whisper.cpp for real-time speech transcription. The element will accept audio packets from sources like webrtcbin and pipewire, perform speech-to-text transcription, and output JSON metadata with sentences and timing information.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Component Design](#component-design)
3. [Detailed Implementation Steps](#detailed-implementation-steps)
4. [API and Interface Design](#api-and-interface-design)
5. [Sliding Window Mechanism](#sliding-window-mechanism)
6. [Testing Strategy](#testing-strategy)
7. [Performance Considerations](#performance-considerations)
8. [Future Enhancements](#future-enhancements)

---

## Architecture Overview

### Element Name: `whispertranscribe`

### Pads Configuration

```
┌─────────────────────────────────────────┐
│                                         │
│  sink (audio)  →  whispertranscribe  →  src (metadata/JSON)
│                        ↕                │
│                   control (pad)         │
│                                         │
└─────────────────────────────────────────┘
```

#### 1. **Sink Pad (Audio Input)**
- **Direction**: Sink (input)
- **Type**: Always pad
- **Capabilities**:
  ```
  audio/x-raw,
    format = { F32LE, S16LE },
    rate = { 8000, 16000, 22050, 44100, 48000 },
    channels = { 1, 2 }
  ```
- **Notes**: Will internally convert to 16kHz mono F32LE as required by whisper.cpp

#### 2. **Source Pad (Metadata Output)**
- **Direction**: Source (output)
- **Type**: Always pad
- **Capabilities**:
  ```
  application/x-json-transcription,
    format = whisper
  ```
- **Output Format**: JSON with structure:
  ```json
  {
    "type": "segment",
    "timestamp_start_ms": 1500,
    "timestamp_end_ms": 3200,
    "text": "Hello world",
    "language": "en",
    "confidence": 0.95,
    "tokens": [
      {
        "text": "Hello",
        "probability": 0.98,
        "start_ms": 1500,
        "end_ms": 1800
      },
      {
        "text": " world",
        "probability": 0.92,
        "start_ms": 1800,
        "end_ms": 3200
      }
    ],
    "no_speech_prob": 0.02
  }
  ```

#### 3. **Control Pad (Optional Request Pad)**
- **Direction**: Sink (input)
- **Type**: Request pad (on-demand)
- **Purpose**: Accept control commands for:
  - Model loading/unloading
  - Parameter changes
  - Runtime configuration
- **Format**: Structured commands (GstStructure or JSON)

---

## Component Design

### Base Classes

**Primary Base**: `GstAudioFilter` (from gst-libs/gst/audio)
- Handles audio format negotiation
- Provides setup() callback for format changes
- Inherits from GstBaseTransform

**Alternative**: `GstBaseTransform` (if more control needed)
- More flexibility for custom buffering
- Manual audio format handling required

**Recommendation**: Start with `GstAudioFilter` for simpler audio handling, migrate to `GstBaseTransform` if buffering requirements become complex.

### Internal Components

#### 1. **Whisper Context Manager**
```c
typedef struct {
    struct whisper_context *ctx;
    gchar *model_path;
    gboolean is_loaded;
    GMutex mutex;  // Thread safety for model operations
} WhisperContextManager;
```

#### 2. **Audio Buffer Manager**
```c
typedef struct {
    GQueue *audio_chunks;      // Queue of audio segments
    gfloat *window_buffer;     // Sliding window buffer
    gsize window_size;         // Current window size in samples
    gsize window_capacity;     // Max window capacity
    gsize overlap_size;        // Overlap between windows
    gint64 pts_offset;         // PTS tracking
    GMutex mutex;              // Thread safety
} AudioBufferManager;
```

#### 3. **Transcription State**
```c
typedef struct {
    GQueue *pending_segments;   // Segments awaiting refinement
    gchar *current_context;     // Recent text for prompting
    gsize context_size;         // Max context length
    gint *prompt_tokens;        // Token array for context
    gsize n_prompt_tokens;      // Number of prompt tokens
    GMutex mutex;
} TranscriptionState;
```

#### 4. **Element Structure**
```c
typedef struct _GstWhisperTranscribe {
    GstAudioFilter parent;

    /* Properties */
    gchar *model_path;
    gchar *language;
    gchar *initial_prompt;
    gboolean translate;
    gboolean detect_language;
    gint n_threads;
    gfloat temperature;
    gboolean use_gpu;
    gboolean enable_vad;

    /* Sampling strategy */
    gint sampling_strategy;  // GREEDY or BEAM_SEARCH
    gint beam_size;
    gfloat entropy_threshold;
    gfloat logprob_threshold;
    gfloat no_speech_threshold;

    /* Sliding window parameters */
    gint window_duration_ms;     // e.g., 10000 (10 seconds)
    gint step_duration_ms;       // e.g., 3000 (3 seconds)
    gint overlap_duration_ms;    // e.g., 200 (0.2 seconds)

    /* Internal state */
    WhisperContextManager *whisper_ctx;
    AudioBufferManager *audio_buffer;
    TranscriptionState *transcription_state;
    GstAudioInfo audio_info;

    /* Output pad */
    GstPad *srcpad;

    /* Control pad */
    GstPad *controlpad;  // Request pad

    /* Threading */
    GThread *worker_thread;
    gboolean worker_running;
    GCond worker_cond;
    GMutex worker_mutex;

} GstWhisperTranscribe;
```

---

## Detailed Implementation Steps

### Phase 1: Basic Element Setup (Week 1)

#### Step 1.1: Project Structure Setup
```
whisper.cpp/
├── gstreamer/
│   ├── meson.build
│   ├── src/
│   │   ├── gstwhispertranscribe.c
│   │   ├── gstwhispertranscribe.h
│   │   ├── gstwhisperplugin.c          # Plugin registration
│   │   ├── whispercontextmanager.c     # Model management
│   │   ├── whispercontextmanager.h
│   │   ├── audiobuffermanager.c        # Audio buffering
│   │   ├── audiobuffermanager.h
│   │   └── utils.c                     # JSON formatting, etc.
│   ├── tests/
│   │   ├── test_basic.c
│   │   └── test_pipelines.c
│   └── examples/
│       ├── simple_transcribe.c
│       └── webrtc_transcribe.c
```

#### Step 1.2: Boilerplate Generation
- Use `gst-element-maker whispertranscribe audiofilter` as starting point
- Or manually create boilerplate following GStreamer Plugin Writer's Guide
- Implement class_init, init, finalize functions

#### Step 1.3: Property and Signal Registration
Register all properties using g_object_class_install_property():
```c
// In class_init()
g_object_class_install_property(gobject_class, PROP_MODEL_PATH,
    g_param_spec_string("model", "Model Path",
        "Path to whisper model file (.bin)",
        NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

g_object_class_install_property(gobject_class, PROP_LANGUAGE,
    g_param_spec_string("language", "Language",
        "Language code (e.g., 'en', 'fr', 'auto')",
        "auto", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

// ... (continue for all properties)
```

Register all signals using g_signal_new():
```c
// Signal registration (see "GObject Signals" section for complete implementation)
gst_whisper_transcribe_signals[SIGNAL_MODEL_LOADED] =
    g_signal_new("model-loaded", G_TYPE_FROM_CLASS(klass), ...);

gst_whisper_transcribe_signals[SIGNAL_SEGMENT_TRANSCRIBED] =
    g_signal_new("segment-transcribed", G_TYPE_FROM_CLASS(klass), ...);

// ... (register all 10 signals - see API section for details)
```

#### Step 1.4: Pad Template Definition
```c
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "audio/x-raw, "
        "format = (string) { F32LE, S16LE }, "
        "rate = (int) { 8000, 16000, 22050, 44100, 48000 }, "
        "channels = (int) { 1, 2 }")
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("application/x-json-transcription, format=whisper")
);
```

### Phase 2: Whisper.cpp Integration (Week 1-2)

#### Step 2.1: Context Manager Implementation
```c
WhisperContextManager* whisper_context_manager_new() {
    WhisperContextManager *mgr = g_new0(WhisperContextManager, 1);
    g_mutex_init(&mgr->mutex);
    mgr->ctx = NULL;
    mgr->model_path = NULL;
    mgr->is_loaded = FALSE;
    return mgr;
}

gboolean whisper_context_manager_load_model(
    WhisperContextManager *mgr,
    const gchar *model_path,
    gboolean use_gpu,
    GError **error
) {
    g_mutex_lock(&mgr->mutex);

    // Unload existing model if loaded
    if (mgr->ctx) {
        whisper_free(mgr->ctx);
        mgr->ctx = NULL;
    }

    // Initialize context parameters
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = use_gpu;
    cparams.flash_attn = use_gpu;  // Enable flash attention with GPU

    // Load model
    mgr->ctx = whisper_init_from_file_with_params(model_path, cparams);

    if (!mgr->ctx) {
        g_set_error(error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
            "Failed to load whisper model from: %s", model_path);
        g_mutex_unlock(&mgr->mutex);
        return FALSE;
    }

    g_free(mgr->model_path);
    mgr->model_path = g_strdup(model_path);
    mgr->is_loaded = TRUE;

    g_mutex_unlock(&mgr->mutex);
    return TRUE;
}

void whisper_context_manager_free(WhisperContextManager *mgr) {
    if (mgr->ctx) {
        whisper_free(mgr->ctx);
    }
    g_free(mgr->model_path);
    g_mutex_clear(&mgr->mutex);
    g_free(mgr);
}
```

#### Step 2.2: Basic Transcription Function
```c
gboolean whisper_context_manager_transcribe(
    WhisperContextManager *mgr,
    const gfloat *audio_data,
    gsize n_samples,
    struct whisper_full_params *params,
    GError **error
) {
    g_mutex_lock(&mgr->mutex);

    if (!mgr->is_loaded) {
        g_set_error(error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
            "Model not loaded");
        g_mutex_unlock(&mgr->mutex);
        return FALSE;
    }

    int ret = whisper_full(mgr->ctx, *params, audio_data, n_samples);

    g_mutex_unlock(&mgr->mutex);

    if (ret != 0) {
        g_set_error(error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
            "Transcription failed with code: %d", ret);
        return FALSE;
    }

    return TRUE;
}
```

### Phase 3: Audio Buffer Management (Week 2)

#### Step 3.1: Audio Buffer Manager Implementation
```c
AudioBufferManager* audio_buffer_manager_new(
    gint window_duration_ms,
    gint overlap_duration_ms,
    gint sample_rate
) {
    AudioBufferManager *mgr = g_new0(AudioBufferManager, 1);
    g_mutex_init(&mgr->mutex);

    mgr->audio_chunks = g_queue_new();

    // Calculate buffer sizes in samples
    mgr->window_capacity = (window_duration_ms * sample_rate) / 1000;
    mgr->overlap_size = (overlap_duration_ms * sample_rate) / 1000;

    mgr->window_buffer = g_malloc0(mgr->window_capacity * sizeof(gfloat));
    mgr->window_size = 0;
    mgr->pts_offset = 0;

    return mgr;
}

typedef struct {
    gfloat *data;
    gsize size;
    gint64 pts;  // Presentation timestamp
} AudioChunk;

void audio_buffer_manager_push(
    AudioBufferManager *mgr,
    const gfloat *data,
    gsize size,
    gint64 pts
) {
    g_mutex_lock(&mgr->mutex);

    AudioChunk *chunk = g_new(AudioChunk, 1);
    chunk->data = g_memdup2(data, size * sizeof(gfloat));
    chunk->size = size;
    chunk->pts = pts;

    g_queue_push_tail(mgr->audio_chunks, chunk);

    g_mutex_unlock(&mgr->mutex);
}

gboolean audio_buffer_manager_get_window(
    AudioBufferManager *mgr,
    gfloat **out_data,
    gsize *out_size,
    gint64 *out_pts
) {
    g_mutex_lock(&mgr->mutex);

    gsize total_available = 0;
    GList *iter;

    // Calculate total available samples
    for (iter = mgr->audio_chunks->head; iter; iter = iter->next) {
        AudioChunk *chunk = iter->data;
        total_available += chunk->size;
    }

    if (total_available < mgr->window_capacity) {
        g_mutex_unlock(&mgr->mutex);
        return FALSE;  // Not enough data yet
    }

    // Fill window buffer
    mgr->window_size = 0;
    gint64 first_pts = -1;

    while (mgr->window_size < mgr->window_capacity && !g_queue_is_empty(mgr->audio_chunks)) {
        AudioChunk *chunk = g_queue_peek_head(mgr->audio_chunks);

        if (first_pts == -1) {
            first_pts = chunk->pts;
        }

        gsize to_copy = MIN(chunk->size, mgr->window_capacity - mgr->window_size);
        memcpy(mgr->window_buffer + mgr->window_size, chunk->data, to_copy * sizeof(gfloat));
        mgr->window_size += to_copy;

        if (to_copy < chunk->size) {
            // Partial consumption, adjust chunk
            memmove(chunk->data, chunk->data + to_copy, (chunk->size - to_copy) * sizeof(gfloat));
            chunk->size -= to_copy;
            chunk->pts += (to_copy * GST_SECOND) / WHISPER_SAMPLE_RATE;
            break;
        } else {
            // Full consumption, remove chunk
            AudioChunk *consumed = g_queue_pop_head(mgr->audio_chunks);
            g_free(consumed->data);
            g_free(consumed);
        }
    }

    *out_data = mgr->window_buffer;
    *out_size = mgr->window_size;
    *out_pts = first_pts;

    g_mutex_unlock(&mgr->mutex);
    return TRUE;
}

void audio_buffer_manager_advance_window(
    AudioBufferManager *mgr,
    gint step_duration_ms,
    gint sample_rate
) {
    gsize step_size = (step_duration_ms * sample_rate) / 1000;
    gsize keep_size = mgr->window_size - step_size;

    // Keep overlap samples for next window
    if (keep_size > 0 && keep_size <= mgr->overlap_size) {
        // Put the overlap back into the queue
        AudioChunk *overlap = g_new(AudioChunk, 1);
        overlap->data = g_memdup2(
            mgr->window_buffer + step_size,
            keep_size * sizeof(gfloat)
        );
        overlap->size = keep_size;
        overlap->pts = mgr->pts_offset + (step_size * GST_SECOND) / sample_rate;

        g_queue_push_head(mgr->audio_chunks, overlap);
    }

    mgr->window_size = 0;
}
```

### Phase 4: Sliding Window Transcription (Week 2-3)

#### Step 4.1: Segment Refinement Strategy

The sliding window mechanism will work similar to faster-whisper:

1. **Initial Pass**: Process audio window with basic transcription
2. **Refinement Passes**: Re-process with adjusted prompts and parameters
3. **Probability Tracking**: Maintain probabilities for each word across passes
4. **Final Selection**: Choose highest probability words for final output

```c
typedef struct {
    gchar *text;
    gfloat probability;
    gint64 t0_ms;
    gint64 t1_ms;
    gint occurrence_count;
} WordCandidate;

typedef struct {
    GList *candidates;  // List of WordCandidate
    gint64 segment_start_ms;
    gint64 segment_end_ms;
} SegmentRefinement;

typedef struct {
    GQueue *refinement_queue;  // Queue of SegmentRefinement
    gint max_refinement_passes;
    gfloat min_confidence_threshold;
} RefinementManager;

RefinementManager* refinement_manager_new() {
    RefinementManager *mgr = g_new0(RefinementManager, 1);
    mgr->refinement_queue = g_queue_new();
    mgr->max_refinement_passes = 3;
    mgr->min_confidence_threshold = 0.7;
    return mgr;
}

void refinement_manager_add_segment(
    RefinementManager *mgr,
    struct whisper_context *ctx,
    gint segment_idx
) {
    SegmentRefinement *ref = g_new0(SegmentRefinement, 1);
    ref->candidates = NULL;
    ref->segment_start_ms = whisper_full_get_segment_t0(ctx, segment_idx) * 10;
    ref->segment_end_ms = whisper_full_get_segment_t1(ctx, segment_idx) * 10;

    // Extract tokens as candidates
    gint n_tokens = whisper_full_n_tokens(ctx, segment_idx);
    for (gint i = 0; i < n_tokens; i++) {
        struct whisper_token_data token_data = whisper_full_get_token_data(ctx, segment_idx, i);

        // Skip special tokens
        if (token_data.id >= whisper_token_eot(ctx)) continue;

        WordCandidate *candidate = g_new0(WordCandidate, 1);
        candidate->text = g_strdup(whisper_full_get_token_text(ctx, segment_idx, i));
        candidate->probability = token_data.p;
        candidate->t0_ms = token_data.t0 * 10;
        candidate->t1_ms = token_data.t1 * 10;
        candidate->occurrence_count = 1;

        ref->candidates = g_list_append(ref->candidates, candidate);
    }

    g_queue_push_tail(mgr->refinement_queue, ref);
}

void refinement_manager_update_probabilities(
    RefinementManager *mgr,
    struct whisper_context *ctx,
    gint segment_idx
) {
    if (g_queue_is_empty(mgr->refinement_queue)) return;

    SegmentRefinement *ref = g_queue_peek_tail(mgr->refinement_queue);

    // Update probabilities based on new transcription pass
    gint n_tokens = whisper_full_n_tokens(ctx, segment_idx);
    for (gint i = 0; i < n_tokens; i++) {
        struct whisper_token_data token_data = whisper_full_get_token_data(ctx, segment_idx, i);
        const gchar *token_text = whisper_full_get_token_text(ctx, segment_idx, i);

        // Find matching candidate
        gboolean found = FALSE;
        for (GList *iter = ref->candidates; iter; iter = iter->next) {
            WordCandidate *candidate = iter->data;
            if (g_strcmp0(candidate->text, token_text) == 0) {
                // Update running average of probability
                candidate->probability =
                    (candidate->probability * candidate->occurrence_count + token_data.p) /
                    (candidate->occurrence_count + 1);
                candidate->occurrence_count++;
                found = TRUE;
                break;
            }
        }

        // Add new candidate if not found
        if (!found) {
            WordCandidate *candidate = g_new0(WordCandidate, 1);
            candidate->text = g_strdup(token_text);
            candidate->probability = token_data.p;
            candidate->t0_ms = token_data.t0 * 10;
            candidate->t1_ms = token_data.t1 * 10;
            candidate->occurrence_count = 1;
            ref->candidates = g_list_append(ref->candidates, candidate);
        }
    }
}

gchar* refinement_manager_get_best_transcription(
    RefinementManager *mgr,
    gfloat *avg_confidence
) {
    if (g_queue_is_empty(mgr->refinement_queue)) return NULL;

    SegmentRefinement *ref = g_queue_pop_head(mgr->refinement_queue);

    GString *result = g_string_new("");
    gfloat total_prob = 0.0;
    gint count = 0;

    // Sort candidates by time
    ref->candidates = g_list_sort(ref->candidates, (GCompareFunc)compare_by_time);

    for (GList *iter = ref->candidates; iter; iter = iter->next) {
        WordCandidate *candidate = iter->data;

        // Only include high-confidence tokens
        if (candidate->probability >= mgr->min_confidence_threshold) {
            g_string_append(result, candidate->text);
            total_prob += candidate->probability;
            count++;
        }

        g_free(candidate->text);
        g_free(candidate);
    }

    g_list_free(ref->candidates);
    g_free(ref);

    *avg_confidence = (count > 0) ? (total_prob / count) : 0.0;

    return g_string_free(result, FALSE);
}
```

#### Step 4.2: Multi-Pass Transcription
```c
gboolean perform_sliding_window_transcription(
    GstWhisperTranscribe *filter,
    const gfloat *audio_data,
    gsize n_samples,
    gint64 pts,
    GError **error
) {
    // Emit transcription-started signal
    gint64 start_time = g_get_monotonic_time() * 1000;  // Convert to nanoseconds
    emit_transcription_lifecycle_signals(filter, pts, TRUE);

    struct whisper_full_params wparams =
        whisper_full_default_params(filter->sampling_strategy);

    // Configure parameters from element properties
    wparams.n_threads = filter->n_threads;
    wparams.language = filter->language;
    wparams.translate = filter->translate;
    wparams.detect_language = filter->detect_language;
    wparams.temperature = filter->temperature;
    wparams.entropy_thold = filter->entropy_threshold;
    wparams.logprob_thold = filter->logprob_threshold;
    wparams.no_speech_thold = filter->no_speech_threshold;
    wparams.enable_vad = filter->enable_vad;

    // Enable token timestamps for precise timing
    wparams.token_timestamps = TRUE;

    // Use prompt from previous transcriptions for context
    if (filter->transcription_state->prompt_tokens) {
        wparams.prompt_tokens = filter->transcription_state->prompt_tokens;
        wparams.prompt_n_tokens = filter->transcription_state->n_prompt_tokens;
    }

    // PASS 1: Initial transcription with current parameters
    if (!whisper_context_manager_transcribe(
            filter->whisper_ctx, audio_data, n_samples, &wparams, error)) {
        return FALSE;
    }

    // Emit language detection signal if auto-detect is enabled
    if (filter->detect_language) {
        emit_language_detected_signal(filter, filter->whisper_ctx->ctx);
    }

    // Extract initial segments
    RefinementManager *refiner = refinement_manager_new();
    gint n_segments = whisper_full_n_segments(filter->whisper_ctx->ctx);

    for (gint i = 0; i < n_segments; i++) {
        refinement_manager_add_segment(refiner, filter->whisper_ctx->ctx, i);
    }

    // PASS 2-N: Refinement with temperature adjustments
    for (gint pass = 1; pass < refiner->max_refinement_passes; pass++) {
        // Adjust temperature for diversity
        wparams.temperature = filter->temperature + (pass * 0.1);

        if (!whisper_context_manager_transcribe(
                filter->whisper_ctx, audio_data, n_samples, &wparams, error)) {
            break;  // Non-critical error, use what we have
        }

        // Update probabilities with new results
        n_segments = whisper_full_n_segments(filter->whisper_ctx->ctx);
        for (gint i = 0; i < n_segments && i < g_queue_get_length(refiner->refinement_queue); i++) {
            refinement_manager_update_probabilities(refiner, filter->whisper_ctx->ctx, i);
        }
    }

    // Extract best transcription and emit segment signal
    gfloat confidence = 0.0;
    gchar *final_text = refinement_manager_get_best_transcription(refiner, &confidence);

    // Get language for segment signal
    gint lang_id = whisper_full_lang_id(filter->whisper_ctx->ctx);
    const gchar *language = whisper_lang_str(lang_id);

    // Calculate timestamps
    gint64 duration_ns = (n_samples * GST_SECOND) / WHISPER_SAMPLE_RATE;
    gint64 end_time_ns = pts + duration_ns;

    // Emit segment-transcribed signal with detailed data
    if (n_segments > 0) {
        emit_segment_transcribed_signal(
            filter,
            final_text,
            pts,
            end_time_ns,
            confidence,
            language,
            filter->whisper_ctx->ctx,
            n_segments - 1  // Last segment
        );
    }

    // Create and push JSON output
    push_transcription_output(filter, final_text, pts, n_samples, confidence);

    // Update context for next iteration
    update_transcription_context(filter, final_text);

    g_free(final_text);
    refinement_manager_free(refiner);

    // Emit transcription-completed signal
    gint64 end_time = g_get_monotonic_time() * 1000;
    gint64 processing_duration = end_time - start_time;
    g_signal_emit(filter,
        gst_whisper_transcribe_signals[SIGNAL_TRANSCRIPTION_COMPLETED],
        0, pts, processing_duration);

    return TRUE;
}
```

### Phase 5: Worker Thread and Processing Loop (Week 3)

#### Step 5.1: Worker Thread Implementation
```c
static gpointer worker_thread_func(gpointer user_data) {
    GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE(user_data);

    g_mutex_lock(&filter->worker_mutex);

    while (filter->worker_running) {
        // Wait for signal or timeout (check every 100ms)
        gint64 end_time = g_get_monotonic_time() + 100 * G_TIME_SPAN_MILLISECOND;
        g_cond_wait_until(&filter->worker_cond, &filter->worker_mutex, end_time);

        if (!filter->worker_running) break;

        // Check if we have enough audio for processing
        gfloat *audio_window = NULL;
        gsize window_size = 0;
        gint64 pts = 0;

        if (!audio_buffer_manager_get_window(
                filter->audio_buffer, &audio_window, &window_size, &pts)) {
            continue;  // Not enough data yet
        }

        g_mutex_unlock(&filter->worker_mutex);

        // Perform transcription (releases mutex during processing)
        GError *error = NULL;
        perform_sliding_window_transcription(filter, audio_window, window_size, pts, &error);

        if (error) {
            GST_ERROR_OBJECT(filter, "Transcription error: %s", error->message);
            g_error_free(error);
        }

        g_mutex_lock(&filter->worker_mutex);

        // Advance window for next iteration
        audio_buffer_manager_advance_window(
            filter->audio_buffer,
            filter->step_duration_ms,
            WHISPER_SAMPLE_RATE
        );
    }

    g_mutex_unlock(&filter->worker_mutex);
    return NULL;
}

static gboolean start_worker_thread(GstWhisperTranscribe *filter) {
    g_mutex_lock(&filter->worker_mutex);

    if (filter->worker_thread) {
        g_mutex_unlock(&filter->worker_mutex);
        return TRUE;  // Already running
    }

    filter->worker_running = TRUE;
    filter->worker_thread = g_thread_new("whisper-worker", worker_thread_func, filter);

    g_mutex_unlock(&filter->worker_mutex);
    return TRUE;
}

static void stop_worker_thread(GstWhisperTranscribe *filter) {
    g_mutex_lock(&filter->worker_mutex);

    if (!filter->worker_thread) {
        g_mutex_unlock(&filter->worker_mutex);
        return;
    }

    filter->worker_running = FALSE;
    g_cond_signal(&filter->worker_cond);

    g_mutex_unlock(&filter->worker_mutex);

    g_thread_join(filter->worker_thread);
    filter->worker_thread = NULL;
}
```

### Phase 6: Transform Implementation (Week 3-4)

#### Step 6.1: GstAudioFilter Callbacks
```c
static gboolean gst_whisper_transcribe_setup(
    GstAudioFilter *filter,
    const GstAudioInfo *info
) {
    GstWhisperTranscribe *whisper = GST_WHISPER_TRANSCRIBE(filter);

    // Store audio info
    whisper->audio_info = *info;

    // Validate format
    if (GST_AUDIO_INFO_RATE(info) != WHISPER_SAMPLE_RATE &&
        GST_AUDIO_INFO_RATE(info) != 48000 &&
        GST_AUDIO_INFO_RATE(info) != 44100) {
        GST_ERROR_OBJECT(filter, "Unsupported sample rate: %d",
            GST_AUDIO_INFO_RATE(info));
        return FALSE;
    }

    // Initialize audio buffer manager
    if (whisper->audio_buffer) {
        audio_buffer_manager_free(whisper->audio_buffer);
    }

    whisper->audio_buffer = audio_buffer_manager_new(
        whisper->window_duration_ms,
        whisper->overlap_duration_ms,
        WHISPER_SAMPLE_RATE
    );

    // Start worker thread
    start_worker_thread(whisper);

    return TRUE;
}

static GstFlowReturn gst_whisper_transcribe_transform_ip(
    GstBaseTransform *trans,
    GstBuffer *buf
) {
    GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE(trans);
    GstMapInfo map;

    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(filter, "Failed to map buffer");
        return GST_FLOW_ERROR;
    }

    // Convert audio to required format (16kHz mono F32LE)
    gfloat *converted_audio = NULL;
    gsize n_samples = 0;

    convert_audio_to_whisper_format(
        filter,
        map.data,
        map.size,
        &converted_audio,
        &n_samples
    );

    // Push to audio buffer
    audio_buffer_manager_push(
        filter->audio_buffer,
        converted_audio,
        n_samples,
        GST_BUFFER_PTS(buf)
    );

    // Signal worker thread
    g_mutex_lock(&filter->worker_mutex);
    g_cond_signal(&filter->worker_cond);
    g_mutex_unlock(&filter->worker_mutex);

    g_free(converted_audio);
    gst_buffer_unmap(buf, &map);

    // This element doesn't modify the audio, just analyzes it
    // Audio passes through unchanged
    return GST_FLOW_OK;
}
```

#### Step 6.2: Audio Format Conversion
```c
static void convert_audio_to_whisper_format(
    GstWhisperTranscribe *filter,
    const guint8 *input_data,
    gsize input_size,
    gfloat **output_data,
    gsize *output_samples
) {
    GstAudioInfo *info = &filter->audio_info;
    gint input_rate = GST_AUDIO_INFO_RATE(info);
    gint input_channels = GST_AUDIO_INFO_CHANNELS(info);
    GstAudioFormat input_format = GST_AUDIO_INFO_FORMAT(info);

    // Calculate number of input samples
    gsize n_input_samples = input_size / GST_AUDIO_INFO_BPF(info);

    // Step 1: Convert to F32LE if needed
    gfloat *f32_data = NULL;

    if (input_format == GST_AUDIO_FORMAT_F32LE) {
        f32_data = (gfloat *)input_data;
    } else if (input_format == GST_AUDIO_FORMAT_S16LE) {
        // Convert S16LE to F32LE
        f32_data = g_malloc(n_input_samples * input_channels * sizeof(gfloat));
        const gint16 *s16_data = (const gint16 *)input_data;

        for (gsize i = 0; i < n_input_samples * input_channels; i++) {
            f32_data[i] = s16_data[i] / 32768.0f;
        }
    }

    // Step 2: Convert to mono if needed
    gfloat *mono_data = NULL;
    gsize n_mono_samples = n_input_samples;

    if (input_channels == 1) {
        mono_data = f32_data;
    } else {
        // Average channels to mono
        mono_data = g_malloc(n_input_samples * sizeof(gfloat));

        for (gsize i = 0; i < n_input_samples; i++) {
            gfloat sum = 0.0f;
            for (gint ch = 0; ch < input_channels; ch++) {
                sum += f32_data[i * input_channels + ch];
            }
            mono_data[i] = sum / input_channels;
        }

        if (input_format == GST_AUDIO_FORMAT_S16LE) {
            g_free(f32_data);  // Free intermediate conversion
        }
    }

    // Step 3: Resample to 16kHz if needed
    if (input_rate == WHISPER_SAMPLE_RATE) {
        *output_data = mono_data;
        *output_samples = n_mono_samples;
    } else {
        // Simple linear interpolation resampling
        // For production, use GstAudioResampler or a proper library
        gsize n_output_samples = (n_mono_samples * WHISPER_SAMPLE_RATE) / input_rate;
        gfloat *resampled = g_malloc(n_output_samples * sizeof(gfloat));

        gfloat ratio = (gfloat)input_rate / WHISPER_SAMPLE_RATE;

        for (gsize i = 0; i < n_output_samples; i++) {
            gfloat src_pos = i * ratio;
            gsize src_idx = (gsize)src_pos;
            gfloat frac = src_pos - src_idx;

            if (src_idx + 1 < n_mono_samples) {
                resampled[i] = mono_data[src_idx] * (1.0f - frac) +
                               mono_data[src_idx + 1] * frac;
            } else {
                resampled[i] = mono_data[src_idx];
            }
        }

        if (input_channels > 1 || input_format == GST_AUDIO_FORMAT_S16LE) {
            g_free(mono_data);
        }

        *output_data = resampled;
        *output_samples = n_output_samples;
    }
}
```

### Phase 7: Control Pad Implementation (Week 4)

#### Step 7.1: Control Pad Setup
```c
static GstPad* create_control_pad(GstWhisperTranscribe *filter) {
    GstPad *pad = gst_pad_new_from_static_template(&control_template, "control");

    gst_pad_set_chain_function(pad,
        GST_DEBUG_FUNCPTR(gst_whisper_transcribe_control_chain));
    gst_pad_set_event_function(pad,
        GST_DEBUG_FUNCPTR(gst_whisper_transcribe_control_event));

    gst_element_add_pad(GST_ELEMENT(filter), pad);

    return pad;
}

static GstFlowReturn gst_whisper_transcribe_control_chain(
    GstPad *pad,
    GstObject *parent,
    GstBuffer *buf
) {
    GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE(parent);
    GstMapInfo map;

    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        return GST_FLOW_ERROR;
    }

    // Parse control message (assume JSON format)
    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_data(parser, (const gchar *)map.data, map.size, &error)) {
        GST_ERROR_OBJECT(filter, "Failed to parse control message: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        gst_buffer_unmap(buf, &map);
        return GST_FLOW_ERROR;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    // Process control commands
    if (json_object_has_member(obj, "command")) {
        const gchar *command = json_object_get_string_member(obj, "command");

        if (g_strcmp0(command, "load_model") == 0) {
            handle_load_model_command(filter, obj);
        } else if (g_strcmp0(command, "unload_model") == 0) {
            handle_unload_model_command(filter);
        } else if (g_strcmp0(command, "set_language") == 0) {
            handle_set_language_command(filter, obj);
        } else if (g_strcmp0(command, "set_prompt") == 0) {
            handle_set_prompt_command(filter, obj);
        } else if (g_strcmp0(command, "set_temperature") == 0) {
            handle_set_temperature_command(filter, obj);
        }
        // ... handle other commands
    }

    g_object_unref(parser);
    gst_buffer_unmap(buf, &map);
    gst_buffer_unref(buf);

    return GST_FLOW_OK;
}
```

#### Step 7.2: Control Command Handlers
```c
static void handle_load_model_command(
    GstWhisperTranscribe *filter,
    JsonObject *params
) {
    if (!json_object_has_member(params, "model_path")) {
        GST_ERROR_OBJECT(filter, "load_model requires model_path parameter");
        return;
    }

    const gchar *model_path = json_object_get_string_member(params, "model_path");
    gboolean use_gpu = filter->use_gpu;

    if (json_object_has_member(params, "use_gpu")) {
        use_gpu = json_object_get_boolean_member(params, "use_gpu");
    }

    GError *error = NULL;
    if (!whisper_context_manager_load_model(filter->whisper_ctx, model_path, use_gpu, &error)) {
        GST_ERROR_OBJECT(filter, "Failed to load model: %s", error->message);
        g_error_free(error);
        return;
    }

    g_free(filter->model_path);
    filter->model_path = g_strdup(model_path);
    filter->use_gpu = use_gpu;

    GST_INFO_OBJECT(filter, "Model loaded successfully: %s", model_path);
}

static void handle_set_language_command(
    GstWhisperTranscribe *filter,
    JsonObject *params
) {
    if (!json_object_has_member(params, "language")) {
        return;
    }

    const gchar *language = json_object_get_string_member(params, "language");

    g_free(filter->language);
    filter->language = g_strdup(language);

    GST_INFO_OBJECT(filter, "Language set to: %s", language);
}

static void handle_set_prompt_command(
    GstWhisperTranscribe *filter,
    JsonObject *params
) {
    if (!json_object_has_member(params, "prompt")) {
        return;
    }

    const gchar *prompt = json_object_get_string_member(params, "prompt");

    g_free(filter->initial_prompt);
    filter->initial_prompt = g_strdup(prompt);

    // Convert prompt to tokens if model is loaded
    if (filter->whisper_ctx->is_loaded) {
        // Tokenize the prompt
        // This requires whisper_tokenize() API
        // Store in filter->transcription_state->prompt_tokens
    }

    GST_INFO_OBJECT(filter, "Prompt updated");
}
```

### Phase 8: JSON Output Generation (Week 4)

#### Step 8.1: JSON Formatting
```c
static void push_transcription_output(
    GstWhisperTranscribe *filter,
    const gchar *text,
    gint64 pts,
    gsize n_samples,
    gfloat confidence
) {
    // Calculate timestamps
    gint64 duration_ns = (n_samples * GST_SECOND) / WHISPER_SAMPLE_RATE;
    gint64 start_ms = pts / GST_MSECOND;
    gint64 end_ms = (pts + duration_ns) / GST_MSECOND;

    // Get detected language
    gint lang_id = whisper_full_lang_id(filter->whisper_ctx->ctx);
    const gchar *language = whisper_lang_str(lang_id);

    // Get no-speech probability (from first segment)
    gfloat no_speech_prob = 0.0f;
    if (whisper_full_n_segments(filter->whisper_ctx->ctx) > 0) {
        // This would require additional API - approximate for now
        no_speech_prob = 0.02f;  // Placeholder
    }

    // Build JSON
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, "segment");

    json_builder_set_member_name(builder, "timestamp_start_ms");
    json_builder_add_int_value(builder, start_ms);

    json_builder_set_member_name(builder, "timestamp_end_ms");
    json_builder_add_int_value(builder, end_ms);

    json_builder_set_member_name(builder, "text");
    json_builder_add_string_value(builder, text);

    json_builder_set_member_name(builder, "language");
    json_builder_add_string_value(builder, language);

    json_builder_set_member_name(builder, "confidence");
    json_builder_add_double_value(builder, confidence);

    json_builder_set_member_name(builder, "no_speech_prob");
    json_builder_add_double_value(builder, no_speech_prob);

    // Add token details
    json_builder_set_member_name(builder, "tokens");
    json_builder_begin_array(builder);

    // Extract tokens from last segment
    gint n_segments = whisper_full_n_segments(filter->whisper_ctx->ctx);
    if (n_segments > 0) {
        gint seg_idx = n_segments - 1;  // Last segment
        gint n_tokens = whisper_full_n_tokens(filter->whisper_ctx->ctx, seg_idx);

        for (gint i = 0; i < n_tokens; i++) {
            struct whisper_token_data token_data =
                whisper_full_get_token_data(filter->whisper_ctx->ctx, seg_idx, i);
            const gchar *token_text =
                whisper_full_get_token_text(filter->whisper_ctx->ctx, seg_idx, i);

            json_builder_begin_object(builder);

            json_builder_set_member_name(builder, "text");
            json_builder_add_string_value(builder, token_text);

            json_builder_set_member_name(builder, "probability");
            json_builder_add_double_value(builder, token_data.p);

            json_builder_set_member_name(builder, "start_ms");
            json_builder_add_int_value(builder, token_data.t0 * 10);

            json_builder_set_member_name(builder, "end_ms");
            json_builder_add_int_value(builder, token_data.t1 * 10);

            json_builder_end_object(builder);
        }
    }

    json_builder_end_array(builder);
    json_builder_end_object(builder);

    // Generate JSON string
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    gchar *json_str = json_generator_to_data(generator, NULL);

    // Create output buffer
    gsize json_len = strlen(json_str);
    GstBuffer *out_buf = gst_buffer_new_allocate(NULL, json_len, NULL);
    gst_buffer_fill(out_buf, 0, json_str, json_len);

    // Set timestamps
    GST_BUFFER_PTS(out_buf) = pts;
    GST_BUFFER_DURATION(out_buf) = duration_ns;

    // Push to source pad
    gst_pad_push(filter->srcpad, out_buf);

    // Cleanup
    g_free(json_str);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
}
```

### Phase 9: State Management and Lifecycle (Week 5)

#### Step 9.1: Element Lifecycle
```c
static GstStateChangeReturn gst_whisper_transcribe_change_state(
    GstElement *element,
    GstStateChange transition
) {
    GstWhisperTranscribe *filter = GST_WHISPER_TRANSCRIBE(element);
    GstStateChangeReturn ret;

    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            // Initialize whisper context manager
            if (!filter->whisper_ctx) {
                filter->whisper_ctx = whisper_context_manager_new();
            }

            // Load model if path is set
            if (filter->model_path) {
                GError *error = NULL;
                if (!whisper_context_manager_load_model(
                        filter->whisper_ctx,
                        filter->model_path,
                        filter->use_gpu,
                        &error)) {
                    GST_ERROR_OBJECT(filter, "Failed to load model: %s", error->message);
                    g_error_free(error);
                    return GST_STATE_CHANGE_FAILURE;
                }
            }
            break;

        case GST_STATE_CHANGE_READY_TO_PAUSED:
            // Initialize transcription state
            if (!filter->transcription_state) {
                filter->transcription_state = transcription_state_new();
            }
            break;

        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            // Start worker thread if not already running
            start_worker_thread(filter);
            break;

        default:
            break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition) {
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            // Pause processing but keep state
            break;

        case GST_STATE_CHANGE_PAUSED_TO_READY:
            // Stop worker thread
            stop_worker_thread(filter);

            // Clear transcription state
            if (filter->transcription_state) {
                transcription_state_free(filter->transcription_state);
                filter->transcription_state = NULL;
            }

            // Clear audio buffer
            if (filter->audio_buffer) {
                audio_buffer_manager_free(filter->audio_buffer);
                filter->audio_buffer = NULL;
            }
            break;

        case GST_STATE_CHANGE_READY_TO_NULL:
            // Unload model and free context
            if (filter->whisper_ctx) {
                whisper_context_manager_free(filter->whisper_ctx);
                filter->whisper_ctx = NULL;
            }
            break;

        default:
            break;
    }

    return ret;
}
```

---

## API and Interface Design

### GObject Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `model` | string | NULL | Path to whisper model file |
| `language` | string | "auto" | Language code (en, fr, es, auto, etc.) |
| `translate` | boolean | FALSE | Translate to English |
| `detect-language` | boolean | TRUE | Auto-detect language |
| `n-threads` | int | 4 | Number of CPU threads |
| `temperature` | float | 0.0 | Sampling temperature |
| `use-gpu` | boolean | FALSE | Enable GPU acceleration |
| `enable-vad` | boolean | TRUE | Enable voice activity detection |
| `sampling-strategy` | enum | GREEDY | GREEDY or BEAM_SEARCH |
| `beam-size` | int | 5 | Beam size for beam search |
| `entropy-threshold` | float | 2.4 | Entropy threshold for decoder |
| `logprob-threshold` | float | -1.0 | Log probability threshold |
| `no-speech-threshold` | float | 0.6 | No-speech probability threshold |
| `initial-prompt` | string | NULL | Initial context prompt |
| `window-duration` | int | 10000 | Sliding window duration (ms) |
| `step-duration` | int | 3000 | Step between windows (ms) |
| `overlap-duration` | int | 200 | Overlap between windows (ms) |

### GObject Signals

The element emits various signals to notify applications of important events during model lifecycle and transcription.

#### Signal Definitions

| Signal | Arguments | Description |
|--------|-----------|-------------|
| `model-loaded` | `model-path` (string) | Emitted when model is successfully loaded |
| `model-unloaded` | - | Emitted when model is unloaded |
| `model-load-failed` | `error-message` (string) | Emitted when model loading fails |
| `segment-transcribed` | `segment-data` (GstStructure) | Emitted for each transcribed segment |
| `language-detected` | `language` (string), `probability` (float) | Emitted when language is auto-detected |
| `transcription-started` | `timestamp` (int64) | Emitted when window transcription begins |
| `transcription-completed` | `timestamp` (int64), `duration` (int64) | Emitted when window transcription finishes |
| `vad-speech-detected` | `timestamp` (int64), `is-speech` (boolean) | Emitted on VAD state changes |
| `buffer-overflow` | `dropped-samples` (uint64) | Warning when audio buffer overflows |
| `model-info` | `info` (GstStructure) | Emitted after model load with model details |

#### Signal Registration

```c
enum {
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

// In class_init():
static void gst_whisper_transcribe_class_init(GstWhisperTranscribeClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    // ... (property registration)

    /**
     * GstWhisperTranscribe::model-loaded:
     * @whispertranscribe: the whispertranscribe instance
     * @model_path: path to the loaded model file
     *
     * Emitted when a model has been successfully loaded.
     */
    gst_whisper_transcribe_signals[SIGNAL_MODEL_LOADED] =
        g_signal_new("model-loaded",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("model-unloaded",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("model-load-failed",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("segment-transcribed",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("language-detected",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("transcription-started",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("transcription-completed",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("vad-speech-detected",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("buffer-overflow",
            G_TYPE_FROM_CLASS(klass),
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
        g_signal_new("model-info",
            G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL, NULL,
            NULL,
            G_TYPE_NONE,
            1, GST_TYPE_STRUCTURE);
}
```

#### Emitting Signals - Implementation Examples

##### Model Loading Signals

```c
static void emit_model_loaded_signal(
    GstWhisperTranscribe *filter,
    const gchar *model_path
) {
    g_signal_emit(filter,
        gst_whisper_transcribe_signals[SIGNAL_MODEL_LOADED],
        0, model_path);

    // Also emit model info
    GstStructure *info = gst_structure_new("model-info",
        "model-type", G_TYPE_STRING,
            whisper_model_type_readable(filter->whisper_ctx->ctx),
        "is-multilingual", G_TYPE_BOOLEAN,
            whisper_is_multilingual(filter->whisper_ctx->ctx),
        "sample-rate", G_TYPE_INT, WHISPER_SAMPLE_RATE,
        "n-vocab", G_TYPE_INT,
            whisper_n_vocab(filter->whisper_ctx->ctx),
        "gpu-enabled", G_TYPE_BOOLEAN, filter->use_gpu,
        NULL);

    g_signal_emit(filter,
        gst_whisper_transcribe_signals[SIGNAL_MODEL_INFO],
        0, info);

    gst_structure_free(info);

    GST_INFO_OBJECT(filter, "Model loaded: %s", model_path);
}

// Updated model loading function
gboolean whisper_context_manager_load_model(
    WhisperContextManager *mgr,
    const gchar *model_path,
    gboolean use_gpu,
    GstWhisperTranscribe *filter,  // Add filter parameter
    GError **error
) {
    g_mutex_lock(&mgr->mutex);

    // ... (existing loading code)

    if (!mgr->ctx) {
        g_set_error(error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
            "Failed to load whisper model from: %s", model_path);
        g_mutex_unlock(&mgr->mutex);

        // Emit failure signal
        g_signal_emit(filter,
            gst_whisper_transcribe_signals[SIGNAL_MODEL_LOAD_FAILED],
            0, error ? (*error)->message : "Unknown error");

        return FALSE;
    }

    g_free(mgr->model_path);
    mgr->model_path = g_strdup(model_path);
    mgr->is_loaded = TRUE;

    g_mutex_unlock(&mgr->mutex);

    // Emit success signal
    emit_model_loaded_signal(filter, model_path);

    return TRUE;
}

void whisper_context_manager_unload(
    WhisperContextManager *mgr,
    GstWhisperTranscribe *filter
) {
    g_mutex_lock(&mgr->mutex);

    if (mgr->ctx) {
        whisper_free(mgr->ctx);
        mgr->ctx = NULL;
        mgr->is_loaded = FALSE;

        g_signal_emit(filter,
            gst_whisper_transcribe_signals[SIGNAL_MODEL_UNLOADED],
            0);

        GST_INFO_OBJECT(filter, "Model unloaded");
    }

    g_mutex_unlock(&mgr->mutex);
}
```

##### Transcription Signals

```c
static void emit_segment_transcribed_signal(
    GstWhisperTranscribe *filter,
    const gchar *text,
    gint64 start_time_ns,
    gint64 end_time_ns,
    gfloat confidence,
    const gchar *language,
    struct whisper_context *ctx,
    gint segment_idx
) {
    // Build structure with segment data
    GstStructure *segment_data = gst_structure_new("segment",
        "text", G_TYPE_STRING, text,
        "start-time", G_TYPE_INT64, start_time_ns,
        "end-time", G_TYPE_INT64, end_time_ns,
        "confidence", G_TYPE_DOUBLE, (gdouble)confidence,
        "language", G_TYPE_STRING, language,
        NULL);

    // Add token array
    GValue tokens_array = G_VALUE_INIT;
    g_value_init(&tokens_array, GST_TYPE_ARRAY);

    gint n_tokens = whisper_full_n_tokens(ctx, segment_idx);
    for (gint i = 0; i < n_tokens; i++) {
        struct whisper_token_data token_data =
            whisper_full_get_token_data(ctx, segment_idx, i);
        const gchar *token_text =
            whisper_full_get_token_text(ctx, segment_idx, i);

        GstStructure *token_struct = gst_structure_new("token",
            "text", G_TYPE_STRING, token_text,
            "probability", G_TYPE_DOUBLE, (gdouble)token_data.p,
            "start-time", G_TYPE_INT64, token_data.t0 * 10 * GST_MSECOND,
            "end-time", G_TYPE_INT64, token_data.t1 * 10 * GST_MSECOND,
            NULL);

        GValue token_value = G_VALUE_INIT;
        g_value_init(&token_value, GST_TYPE_STRUCTURE);
        gst_value_set_structure(&token_value, token_struct);
        gst_value_array_append_value(&tokens_array, &token_value);
        g_value_unset(&token_value);
        gst_structure_free(token_struct);
    }

    gst_structure_take_value(segment_data, "tokens", &tokens_array);

    // Emit signal
    g_signal_emit(filter,
        gst_whisper_transcribe_signals[SIGNAL_SEGMENT_TRANSCRIBED],
        0, segment_data);

    gst_structure_free(segment_data);
}

static void emit_transcription_lifecycle_signals(
    GstWhisperTranscribe *filter,
    gint64 start_timestamp_ns,
    gboolean is_starting
) {
    if (is_starting) {
        g_signal_emit(filter,
            gst_whisper_transcribe_signals[SIGNAL_TRANSCRIPTION_STARTED],
            0, start_timestamp_ns);
    } else {
        gint64 end_time = g_get_monotonic_time() * 1000;  // Convert to ns
        gint64 duration = end_time - start_timestamp_ns;

        g_signal_emit(filter,
            gst_whisper_transcribe_signals[SIGNAL_TRANSCRIPTION_COMPLETED],
            0, start_timestamp_ns, duration);
    }
}
```

##### Language Detection Signal

```c
static void emit_language_detected_signal(
    GstWhisperTranscribe *filter,
    struct whisper_context *ctx
) {
    // Auto-detect language
    gfloat *lang_probs = g_malloc0((whisper_lang_max_id() + 1) * sizeof(gfloat));

    whisper_lang_auto_detect(ctx, 0, filter->n_threads, lang_probs);

    // Find most probable language
    gint best_lang_id = 0;
    gfloat best_prob = 0.0f;

    for (gint i = 0; i <= whisper_lang_max_id(); i++) {
        if (lang_probs[i] > best_prob) {
            best_prob = lang_probs[i];
            best_lang_id = i;
        }
    }

    const gchar *detected_lang = whisper_lang_str(best_lang_id);

    g_signal_emit(filter,
        gst_whisper_transcribe_signals[SIGNAL_LANGUAGE_DETECTED],
        0, detected_lang, best_prob);

    GST_INFO_OBJECT(filter, "Language detected: %s (%.2f%%)",
        detected_lang, best_prob * 100.0f);

    g_free(lang_probs);
}
```

##### VAD and Buffer Signals

```c
static void emit_vad_signal(
    GstWhisperTranscribe *filter,
    gint64 timestamp_ns,
    gboolean is_speech
) {
    g_signal_emit(filter,
        gst_whisper_transcribe_signals[SIGNAL_VAD_SPEECH_DETECTED],
        0, timestamp_ns, is_speech);
}

static void emit_buffer_overflow_warning(
    GstWhisperTranscribe *filter,
    guint64 dropped_samples
) {
    g_signal_emit(filter,
        gst_whisper_transcribe_signals[SIGNAL_BUFFER_OVERFLOW],
        0, dropped_samples);

    GST_WARNING_OBJECT(filter,
        "Audio buffer overflow! Dropped %" G_GUINT64_FORMAT " samples. "
        "Consider increasing window-duration or reducing step-duration.",
        dropped_samples);
}
```

#### Signal Usage - Application Examples

##### Example 1: Monitoring Model Loading

```c
static void on_model_loaded(GstElement *element, const gchar *model_path, gpointer user_data) {
    g_print("✓ Model loaded successfully: %s\n", model_path);
}

static void on_model_load_failed(GstElement *element, const gchar *error_msg, gpointer user_data) {
    g_printerr("✗ Model loading failed: %s\n", error_msg);
    // Handle error - maybe try alternative model
}

static void on_model_info(GstElement *element, GstStructure *info, gpointer user_data) {
    const gchar *model_type = gst_structure_get_string(info, "model-type");
    gboolean is_multilingual = FALSE;
    gboolean gpu_enabled = FALSE;

    gst_structure_get_boolean(info, "is-multilingual", &is_multilingual);
    gst_structure_get_boolean(info, "gpu-enabled", &gpu_enabled);

    g_print("Model Info:\n");
    g_print("  Type: %s\n", model_type);
    g_print("  Multilingual: %s\n", is_multilingual ? "yes" : "no");
    g_print("  GPU Acceleration: %s\n", gpu_enabled ? "enabled" : "disabled");
}

// In application setup:
GstElement *whisper = gst_element_factory_make("whispertranscribe", "transcriber");
g_object_set(whisper, "model", "/path/to/model.bin", NULL);

g_signal_connect(whisper, "model-loaded", G_CALLBACK(on_model_loaded), NULL);
g_signal_connect(whisper, "model-load-failed", G_CALLBACK(on_model_load_failed), NULL);
g_signal_connect(whisper, "model-info", G_CALLBACK(on_model_info), NULL);
```

##### Example 2: Real-time Transcription Display

```c
static void on_segment_transcribed(
    GstElement *element,
    GstStructure *segment_data,
    gpointer user_data
) {
    const gchar *text = gst_structure_get_string(segment_data, "text");
    gint64 start_time = 0, end_time = 0;
    gdouble confidence = 0.0;
    const gchar *language = NULL;

    gst_structure_get_int64(segment_data, "start-time", &start_time);
    gst_structure_get_int64(segment_data, "end-time", &end_time);
    gst_structure_get_double(segment_data, "confidence", &confidence);
    language = gst_structure_get_string(segment_data, "language");

    g_print("[%02d:%02d.%03d -> %02d:%02d.%03d] [%s] (%.1f%%) %s\n",
        (gint)(start_time / GST_SECOND / 60),
        (gint)(start_time / GST_SECOND % 60),
        (gint)(start_time / GST_MSECOND % 1000),
        (gint)(end_time / GST_SECOND / 60),
        (gint)(end_time / GST_SECOND % 60),
        (gint)(end_time / GST_MSECOND % 1000),
        language,
        confidence * 100.0,
        text);

    // Access token details if needed
    const GValue *tokens_array = gst_structure_get_value(segment_data, "tokens");
    if (tokens_array && GST_VALUE_HOLDS_ARRAY(tokens_array)) {
        guint n_tokens = gst_value_array_get_size(tokens_array);
        for (guint i = 0; i < n_tokens; i++) {
            const GValue *token_val = gst_value_array_get_value(tokens_array, i);
            GstStructure *token = gst_value_get_structure(token_val);
            // Process individual tokens...
        }
    }
}

g_signal_connect(whisper, "segment-transcribed",
    G_CALLBACK(on_segment_transcribed), NULL);
```

##### Example 3: Language Detection

```c
static void on_language_detected(
    GstElement *element,
    const gchar *language,
    gfloat probability,
    gpointer user_data
) {
    g_print("Detected language: %s (confidence: %.1f%%)\n",
        language, probability * 100.0f);

    if (probability < 0.5) {
        g_print("Warning: Low confidence language detection\n");
    }

    // Optionally update UI or adjust parameters based on language
}

g_signal_connect(whisper, "language-detected",
    G_CALLBACK(on_language_detected), NULL);
```

##### Example 4: Performance Monitoring

```c
static void on_transcription_started(
    GstElement *element,
    gint64 timestamp,
    gpointer user_data
) {
    g_print("⏳ Transcription started at %" GST_TIME_FORMAT "\n",
        GST_TIME_ARGS(timestamp));
}

static void on_transcription_completed(
    GstElement *element,
    gint64 timestamp,
    gint64 duration,
    gpointer user_data
) {
    g_print("✓ Transcription completed in %" GST_TIME_FORMAT "\n",
        GST_TIME_ARGS(duration));

    // Calculate real-time factor
    gint64 audio_duration = 10 * GST_SECOND;  // Assuming 10s windows
    gdouble rt_factor = (gdouble)audio_duration / duration;
    g_print("  Real-time factor: %.2fx\n", rt_factor);
}

g_signal_connect(whisper, "transcription-started",
    G_CALLBACK(on_transcription_started), NULL);
g_signal_connect(whisper, "transcription-completed",
    G_CALLBACK(on_transcription_completed), NULL);
```

##### Example 5: VAD-based UI Updates

```c
static void on_vad_speech_detected(
    GstElement *element,
    gint64 timestamp,
    gboolean is_speech,
    gpointer user_data
) {
    if (is_speech) {
        g_print("🎤 Speech detected at %" GST_TIME_FORMAT "\n",
            GST_TIME_ARGS(timestamp));
        // Update UI: show recording indicator
    } else {
        g_print("🔇 Silence at %" GST_TIME_FORMAT "\n",
            GST_TIME_ARGS(timestamp));
        // Update UI: hide recording indicator
    }
}

g_signal_connect(whisper, "vad-speech-detected",
    G_CALLBACK(on_vad_speech_detected), NULL);
```

##### Example 6: Error Handling

```c
static void on_buffer_overflow(
    GstElement *element,
    guint64 dropped_samples,
    gpointer user_data
) {
    g_warning("⚠ Buffer overflow! Dropped %" G_GUINT64_FORMAT " samples (%.2f seconds)",
        dropped_samples,
        (gdouble)dropped_samples / WHISPER_SAMPLE_RATE);

    // Take action: pause pipeline, increase buffer size, etc.
    g_object_set(element, "window-duration", 15000, NULL);  // Increase to 15s
}

g_signal_connect(whisper, "buffer-overflow",
    G_CALLBACK(on_buffer_overflow), NULL);
```

##### Example 7: Complete Application Integration

```c
typedef struct {
    GMainLoop *loop;
    GstElement *pipeline;
    GstElement *whisper;
    gchar *output_file;
    FILE *transcript_fp;
} AppContext;

static void on_segment_transcribed_to_file(
    GstElement *element,
    GstStructure *segment_data,
    gpointer user_data
) {
    AppContext *app = (AppContext *)user_data;
    const gchar *text = gst_structure_get_string(segment_data, "text");
    gint64 start_time = 0;

    gst_structure_get_int64(segment_data, "start-time", &start_time);

    // Write to file in SRT format
    fprintf(app->transcript_fp, "[%" GST_TIME_FORMAT "] %s\n",
        GST_TIME_ARGS(start_time), text);
    fflush(app->transcript_fp);
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    AppContext app = {0};
    app.loop = g_main_loop_new(NULL, FALSE);
    app.transcript_fp = fopen("transcript.txt", "w");

    app.pipeline = gst_parse_launch(
        "pulsesrc ! audioconvert ! "
        "whispertranscribe name=whisper model=/path/to/model.bin ! "
        "fakesink",
        NULL);

    app.whisper = gst_bin_get_by_name(GST_BIN(app.pipeline), "whisper");

    // Connect all signals
    g_signal_connect(app.whisper, "segment-transcribed",
        G_CALLBACK(on_segment_transcribed_to_file), &app);
    g_signal_connect(app.whisper, "model-loaded",
        G_CALLBACK(on_model_loaded), &app);
    g_signal_connect(app.whisper, "language-detected",
        G_CALLBACK(on_language_detected), &app);
    g_signal_connect(app.whisper, "buffer-overflow",
        G_CALLBACK(on_buffer_overflow), &app);

    gst_element_set_state(app.pipeline, GST_STATE_PLAYING);
    g_main_loop_run(app.loop);

    // Cleanup
    gst_element_set_state(app.pipeline, GST_STATE_NULL);
    gst_object_unref(app.pipeline);
    fclose(app.transcript_fp);
    g_main_loop_unref(app.loop);

    return 0;
}
```

### Control Pad Commands

Control messages are JSON objects with a `command` field:

```json
{
  "command": "load_model",
  "model_path": "/path/to/model.bin",
  "use_gpu": true
}
```

Supported commands:
- `load_model`: Load a new model
- `unload_model`: Unload current model
- `set_language`: Change language
- `set_prompt`: Update context prompt
- `set_temperature`: Adjust temperature
- `set_sampling_strategy`: Change sampling strategy
- `reset_context`: Clear transcription context

---

## Sliding Window Mechanism

### Overview

The sliding window mechanism processes audio in overlapping segments to improve accuracy and provide continuous transcription similar to faster-whisper.

### Parameters

- **Window Duration**: Length of audio segment to process (default: 10 seconds)
- **Step Duration**: Advance amount between windows (default: 3 seconds)
- **Overlap Duration**: Overlap to maintain context (default: 0.2 seconds)

### Process Flow

```
Time:     0s    3s    6s    9s    12s   15s
         ┌──────────┐
Window 1 │          │
         └──────────┘
                ┌──────────┐
Window 2        │          │
                └──────────┘
                       ┌──────────┐
Window 3               │          │
                       └──────────┘
```

### Refinement Strategy

1. **Initial Pass**: Transcribe window with default parameters
2. **Refinement Passes**: Re-transcribe with temperature variations
3. **Probability Accumulation**: Track word probabilities across passes
4. **Final Selection**: Choose highest-probability words
5. **Context Propagation**: Use result as prompt for next window

### Benefits

- Improved accuracy through multiple passes
- Better handling of ambiguous audio
- Context awareness across boundaries
- Probability-based confidence scoring

---

## Testing Strategy

### Unit Tests

1. **Whisper Context Manager Tests**
   - Model loading/unloading
   - Thread safety
   - Error handling

2. **Audio Buffer Manager Tests**
   - Window extraction
   - Overlap handling
   - Buffer overflow conditions

3. **Format Conversion Tests**
   - S16LE to F32LE conversion
   - Stereo to mono conversion
   - Resampling accuracy

4. **Refinement Manager Tests**
   - Probability tracking
   - Candidate merging
   - Best selection logic

### Integration Tests

1. **Pipeline Tests**
   ```bash
   # Test with file source
   gst-launch-1.0 filesrc location=test.wav ! \
       wavparse ! \
       audioconvert ! \
       whispertranscribe model=/path/to/model.bin ! \
       fakesink dump=true

   # Test with live source
   gst-launch-1.0 pulsesrc ! \
       audioconvert ! \
       whispertranscribe model=/path/to/model.bin language=en ! \
       fakesink dump=true
   ```

2. **WebRTC Integration Test**
   - Test with webrtcbin source
   - Verify JSON output synchronization
   - Test latency requirements

3. **Control Pad Test**
   - Send control messages
   - Verify parameter updates
   - Test model hot-swapping

### Performance Tests

1. **Latency Measurement**
   - Audio input to JSON output delay
   - Target: < 500ms for real-time use

2. **Throughput Test**
   - Process hours of audio
   - Monitor memory usage
   - CPU/GPU utilization

3. **Accuracy Test**
   - Compare with reference transcriptions
   - Word Error Rate (WER) calculation
   - Confidence score correlation

---

## Performance Considerations

### Threading Strategy

- **Main Thread**: GStreamer pipeline processing
- **Worker Thread**: Whisper inference (CPU/GPU intensive)
- **Separation Benefits**:
  - Non-blocking audio ingestion
  - Smooth pipeline flow
  - Better CPU utilization

### Memory Management

- **Audio Buffer**: Circular buffer with max capacity
- **Model Memory**: Single context instance (reused)
- **Token Cache**: Limited prompt token history
- **JSON Buffers**: Small, short-lived allocations

### GPU Acceleration

- Enable with `use-gpu=true` property
- Requires whisper.cpp built with CUDA/Metal/OpenCL
- Significant speedup for inference (5-10x)
- Flash attention for additional performance

### Latency Optimization

1. **Reduce Window Size**: Smaller windows = faster results
2. **Limit Refinement Passes**: Trade accuracy for speed
3. **Adjust Thread Count**: Match CPU core count
4. **Enable VAD**: Skip silent segments

---

## Future Enhancements

### Phase 2 Features

1. **Diarization Integration**
   - Speaker segmentation metadata
   - Synchronized with timing output
   - Speaker ID in JSON output

2. **Multi-Model Support**
   - Load multiple models simultaneously
   - Language-specific routing
   - Quality/speed variants

3. **Advanced Prompting**
   - Dynamic prompt generation
   - Domain-specific vocabularies
   - Custom token biasing

4. **Quality Metrics**
   - Real-time WER estimation
   - Confidence-based filtering
   - Uncertainty quantification

### Phase 3 Features

1. **Streaming Optimizations**
   - Zero-copy audio paths
   - SIMD optimizations
   - Custom memory allocators

2. **Enhanced Control**
   - REST API control interface
   - WebSocket control channel
   - Dynamic pipeline reconfiguration

3. **Output Formats**
   - SRT/VTT subtitle generation
   - Custom JSON schemas
   - Protobuf output option

4. **Cloud Integration**
   - Remote model loading
   - Distributed inference
   - Model cache management

---

## Build System Integration

### Meson Build Configuration

```meson
# gstreamer/meson.build

whisper_dep = dependency('whisper',
    required: true,
    fallback: ['whisper', 'whisper_dep'])

gstreamer_deps = [
    dependency('gstreamer-1.0'),
    dependency('gstreamer-base-1.0'),
    dependency('gstreamer-audio-1.0'),
    dependency('json-glib-1.0'),
    whisper_dep,
]

whisper_plugin_sources = [
    'src/gstwhisperplugin.c',
    'src/gstwhispertranscribe.c',
    'src/whispercontextmanager.c',
    'src/audiobuffermanager.c',
    'src/utils.c',
]

whisper_plugin = library('gstwhisper',
    whisper_plugin_sources,
    dependencies: gstreamer_deps,
    install: true,
    install_dir: plugins_install_dir,
)
```

---

## Example Usage

### Basic Transcription

```bash
gst-launch-1.0 \
    filesrc location=audio.wav ! \
    wavparse ! \
    audioconvert ! \
    whispertranscribe \
        model=/models/ggml-base.en.bin \
        language=en \
        n-threads=4 ! \
    fakesink dump=true
```

### WebRTC Transcription

```bash
gst-launch-1.0 \
    webrtcbin name=webrtc ! \
    audioconvert ! \
    whispertranscribe \
        model=/models/ggml-small.bin \
        language=auto \
        use-gpu=true \
        window-duration=5000 \
        step-duration=2000 ! \
    appsink name=transcription
```

### With Control Pad

```c
// Send control message to change language
GstPad *control_pad = gst_element_get_request_pad(whisper_elem, "control");

const gchar *control_json = "{\"command\":\"set_language\",\"language\":\"fr\"}";
GstBuffer *control_buf = gst_buffer_new_allocate(NULL, strlen(control_json), NULL);
gst_buffer_fill(control_buf, 0, control_json, strlen(control_json));

gst_pad_push(control_pad, control_buf);
```

---

## Timeline Summary

| Phase | Duration | Deliverables |
|-------|----------|-------------|
| 1 | Week 1 | Basic element structure, properties, pads |
| 2 | Week 1-2 | Whisper.cpp integration, model loading |
| 3 | Week 2 | Audio buffering, format conversion |
| 4 | Week 2-3 | Sliding window, multi-pass refinement |
| 5 | Week 3 | Worker thread, async processing |
| 6 | Week 3-4 | Transform implementation, audio flow |
| 7 | Week 4 | Control pad, dynamic configuration |
| 8 | Week 4 | JSON output generation |
| 9 | Week 5 | State management, lifecycle, testing |

**Total Estimated Time**: 5-6 weeks for initial implementation

---

## Dependencies

### Required

- GStreamer >= 1.20
- GStreamer Base Plugins
- GStreamer Audio Library
- json-glib >= 1.6
- whisper.cpp (latest)

### Optional

- CUDA Toolkit (for GPU support)
- Metal (for macOS GPU support)
- OpenCL (for cross-platform GPU support)

### Build Tools

- Meson >= 0.59
- Ninja
- pkg-config
- C compiler (gcc/clang)

---

## References

1. [GStreamer Plugin Writer's Guide](https://gstreamer.freedesktop.org/documentation/plugin-development/)
2. [GStreamer Audio Filter Documentation](https://gstreamer.freedesktop.org/documentation/audio/gstaudiofilter.html)
3. [whisper.cpp API Documentation](https://github.com/ggerganov/whisper.cpp)
4. [faster-whisper](https://github.com/guillaumekln/faster-whisper) - Reference for sliding window approach
5. [GStreamer WebRTC Implementation](https://gstreamer.freedesktop.org/documentation/webrtc/index.html)

---

*This implementation plan provides a comprehensive roadmap for developing a production-quality GStreamer filter for whisper.cpp with real-time transcription capabilities.*
