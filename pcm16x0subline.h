/**************************************************************************************************************************************************************
pcm16x0subline.h

Copyright © 2023 Maksim Kryukov <fagear@mail.ru>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Created: 2021-07

Binarized PCM-1600/PCM-1610/PCM-1630 sub-line container.
Sub-class of [PCMLine].

Data container used between [Binarizer] and [PCM16X0DataStitcher]/[PCM16X0Deinterleaver] modules.
Object contains PCM-16x0-specific data after binarization.
It holds pre-calculated bit coordinates in [pixel_coordinates[][]], data words in [words[]]
and number of picked (with Bit Picker) bits from the sides of the line in [picked_bits_left] and [picked_bits_right].

PCM-16x0 line structure consists of three independent parts with two audio words, one parity word and one CRC in each.
[Binarizer] is a closed-loop system that targets a valid CRC in the line and it would confuse things if there was
more than one CRC per one line.
To make maximum data extraction possible and to leave [Binarizer] universal
the decision was made to "cut" PCM-16x0 line into three equivalent parts and run [Binarizer] on each part separately.
This way [Binarizer] still has to target a single CRC in each run and
external wrapper [VideoToDigital] handles setting up and running [Binarizer] three times on a single [VideoLine].
Thus name of this class is [PCM16X0SubLine] and not "PCM16X0Line" and there is [line_part] field that holds index of this sub-line in the source line.

**************************************************************************************************************************************************************/

#ifndef PCM16X0SUBLINE_H
#define PCM16X0SUBLINE_H

#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcmline.h"

//#define PCM16X0_LINE_EN_DBG_OUT     1       // Enable debug console output.
#define PCM16X0_LINE_HELP_SIZE  14      // Number of lines in help dump.

static const std::string PCM16X0_HELP_LIST[PCM16X0_LINE_HELP_SIZE]
{
    "   >---Source frame number",
    "   |      >---Line number in the source frame",
    "   |      |     >---Part of the source line ('0' - left, '1' - middle, '2' - right)                                                                  CRC-16 value calculated for words in the line---<",
    "   |      |     |     >---BLACK level (by brightness statistics)                 Line state: 'CRC OK' - all good, 'BD CRC!' - PCM detected, but with errors, 'No PCM!' - PCM not detected---<        |",
    "   |      |     |     |   >---WHITE level (by brightness statistics)                                                                           Time spent on line binarization (us)---<     |        |",
    "   |      |     |     |   |    >---Binarization detection: '=' - externally set, '?' - by stats estimate            Final binarization bit coordinates pixel shifting stage---<       |     |        |",
    "   |      |     |     |   |    |  >---Low binarization level (with hysteresis)                                              Final binarization level hysteresis depth---<     |       |     |        |",
    "   |      |     |     |   |    |  |   >---Binarization level (0...255)                Audio words: 'NS' - not silent, 'AS' - almost silent, 'FS' - fully silent---<     |     |       |     |        |",
    "   |      |     |     |   |    |  |   |   >---High binarization level (with hysteresis)                                               Pixels Per PCM Bit---<      |     |     |       |     |        |",
    "   |      |     |     |   |    |  |   |   |      >---Data start coordinate                                                         Queue index---<         |      |     |     |       |     |        |",
    "   |      |     |     |   |    |  |   |   |      |    >---Data end coordinate                  Number of picked bits from the right side---<     |         |      |     |     |       |     |        |",
    "   |      |     |     |   |    |  |   |   |      |    |    >---Control Bit state              Number of picked bits from the left side---< |     |         |      |     |     |       |     |        |",
    "   |      |     |     |   |    |  |   |   |      |    |    | [===============================PCM DATA===============================]    | |     |         |      |     |     |       |     |        |",
    "   |      |     |     |   |    |  |   |   |      |    |   [ ][     L/P/Rn     ][    R/P/Nn+1    ][    L/P/Rn+2    ][      CRCC      ]    | |     |         |      |     |     |       |     |        |",
};

class PCM16X0SubLine : public PCMLine
{
public:
    enum
    {
        SUBLINES_PER_LINE = 3   // Number of [PCM16X0SubLine] produced from one [VideoLine].
    };

