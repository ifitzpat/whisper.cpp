/* Audio Buffer Manager
 * Copyright (C) 2024 whisper.cpp contributors
 *
 * Manages audio buffering and sliding window mechanism
 *
 * Phase 3 - Audio Buffer Management
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "audiobuffermanager.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (audio_buffer_manager_debug);
#define GST_CAT_DEFAULT audio_buffer_manager_debug

/* Helper function to free an AudioChunk */
static void
audio_chunk_free (AudioChunk *chunk)
{
  if (chunk) {
    g_free (chunk->data);
    g_free (chunk);
  }
}

/**
 * audio_buffer_manager_new:
 *
 * Creates a new AudioBufferManager instance.
 */
AudioBufferManager *
audio_buffer_manager_new (gint window_duration_ms,
    gint step_duration_ms,
    gint overlap_duration_ms,
    gint sample_rate)
{
  AudioBufferManager *mgr = g_new0 (AudioBufferManager, 1);

  g_mutex_init (&mgr->mutex);
  mgr->audio_chunks = g_queue_new ();
  mgr->sample_rate = sample_rate;

  /* Calculate buffer sizes in samples */
  mgr->window_capacity = (window_duration_ms * sample_rate) / 1000;
  mgr->overlap_size = (overlap_duration_ms * sample_rate) / 1000;

  /* Allocate window buffer */
  mgr->window_buffer = g_malloc0 (mgr->window_capacity * sizeof (gfloat));
  mgr->window_size = 0;

  GST_DEBUG_CATEGORY_INIT (audio_buffer_manager_debug, "audiobufmgr",
      0, "Audio Buffer Manager");
  GST_DEBUG ("Created AudioBufferManager: window=%d samples, overlap=%d samples, rate=%d Hz",
      (gint)mgr->window_capacity, (gint)mgr->overlap_size, sample_rate);

  return mgr;
}

/**
 * audio_buffer_manager_push:
 *
 * Pushes audio data into the buffer queue.
 */
void
audio_buffer_manager_push (AudioBufferManager *mgr,
    const gfloat *data,
    gsize size,
    gint64 pts)
{
  AudioChunk *chunk;

  g_return_if_fail (mgr != NULL);
  g_return_if_fail (data != NULL);

  g_mutex_lock (&mgr->mutex);

  chunk = g_new (AudioChunk, 1);
  chunk->data = g_memdup2 (data, size * sizeof (gfloat));
  chunk->size = size;
  chunk->pts = pts;

  g_queue_push_tail (mgr->audio_chunks, chunk);

  GST_LOG ("Pushed %zu samples, PTS=%" GST_TIME_FORMAT ", queue length=%d",
      size, GST_TIME_ARGS (pts), g_queue_get_length (mgr->audio_chunks));

  g_mutex_unlock (&mgr->mutex);
}

/**
 * audio_buffer_manager_get_window:
 *
 * Extracts a window of audio data if enough samples are available.
 */
gboolean
audio_buffer_manager_get_window (AudioBufferManager *mgr,
    gfloat **out_data,
    gsize *out_size,
    gint64 *out_pts)
{
  gsize total_available = 0;
  GList *iter;
  gint64 first_pts = GST_CLOCK_TIME_NONE;

  g_return_val_if_fail (mgr != NULL, FALSE);
  g_return_val_if_fail (out_data != NULL, FALSE);
  g_return_val_if_fail (out_size != NULL, FALSE);
  g_return_val_if_fail (out_pts != NULL, FALSE);

  g_mutex_lock (&mgr->mutex);

  /* Calculate total available samples */
  for (iter = mgr->audio_chunks->head; iter; iter = iter->next) {
    AudioChunk *chunk = iter->data;
    total_available += chunk->size;
  }

  GST_LOG ("Window check: available=%zu, needed=%zu",
      total_available, mgr->window_capacity);

  if (total_available < mgr->window_capacity) {
    g_mutex_unlock (&mgr->mutex);
    return FALSE;  /* Not enough data yet */
  }

  /* Fill window buffer */
  mgr->window_size = 0;

  while (mgr->window_size < mgr->window_capacity && !g_queue_is_empty (mgr->audio_chunks)) {
    AudioChunk *chunk = g_queue_peek_head (mgr->audio_chunks);

    if (first_pts == GST_CLOCK_TIME_NONE) {
      first_pts = chunk->pts;
    }

    gsize to_copy = MIN (chunk->size, mgr->window_capacity - mgr->window_size);
    memcpy (mgr->window_buffer + mgr->window_size,
            chunk->data,
            to_copy * sizeof (gfloat));
    mgr->window_size += to_copy;

    if (to_copy < chunk->size) {
      /* Partial consumption, adjust chunk */
      memmove (chunk->data, chunk->data + to_copy,
               (chunk->size - to_copy) * sizeof (gfloat));
      chunk->size -= to_copy;
      /* Update PTS to reflect consumed samples */
      chunk->pts += (to_copy * GST_SECOND) / mgr->sample_rate;
      break;
    } else {
      /* Full consumption, remove chunk */
      AudioChunk *consumed = g_queue_pop_head (mgr->audio_chunks);
      audio_chunk_free (consumed);
    }
  }

  *out_data = mgr->window_buffer;
  *out_size = mgr->window_size;
  *out_pts = first_pts;

  GST_DEBUG ("Extracted window: %zu samples, PTS=%" GST_TIME_FORMAT,
      mgr->window_size, GST_TIME_ARGS (first_pts));

  g_mutex_unlock (&mgr->mutex);
  return TRUE;
}

/**
 * audio_buffer_manager_clear:
 *
 * Clears all buffered audio data.
 */
void
audio_buffer_manager_clear (AudioBufferManager *mgr)
{
  g_return_if_fail (mgr != NULL);

  g_mutex_lock (&mgr->mutex);

  GST_DEBUG ("Clearing audio buffer (%d chunks)",
      g_queue_get_length (mgr->audio_chunks));

  g_queue_free_full (mgr->audio_chunks, (GDestroyNotify) audio_chunk_free);
  mgr->audio_chunks = g_queue_new ();
  mgr->window_size = 0;

  g_mutex_unlock (&mgr->mutex);
}

/**
 * audio_buffer_manager_get_buffered_duration:
 *
 * Gets the duration of buffered audio in nanoseconds.
 */
gint64
audio_buffer_manager_get_buffered_duration (AudioBufferManager *mgr)
{
  gsize total_samples = 0;
  GList *iter;
  gint64 duration;

  g_return_val_if_fail (mgr != NULL, 0);

  g_mutex_lock (&mgr->mutex);

  for (iter = mgr->audio_chunks->head; iter; iter = iter->next) {
    AudioChunk *chunk = iter->data;
    total_samples += chunk->size;
  }

  duration = (total_samples * GST_SECOND) / mgr->sample_rate;

  g_mutex_unlock (&mgr->mutex);

  return duration;
}

/**
 * audio_buffer_manager_free:
 *
 * Frees an AudioBufferManager and all associated resources.
 */
void
audio_buffer_manager_free (AudioBufferManager *mgr)
{
  if (!mgr)
    return;

  GST_DEBUG ("Freeing AudioBufferManager");

  g_queue_free_full (mgr->audio_chunks, (GDestroyNotify) audio_chunk_free);
  g_free (mgr->window_buffer);
  g_mutex_clear (&mgr->mutex);
  g_free (mgr);
}
