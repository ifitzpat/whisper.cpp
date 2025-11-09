/* Whisper Context Manager
 * Copyright (C) 2024 whisper.cpp contributors
 *
 * Manages whisper.cpp model loading and inference
 */

#ifndef __WHISPER_CONTEXT_MANAGER_H__
#define __WHISPER_CONTEXT_MANAGER_H__

#include <glib.h>
#include <whisper.h>

G_BEGIN_DECLS

typedef struct _WhisperContextManager WhisperContextManager;

/**
 * WhisperContextManager:
 *
 * Manages the whisper.cpp context and provides thread-safe
 * access to model loading and transcription operations.
 */
struct _WhisperContextManager {
  struct whisper_context *ctx;
  gchar *model_path;
  gboolean is_loaded;
  GMutex mutex;
};

/**
 * whisper_context_manager_new:
 *
 * Creates a new WhisperContextManager instance.
 *
 * Returns: A new #WhisperContextManager. Free with whisper_context_manager_free().
 */
WhisperContextManager * whisper_context_manager_new (void);

/**
 * whisper_context_manager_load_model:
 * @mgr: A #WhisperContextManager
 * @model_path: Path to the whisper model file
 * @use_gpu: Whether to enable GPU acceleration
 * @error: (out) (nullable): Return location for error
 *
 * Loads a whisper model from the specified path.
 * If a model is already loaded, it will be unloaded first.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean whisper_context_manager_load_model (
    WhisperContextManager *mgr,
    const gchar *model_path,
    gboolean use_gpu,
    GError **error);

/**
 * whisper_context_manager_unload_model:
 * @mgr: A #WhisperContextManager
 *
 * Unloads the currently loaded model, if any.
 */
void whisper_context_manager_unload_model (WhisperContextManager *mgr);

/**
 * whisper_context_manager_is_loaded:
 * @mgr: A #WhisperContextManager
 *
 * Checks if a model is currently loaded.
 *
 * Returns: %TRUE if a model is loaded, %FALSE otherwise
 */
gboolean whisper_context_manager_is_loaded (WhisperContextManager *mgr);

/**
 * whisper_context_manager_get_context:
 * @mgr: A #WhisperContextManager
 *
 * Gets the underlying whisper_context pointer.
 * This should only be used while holding the mutex.
 *
 * Returns: (transfer none) (nullable): The whisper_context, or NULL if not loaded
 */
struct whisper_context * whisper_context_manager_get_context (WhisperContextManager *mgr);

/**
 * whisper_context_manager_transcribe:
 * @mgr: A #WhisperContextManager
 * @audio_data: Float array of audio samples
 * @n_samples: Number of samples in audio_data
 * @params: Whisper full parameters
 * @error: (out) (nullable): Return location for error
 *
 * Transcribes audio data using the loaded model.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean whisper_context_manager_transcribe (
    WhisperContextManager *mgr,
    const gfloat *audio_data,
    gsize n_samples,
    struct whisper_full_params *params,
    GError **error);

/**
 * whisper_context_manager_free:
 * @mgr: A #WhisperContextManager
 *
 * Frees a WhisperContextManager and all associated resources.
 */
void whisper_context_manager_free (WhisperContextManager *mgr);

G_END_DECLS

#endif /* __WHISPER_CONTEXT_MANAGER_H__ */
