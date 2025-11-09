project('gst-whisper', 'c',
  version : '1.0.0',
  meson_version : '>= 0.59',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cc = meson.get_compiler('c')

gst_version = meson.project_version()

api_version = '1.0'

gst_req = '>= 1.20.0'

# GStreamer dependencies
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  required : true,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
  required : true)
json_glib_dep = dependency('json-glib-1.0', version : '>= 1.6',
  required : true)

# whisper.cpp dependency
whisper_inc = include_directories('..')
whisper_lib = cc.find_library('whisper',
  dirs : [join_paths(meson.source_root(), 'build', 'src')],
  required : false)

if not whisper_lib.found()
  message('whisper library not found, will need to build whisper.cpp first')
  message('Run: mkdir -p build && cd build && cmake .. && make')
  whisper_lib = declare_dependency()
endif

plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-whisper')
cdata.set_quoted('GST_LICENSE', 'MIT')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer whisper.cpp Plugin')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/ggerganov/whisper.cpp')
configure_file(output : 'config.h', configuration : cdata)

# Plugin source files
plugin_sources = [
  'src/gstwhispertranscribe.c',
  'src/gstwhisperplugin.c',
  'src/whispercontextmanager.c',
  'src/audiobuffermanager.c',
  'src/utils.c',
]

gstwhisper = library('gstwhisper',
  plugin_sources,
  c_args: plugin_c_args,
  include_directories : [include_directories('.'), whisper_inc, include_directories('../include')],
  dependencies : [gst_dep, gstbase_dep, gstaudio_dep, json_glib_dep, whisper_lib],
  install : true,
  install_dir : plugins_install_dir,
)

# Tests
if get_option('tests').enabled()
  subdir('tests')
endif
