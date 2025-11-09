/* GStreamer Whisper Plugin
 * Copyright (C) 2024 whisper.cpp contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the MIT License
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstwhispertranscribe.h"

/* Plugin initialization function
 * This function is called by GStreamer to register all elements
 * provided by this plugin
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  /* Register the whispertranscribe element */
  if (!gst_element_register (plugin, "whispertranscribe", GST_RANK_NONE,
          GST_TYPE_WHISPER_TRANSCRIBE)) {
    return FALSE;
  }

  return TRUE;
}

/* Plugin definition macro
 * This creates the entry point that GStreamer uses to load the plugin
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    whisper,
    "Whisper.cpp speech transcription plugin",
    plugin_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
