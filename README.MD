# SDVPCM

## What is this?

**SDVPCM** (or *Standard Definition Video to Pulse Code Modulation*) is
GUI cross-platform software designed for decoding PCM digital audio data
from SD video sources.
It takes video file containing capture of some PCM format recording
and produces WAV file with decoded audio, performing error correction
and dropout masking set by user.

## History

At the dawn of digital audio the only viable option to store relatively
high bandwidth digital signal was to record it to the video tape.
From 1970s various systems were developed that accompanied some kind of
video tape recorder with additional [PCM audio processor](https://pcm.vcd.digital/wp-content/uploads/2021/11/Tadashi_Kojima_-_PCM_Signal_Processor_US4433416.pdf) to achieve
capabilities of digital audio recording and playback.

*Notable ones:*
- **Denon DN-023R** (1972)
- **Sony PCM-1** (1977)
- **Sony PCM-1600** (1978), **PCM-1610** (1980), **PCM-1630** (1982)
- **EIAJ STC-007** (1980), **Sony PCM-F1** (1981)
- **[Technics SV-P100](https://www.youtube.com/watch?v=cFvb8HhlQ38)** (1981)

*Denon DN-023R* was a professional 8-channel digital audio recorder.
*Sony PCM-1* was the first consumer-oriented 13-bit PCM audio processor.
*Sony PCM-16x0* series were professional 16-bit PCM audio processors,
that were used for mastering Audio CDs.
*EIAJ STC-007* was widely supported standard of consumer PCM audio processors
with 14-bit audio, *Sony PCM-F1* was its 16-bit extension.
*Technics SV-P100* was a combined VHS deck with STC-007 digital audio processing
and some additional mark and search features put into one device.

The idea was to encode digital audio bitstream as some sort of video patterns
that could be recorded as SD video and than played back and decoded.
Digital nature of the signal allowed addition of error detection and
error correction codes into the bitstream and interleaving data
to avoid signal loss in case of problems with reading signal from the tape.

## Supported PCM formats

- **Sony PCM-1** (13 bits)
- **Sony PCM-16x0** (16 bits) (both SI and EI formats)
- **[EIAJ STC-007](https://pcm.vcd.digital/wp-content/uploads/2021/11/Consumer_Use_PCM_Encoder-Decoder_EIAJ_STC-007.pdf)** (14 bits) and **Sony PCM-F1** (16 bits)
- **[M2](https://pcm.vcd.digital/m2/)** (16 bits)

## Features

- Linear decoding of SD video into digital audio skipping analog domain.
- Final digital audio can be stored into WAV file.
- Live playback is possible but it's only for evaluation purposes and it's
  not the primary use case of the software.
- All error detection and error correction features of each format
  are supported and sometimes more than that.
- Data decoding from video files with lossy digital compression.
- Data decoding from heavily degraded and damaged sources.
- Possibility of full audio data recovery even if not all video lines
  are captured in the file (most PCM formats use inactive region of the video).
- Video resolution and aspect ratio do not matter as long as video
  is not vertically resized and data bits are not blended with each other
  (decoder can't help with dropped or duplicated frames in your capture though).
- Selectable mode of binarization to balance speed and data recovery efforts.
- Many user-controllable parameters (including PCM format,
  source video field order, active error correction stages,
  dropout masking method and more)
  but most of those can be left in "auto" thanks to auto-detection algorithms.
- Statistics display for decoding process.
- Visualizations for source, binarized data and data blocks
  with error correction indicators.
- Multithreaded data processing.
- Interface translation support (language is selected by system locale).
- [Qt 5](https://www.qt.io/) and [FFMPEG libraries](https://ffmpeg.org/)
  are used to achieve cross-platform support.

## More nerdy features

- **Automatic Gain Control (AGC)**: levels of "0" and "1" bits
  as well as reference (threshold) level are automatically detected
  on per-line basis using statistics on brightness spread information.
- Reference level can be calculated fast or brute-force sweeped through
  all available levels according to preset binarization mode.
- **Time-Based Correction (TBC)**: automatic horizontal data coordinates detection
  that can be performed on per-line basis or averaged by the frame
  according to preset binarization mode and PCM format.
- **Bit Picker**: automatic detection and brute-force picking bits
  that were cut from the edges of the source video
  due to incorrectly setup video capture (for PCM-1 and PCM-16x0).
- Automatic reference level hysteresis is used to extract more data from noisy
  and/or low-bandwidth sources, enabling adaptive reference level
  depending on the state of the previous bits (maximum hysteresis depth
  depends on preset binarization mode).
- **Pixel-Shifter (Micro TBC)**: another mechanism that allows more accurate
  data extraction from noisy and damaged sources, performing
  multiple shifts of each data bit coordinates until valid data is read.
- Extensive statistics collection and analysis on each stage to detect and
  filter out false-positive CRCs and CRC collisions
  to prevent erroneous data propagating through.
- Big list of fine settings, including setting limits for "0" and "1"
  brightnesses, minimum contrast, limits for reference level,
  maximum allowed picked bits, margins for data coordinates search,
  manual data coordinates, capabilities to decode data from different
  color channels (R/G/B/Y) or to ignore CRC errors.
- Sophisticated algorithms for filtering noise and empty lines from the video
  and automatically detecting vertical data coordinates for each PCM format.
- Automatic detection of field order, video standard, data resolution
  for STC-007/PCM-F1 formats.
- Selectable dropout masking method (skip, hold level, linear interpolation).

## Usage

- Launch application
- Click {*Open a video...*} and select a video file with PCM data in it
- Set {*PCM format*} to desired format
- Tick {*Save to a *.WAV file*} and/or {*Live playback*}
- Click {*START/STOP source video*}

After all frames are decoded, process will stop and {*START/STOP*} button will reset.
Decoded WAV file will be placed in the same path as the source video file
and will have the same name with decoder version added to it.
If WAV file is already present it will be overwritten!

User can stop decoding at any moment by pressing {*START/STOP*} button.
User also can pause decoding with {*Pause*} button or
by ticking {*Stepped playback*} option while in decoding.
User can change decoder settings before or while decoding
but best practice is to set everything before starting decoding.

When in doubt, reset decoder settings to defaults by selecting
{*Reset decoder settings*} in the {*Settings*} menu.

## Demo

A little bit outdated video:

[![Outdated video](https://img.youtube.com/vi/q1pasjA9C-I/default.jpg)](https://www.youtube.com/watch?v=q1pasjA9C-I)

## Current state

SDVPCM is still in development and debugging stage, GUI is not finalized yet.
There are many debug menus and indicators all over the interface.

Most of the features should work as intended.

Target platform is Windows, not guaranteed to be working on other platforms.
Development is performed with [Qt 5.5.0 toolset](https://www.qt.io/).

## Contributing

- If you have captures of some SD video, containing digital audio encodings
  from formats that are not supported by the software and you can share
  those recordings - please do.
- If you have captures of supported PCM formats but you have throubles
  with properly decoding it, you can share problematic video and probably
  get some help with that, also improving decoder in the process.
- English and Russian translations are maintained by the developer but you
  can contribute translations to other languages using
  standard Qt Linguist tools.
- If you can test the decoder on other platforms and provide bugfixes
  to enable correct operation on those - please do.

## License
Program copyright 2020-2022.

This program is free software; Licence is TBD.
