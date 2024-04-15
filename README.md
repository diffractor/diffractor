# Diffractor
[![CI](https://github.com/diffractor/diffractor/actions/workflows/msbuild.yml/badge.svg)](https://github.com/diffractor/diffractor/actions/workflows/msbuild.yml)

The fastest photo and video organizer for Windows. It indexes your files to allow duplicate detection and fast search.

## Building
1. Reclusively clone the code using git. There are submodules for [ffmpeg](https://github.com/diffractor/FFmpeg) and the [xmp-sdk](https://github.com/diffractor/XMP-Toolkit-SDK). 
2. Open the solution file (df.sln) in Visual Studio.

## Contributions
Contributions welcome. Discussions about changes normally happen in [issues](https://github.com/diffractor/diffractor/issues).

### Translations
- Diffractor uses PO file translations that can be edited by [poedit](https://poedit.net/).
- Translations can be found under [exe/languages](https://github.com/diffractor/diffractor/tree/master/exe/languages) and updated via a pull request.
- When adding a new language, it is recommended to use the German file as a starting point.
- You can test a new language file by copying it into Diffractors languages folder. Diffractor should automatically detect the new or updated po file when it starts. You can specify a language under Diffractors Languages menu. The language folder would typically be: C:\Users\USER-NAME\AppData\Local\Diffractor\languages

### Running tests
Diffractor has a built-in test runner. If the application is started from Visual Studio there should be an extra toolbar button (top left) with a checkmark on it. Click this button for the test run menu. When running tests, you can use the escape key to return to the normal application mode.

## Dependancies (3rd party libraries)
 
- [bzip2](https://sourceware.org/bzip2/) 1.0.8
- [dng-sdk](https://helpx.adobe.com/camera-raw/digital-negative.html) 1.6
- [expat](https://libexpat.github.io/) 2.4.9 
- [ffmpeg](https://ffmpeg.org/) 5.0.1
- [hunspell](https://github.com/hunspell/hunspell) 1.7.1
- [libde265](https://github.com/strukturag/libde265) 1.0.8
- [libebml](https://github.com/Matroska-Org/libebml) 1.4.3
- [libexif](https://github.com/libexif/libexif) 0.6.24
- [libheif](https://github.com/strukturag/libheif) 1.13.0
- [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) 2.1.4
- [liblzma](https://tukaani.org/xz/) 5.2.7
- [libmatroska](https://github.com/Matroska-Org/libmatroska) 1.7.0
- [libopenmpt](https://lib.openmpt.org) 0.6.6
- [LibPng](http://www.libpng.org/pub/png/libpng.html) 1.6.38
- [LibRaw](https://www.libraw.org) 0.21.2
- [libwebp](https://github.com/webmproject/libwebp) 1.2.4
- [minizip-ng](https://github.com/zlib-ng/minizip-ng) 3.0.6
- [parallel-hashmap](https://github.com/greg7mdp/parallel-hashmap) 1.3.8
- [sqlite](https://www.sqlite.org/index.html) 3.45.2
- [utf-cpp](https://github.com/nemtrif/utfcpp) 3.2.1
- [zlib-ng](https://github.com/zlib-ng/zlib-ng) 2.0.6