    // Bit counts.
    enum
    {
        BITS_PER_WORD = 16,     // Number of bits per PCM data word.
        DATA_WORD_MASK = ((1<<BITS_PER_WORD)-1),    // Mask for a data word.
        BITS_PER_CRC = 16,      // Number of bits per CRC word.
        CRC_WORD_MASK = ((1<<BITS_PER_CRC)-1),      // Mask for a CRC word.
        BITS_PCM_DATA = ((BITS_PER_WORD*3)+BITS_PER_CRC),       // Number of bits for data.
        BITS_IN_LINE = ((BITS_PCM_DATA*SUBLINES_PER_LINE)+1),   // Total number of useful bits in one video line.
        BITS_LEFT_SHIFT = 34,   // Highest bit number for left part pixel-shifting.
        BITS_RIGHT_SHIFT = 107  // Lowest bit number for right part pixel-shifting.
    };

    // Word order in the [words[]] for the sub-line.
    enum
    {
        WORD_R1P1L1,            // Can contain: right channel 16-bit Nth sample, Nth parity, left channel 16-bit Nth sample.
        WORD_L2P2R2,            // Can contain: left channel 16-bit N+1 sample, N+1 parity, right channel 16-bit N+1 sample.
        WORD_R3P3L3,            // Can contain: right channel 16-bit N+2 sample, N+2 parity, left channel 16-bit N+3 sample.
        WORD_CRCC,              // CRCC 16-bit word for current sub-line.
        WORD_CNT                // Limiter for word-operations.
    };

    // Default CRC values.
    enum
    {
        CRC_SILENT = 0x0E10     // CRC for silent (muted) line.
    };

    // Part [line_part] of the source video line that contained in this object.
    enum
    {
        PART_LEFT,              // 1st (left) part of the video line.
        PART_MIDDLE,            // 2nd (middle) part of the video line.
        PART_RIGHT,             // 3rd (right) part of the video line.
        PART_MAX                // Limiter for part-operations.
    };

public:
    bool control_bit;           // Service/skew bit (129th bit) setting.
    uint8_t line_part;          // Part of the source video line.
    uint8_t picked_bits_left;   // Number of left bits of [WORD_R1P1L1] that were picked in binarizator after bad CRC.
    uint8_t picked_bits_right;  // Number of right bits of [WORD_CRCC] that were picked in binarizator after bad CRC.
    uint16_t queue_order;       // Order in a queue.

private:
    uint16_t pixel_coordinates[PCM_LINE_MAX_PS_STAGES][BITS_IN_LINE];   // Pre-calculated coordinates for all bits and pixel-shift stages.
    uint16_t words[WORD_CNT];   // 16 bit PCM words + 16 bit CRCC.

public:
    PCM16X0SubLine();
    PCM16X0SubLine(const PCM16X0SubLine &);
    PCM16X0SubLine& operator= (const PCM16X0SubLine &);

    void clear();
    void setSourceCRC(uint16_t in_crc = 0);
    void setSilent();
    void setWord(uint8_t index, uint16_t in_word);
    uint8_t getBitsPerObject();
    uint8_t getBitsPerSourceLine();
    uint8_t getBitsBetweenDataCoordinates();
    uint8_t getLeftShiftZoneBit();
    uint8_t getRightShiftZoneBit();
    uint16_t getVideoPixelByTable(uint8_t pcm_bit, uint8_t shift_stage);
    void calcCRC();
    uint16_t getSourceCRC();
    uint8_t getPCMType();
    uint16_t getWord(uint8_t index);
    int16_t getSample(uint8_t index);
    uint8_t getWordsDiffBitCount(PCM16X0SubLine *in_line = NULL);
    bool hasSameWords(PCM16X0SubLine *in_line = NULL);
    bool hasPickedWords();
    bool hasPickedLeft();
    bool hasPickedRight();
    bool isCRCValidIgnoreForced();
    bool isNearSilence(uint8_t index);
    bool isAlmostSilent();
    bool isSilent();
    bool isPicked(uint8_t word);
    std::string dumpWordsString();
    std::string dumpContentString();
    std::string helpDumpNext();

private:
    void calcCoordinates(uint8_t in_shift);
};

#endif // PCM16X0SUBLINE_H
