# Diffractor
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
 
- [ffmpeg](https://ffmpeg.org/) 5.0.1
- [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) 2.1.3
- [sqlite](https://www.sqlite.org/index.html) 3.38.5 
- [libwebp](https://github.com/webmproject/libwebp) 1.2.2
- [hunspell](https://github.com/hunspell/hunspell) 1.7.0
- [LibRaw](https://www.libraw.org) 0.20
