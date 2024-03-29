/**************************************************************************************************************************************************************
arvidline.h

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

Created: 2022-11

**************************************************************************************************************************************************************/

#ifndef ARVIDLINE_H
#define ARVIDLINE_H

#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcmline.h"

// ArVid Audio prototype.
class ArVidLine : public PCMLine
{
public:
    // Bit counts.
    enum
    {
        BITS_PER_WORD = 8,
        WORD_MASK = ((1<<BITS_PER_WORD)-1),         // Mask for a data word.
        BITS_START = 12,        // Number of bits for PCM START marker.
        BITS_PCM_DATA = 144,    // Number of bits for data.
        BITS_IN_LINE = (BITS_START+BITS_PCM_DATA),  // Total number of useful bits in one video line.
        BITS_LEFT_SHIFT = 24,   // Highest bit number for left part pixel-shifting.
        BITS_RIGHT_SHIFT = 96   // Lowest bit number for right part pixel-shifting.
    };

    enum
    {
        WORD_CNT = 18
    };

private:
    uint16_t pixel_coordinates[PCM_LINE_MAX_PS_STAGES][BITS_PCM_DATA];  // Pre-calculated coordinates for all bits and pixel-shift stages.
    uint8_t words[WORD_CNT];    // 8 bit words.

public:
    ArVidLine();
    ArVidLine(const ArVidLine &);
    ArVidLine& operator= (const ArVidLine &);

    void clear();
    void setSourceCRC(uint16_t in_crc = 0);
    void setSilent();
    void setWord(uint8_t index, uint8_t in_word);

    uint8_t getBitsPerSourceLine();
    uint8_t getBitsBetweenDataCoordinates();
    uint8_t getLeftShiftZoneBit();
    uint8_t getRightShiftZoneBit();
    uint16_t getVideoPixelByTable(uint8_t pcm_bit, uint8_t shift_stage);
    void calcCRC();
    uint16_t getSourceCRC();
    uint8_t getPCMType();
    int16_t getSample(uint8_t index);

    bool isCRCValidIgnoreForced();

    bool isNearSilence(uint8_t index);
    bool isAlmostSilent();
    bool isSilent();

    std::string dumpWordsString();
    std::string dumpContentString();
    std::string helpDumpNext();

private:
    void calcCoordinates(uint8_t in_shift);
};

#endif // ARVIDLINE_H
