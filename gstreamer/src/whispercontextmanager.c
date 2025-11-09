/* Whisper Context Manager
 * Copyright (C) 2024 whisper.cpp contributors
 *
 * Manages whisper.cpp model loading and inference
 *
 * Phase 2 - Whisper.cpp Integration
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "whispercontextmanager.h"
#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (whisper_context_manager_debug);
#define GST_CAT_DEFAULT whisper_context_manager_debug

/**
 * whisper_context_manager_new:
 *
 * Creates a new WhisperContextManager instance.
 */
WhisperContextManager *
whisper_context_manager_new (void)
{
  WhisperContextManager *mgr = g_new0 (WhisperContextManager, 1);
  g_mutex_init (&mgr->mutex);
  mgr->ctx = NULL;
  mgr->model_path = NULL;
  mgr->is_loaded = FALSE;

  GST_DEBUG_CATEGORY_INIT (whisper_context_manager_debug, "whisperctxmgr",
      0, "Whisper Context Manager");
  GST_DEBUG ("Created new WhisperContextManager");

  return mgr;
}

/**
 * whisper_context_manager_load_model:
 *
 * Loads a whisper model from the specified path.
 */
gboolean
whisper_context_manager_load_model (WhisperContextManager *mgr,
    const gchar *model_path,
    gboolean use_gpu,
    GError **error)
{
  g_return_val_if_fail (mgr != NULL, FALSE);
  g_return_val_if_fail (model_path != NULL, FALSE);

  g_mutex_lock (&mgr->mutex);

  GST_DEBUG ("Loading whisper model from: %s (GPU: %s)",
      model_path, use_gpu ? "enabled" : "disabled");

  // Unload existing model if loaded
  if (mgr->ctx) {
    GST_DEBUG ("Unloading existing model");
    whisper_free (mgr->ctx);
    mgr->ctx = NULL;
    mgr->is_loaded = FALSE;
  }

  // Initialize context parameters
  struct whisper_context_params cparams = whisper_context_default_params ();
  cparams.use_gpu = use_gpu;
  cparams.flash_attn = use_gpu;  // Enable flash attention with GPU

  // Load model
  mgr->ctx = whisper_init_from_file_with_params (model_path, cparams);

  if (!mgr->ctx) {
    g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
        "Failed to load whisper model from: %s", model_path);
    GST_ERROR ("Failed to load whisper model from: %s", model_path);
    g_mutex_unlock (&mgr->mutex);
    return FALSE;
  }

  // Update state
  g_free (mgr->model_path);
  mgr->model_path = g_strdup (model_path);
  mgr->is_loaded = TRUE;

  GST_INFO ("Successfully loaded whisper model: %s", model_path);

  g_mutex_unlock (&mgr->mutex);
  return TRUE;
}

/**
 * whisper_context_manager_unload_model:
 *
 * Unloads the currently loaded model.
 */
void
whisper_context_manager_unload_model (WhisperContextManager *mgr)
{
  g_return_if_fail (mgr != NULL);

  g_mutex_lock (&mgr->mutex);

  if (mgr->ctx) {
    GST_DEBUG ("Unloading whisper model");
    whisper_free (mgr->ctx);
    mgr->ctx = NULL;
  }

  g_free (mgr->model_path);
  mgr->model_path = NULL;
  mgr->is_loaded = FALSE;

  g_mutex_unlock (&mgr->mutex);
}

/**
 * whisper_context_manager_is_loaded:
 *
 * Checks if a model is currently loaded.
 */
gboolean
whisper_context_manager_is_loaded (WhisperContextManager *mgr)
{
  gboolean loaded;

  g_return_val_if_fail (mgr != NULL, FALSE);

  g_mutex_lock (&mgr->mutex);
  loaded = mgr->is_loaded;
  g_mutex_unlock (&mgr->mutex);

  return loaded;
}

/**
 * whisper_context_manager_get_context:
 *
 * Gets the underlying whisper_context pointer.
 * WARNING: Caller must ensure proper locking!
 */
struct whisper_context *
whisper_context_manager_get_context (WhisperContextManager *mgr)
{
  g_return_val_if_fail (mgr != NULL, NULL);
  return mgr->ctx;
}

/**
 * whisper_context_manager_transcribe:
 *
 * Transcribes audio data using the loaded model.
 */
gboolean
whisper_context_manager_transcribe (WhisperContextManager *mgr,
    const gfloat *audio_data,
    gsize n_samples,
    struct whisper_full_params *params,
    GError **error)
{
  int ret;

  g_return_val_if_fail (mgr != NULL, FALSE);
  g_return_val_if_fail (audio_data != NULL, FALSE);
  g_return_val_if_fail (params != NULL, FALSE);

  g_mutex_lock (&mgr->mutex);

  if (!mgr->is_loaded) {
    g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
        "Model not loaded");
    GST_ERROR ("Attempted transcription without loaded model");
    g_mutex_unlock (&mgr->mutex);
    return FALSE;
  }

  GST_DEBUG ("Transcribing %zu samples", n_samples);

  ret = whisper_full (mgr->ctx, *params, audio_data, n_samples);

  g_mutex_unlock (&mgr->mutex);

  if (ret != 0) {
    g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
        "Transcription failed with code: %d", ret);
    GST_ERROR ("Transcription failed with code: %d", ret);
    return FALSE;
  }

  GST_DEBUG ("Transcription completed successfully");
  return TRUE;
}

/**
 * whisper_context_manager_free:
 *
 * Frees a WhisperContextManager and all associated resources.
 */
void
whisper_context_manager_free (WhisperContextManager *mgr)
{
  if (!mgr)
    return;

  GST_DEBUG ("Freeing WhisperContextManager");

  if (mgr->ctx) {
    whisper_free (mgr->ctx);
  }

  g_free (mgr->model_path);
  g_mutex_clear (&mgr->mutex);
  g_free (mgr);
}
