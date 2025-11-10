(use-modules (guix packages)
             (guix gexp)
             (guix git-download)
             (guix build-system cmake)
             (guix build-system meson)
             ((guix licenses) #:prefix license:)
             (gnu packages)
             (gnu packages audio)
             (gnu packages gstreamer)
             (gnu packages machine-learning)
             (gnu packages pkg-config)
             (gnu packages cmake)
             (gnu packages gcc)
             (gnu packages glib)
             (gnu packages gnome))

;; Define whisper-cpp package since it might not be in Guix yet
(define whisper-cpp
  (package
    (name "whisper-cpp")
    (version "1.8.2")
    (source (local-file ".." "whisper-cpp-source"
                        #:recursive? #t))
    (build-system cmake-build-system)
    (arguments
     (list
      #:configure-flags
      #~(list "-DCMAKE_BUILD_TYPE=Release"
              "-DWHISPER_BUILD_TESTS=OFF"
              "-DWHISPER_BUILD_EXAMPLES=OFF"
              "-DBUILD_SHARED_LIBS=ON")
      #:tests? #f))
    (native-inputs
     (list pkg-config))
    (synopsis "Port of OpenAI's Whisper model in C/C++")
    (description
     "whisper.cpp is a high-performance inference of OpenAI's Whisper
automatic speech recognition (ASR) model.")
    (home-page "https://github.com/ggerganov/whisper.cpp")
    (license license:expat)))

;; Main GStreamer plugin package
(define-public gst-whisper
  (package
    (name "gst-whisper")
    (version "1.0.0")
    (source (local-file "." "gst-whisper-source"
                        #:recursive? #t))
    (build-system meson-build-system)
    (arguments
     (list
      #:configure-flags
      #~(list "-Dtests=enabled")
      #:tests? #f))
    (native-inputs
     (list pkg-config
           cmake
           whisper-cpp))
    (inputs
     (list gstreamer
           gst-plugins-base
           json-glib))
    (synopsis "GStreamer plugin for whisper.cpp speech recognition")
    (description
     "This package provides a GStreamer filter element that integrates
OpenAI's Whisper speech recognition model via whisper.cpp.  It supports
real-time audio transcription with sliding window processing and outputs
timestamped JSON transcripts.")
    (home-page "https://github.com/ggerganov/whisper.cpp")
    (license license:expat)))

gst-whisper
