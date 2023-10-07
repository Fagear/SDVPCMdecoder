/**************************************************************************************************************************************************************
pcm1line.h

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

Binarized PCM-1 (not to be confused with PCM-F1) line container.
Sub-class of [PCMLine].

Data container used between [Binarizer] and [PCM1DataStitcher] modules.
Object contains PCM-1-specific data after binarization.
It holds pre-calculated bit coordinates in [pixel_coordinates[][]], data words in [words[]]
and number of picked (with Bit Picker) bits from the sides of the line in [picked_bits_left] and [picked_bits_right].

**************************************************************************************************************************************************************/

#ifndef PCM1LINE_H
#define PCM1LINE_H

#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcmline.h"

//#define PCM1_LINE_EN_DBG_OUT    1       // Enable debug console output.
#define PCM1_LINE_HELP_SIZE     12      // Number of lines in help dump.

static const std::string PCM1_HELP_LIST[PCM1_LINE_HELP_SIZE]
{
    "   >---Source frame number",
    "   |      >---Line number in the source frame                                                                                                                            CRC-16 value calculated for words in the line---<",
    "   |      |      >---BLACK level (by brightness statistics)                                          Line state: 'CRC OK' - all good, 'BD CRC!' - PCM detected, but with errors, 'No PCM!' - PCM not detected---<        |",
    "   |      |      |   >---WHITE level (by brightness statistics)                                                                                                    Time spent on line binarization (us)---<     |        |",
    "   |      |      |   |    >---Binarization detection: '=' - externally set, '?' - by stats estimate                                     Final binarization bit coordinates pixel shifting stage---<       |     |        |",
    "   |      |      |   |    |  >---Low binarization level (with hysteresis)                                                                       Final binarization level hysteresis depth---<     |       |     |        |",
    "   |      |      |   |    |  |   >---Binarization level (0...255)                                                                                            Pixels Per PCM Bit---<         |     |       |     |        |",
    "   |      |      |   |    |  |   |   >---High binarization level (with hysteresis)            Audio words: 'NS' - not silent, 'AS' - almost silent, 'FS' - fully silent---<       |         |     |       |     |        |",
    "   |      |      |   |    |  |   |   |      >---Data start coordinate                                                     Number of picked bits from the right side---<   |       |         |     |       |     |        |",
    "   |      |      |   |    |  |   |   |      |  >---Data end coordinate                                                   Number of picked bits from the left side---< |   |       |         |     |       |     |        |",
    "   |      |      |   |    |  |   |   |      |  |    [=================================================PCM DATA=================================================]    | |   |       |         |     |       |     |        |",
    "   |      |      |   |    |  |   |   |      |  |    [      Ln     ][      Rn     ][     Ln+1    ][     Rn+1    ][     Ln+2    ][     Rn+2    ][      CRCCn     ]    | |   |       |         |     |       |     |        |"
};

class PCM1Line : public PCMLine
{
public:
    enum
    {
        SUBLINES_PER_LINE = 3   // Number of [PCM1SubLine] produced from one [VideoLine]/[PCM1Line].
    };

    // Bit counts.
    enum
    {
        BITS_PER_WORD = 13,         // Number of bits per PCM data word.
        DATA_WORD_MASK = ((1<<BITS_PER_WORD)-1),    // Mask for a data word.
        BITS_PER_CRC = 16,          // Number of bits per CRC word.
        CRC_WORD_MASK = ((1<<BITS_PER_CRC)-1),      // Mask for a CRC word.
        BITS_PCM_DATA = ((BITS_PER_WORD*6)+BITS_PER_CRC),     // Number of bits for data.
        BITS_IN_LINE = BITS_PCM_DATA,       // Total number of useful bits in one video line.
        BITS_LEFT_SHIFT = 16,       // Highest bit number for left part pixel-shifting.
        BITS_RIGHT_SHIFT = 52,      // Lowest bit number for right part pixel-shifting.
        BIT_RANGE_POS = (1<<12),    // R bit, that determines value range of other bits.
        BIT_SIGN_POS = (1<<11)      // Sign bit in the source 13-bit word.
    };

    // Word order in the [words[]] for the line.
    enum
    {
        WORD_L2,                // Left channel 13-bit word.
        WORD_R2,                // Right channel 13-bit word.
        WORD_L4,                // Left channel 13-bit word.
        WORD_R4,                // Right channel 13-bit word.
        WORD_L6,                // Left channel 13-bit word.
        WORD_R6,                // Right channel 13-bit word.
        WORD_CRCC,              // CRCC 16-bit word for current line.
        WORD_CNT                // Limiter for word-operations.
    };

    // Default CRC values.
    enum
    {
        CRC_SILENT = 0xECBF     // CRC for silent (muted) line.
    };

public:
    uint8_t picked_bits_left;   // Number of left bits of [WORD_L2] that were picked after bad CRC.
    uint8_t picked_bits_right;  // Number of right bits of [WORD_CRCC] that were picked after bad CRC.

private:
    uint16_t pixel_coordinates[PCM_LINE_MAX_PS_STAGES][BITS_PCM_DATA];      // Pre-calculated coordinates for all bits and pixel-shift stages.
    uint16_t words[WORD_CNT];   // 13 bit PCM words (3 MSBs unused) + 16 bit CRCC.

public:
    PCM1Line();
    PCM1Line(const PCM1Line &);
    PCM1Line& operator= (const PCM1Line &);

    void clear();
    void setServHeader();
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
    uint8_t getWordsDiffBitCount(PCM1Line *in_line = NULL);
    bool hasSameWords(PCM1Line *in_line = NULL);
    bool hasPickedWords();
    bool hasPickedLeft();
    bool hasPickedRight();
    bool hasHeader();
    bool isCRCValidIgnoreForced();
    bool isNearSilence(uint8_t index);
    bool isAlmostSilent();
    bool isSilent();
    bool isServHeader();
    std::string dumpWordsString();
    std::string dumpContentString();
    std::string helpDumpNext();

private:
    void calcCoordinates(uint8_t in_shift);
};

#endif // PCM1LINE_H
