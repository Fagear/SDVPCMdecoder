/**************************************************************************************************************************************************************
pcm1subline.h

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

Created: 2021-11

Binarized PCM-1 (not to be confused with PCM-F1) sub-line line container.

Data container used between [PCM1DataStitcher] and [PCM1Deinterleaver] modules.
Deinterleaving in PCM-1 has very cumbersome and illogical algorythm that operates with word pairs instead of whole contents of each line.
In essence, one line of PCM-1 contains three word-pairs, that are covered with the same CRC.
That's not the same as for PCM-16x0, where one line contains independent parts with separate CRCs for each part.
[PCM1DataStitcher] still operates on whole lines as it tries to determine vertical offset of the data in the frame,
but [PCM1Deinterleaver] at the final stage operates on word-pairs.
For the reasons above output format of [Binarizer] for PCM-1 keeps data for the whole line ([PCM1Line]),
but for deinterleaving its easier to split one line into three word-pairs/sub-lines
to avoid confusing calculations of line and word-pair offsets through all lines.

This object holds simplified data from a part of [PCM1Line], index of this part is stored in [line_part].
Data coordinates are dropped and CRC word replaced with simple flag "valid/invalid" in [crc].
This is not a sub-class of [PCMLine], so frame and line numbers are stored in [frame_number] and [line_number].
Data words are stored in [words[]], number of picked (with Bit Picker) bits from the sides of the line in [picked_bits_left] and [picked_bits_right].

**************************************************************************************************************************************************************/

#ifndef PCM1SUBLINE_H
#define PCM1SUBLINE_H

#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcm1line.h"

class PCM1SubLine
{
public:
    // Bit counts.
    enum
    {
        BITS_PER_WORD = (PCM1Line::BITS_PER_WORD),  // Number of bits per PCM data word.
        DATA_WORD_MASK = ((1<<BITS_PER_WORD)-1),    // Mask for a data word.
        BITS_PCM_DATA = (BITS_PER_WORD*2)           // Number of bits for data.
    };

    // Word order in the [words[]] for the line.
    enum
    {
        WORD_L,                 // Left channel 13-bit sample.
        WORD_R,                 // Right channel 13-bit sample.
        WORD_CNT                // Limiter for word-operations.
    };

    // Part [line_part] of the source video line that contained in this object.
    enum
    {
        PART_LEFT,              // 1st (left) part of the video line.
        PART_MIDDLE,            // 2nd (middle) part of the video line.
        PART_RIGHT,             // 3rd (right) part of the video line.
        PART_MAX                // Limiter for part-operations.
    };

    // Bits placeholders in debug log string.
    enum
    {
        DUMP_BIT_ONE = (PCMLine::DUMP_BIT_ONE),             // Bit char representing "1".
        DUMP_BIT_ZERO = (PCMLine::DUMP_BIT_ZERO),           // Bit char representing "0".
        DUMP_BIT_ONE_BAD = (PCMLine::DUMP_BIT_ONE_BAD),     // Bit char representing "1" with invalid CRC.
        DUMP_BIT_ZERO_BAD = (PCMLine::DUMP_BIT_ZERO_BAD)    // Bit char representing "0" with invalid CRC.
    };

public:
    uint32_t frame_number;      // Number of source frame for this line.
    uint16_t line_number;       // Number of line in the frame (#1=topmost).
    uint8_t picked_bits_left;   // Number of left bits of [WORD_L] that were picked after bad CRC.
    uint8_t picked_bits_right;  // Number of right bits of source line CRC that were picked after bad CRC.

private:
    uint8_t line_part;          // Part of the source video line.
    uint16_t words[WORD_CNT];   // 13 bit PCM words (3 MSBs unused).
    bool bw_set;                // Were BLACK and WHITE levels set for the line?
    bool crc;                   // Are all words ok?

public:
    PCM1SubLine();
    PCM1SubLine(const PCM1SubLine &);
    PCM1SubLine& operator= (const PCM1SubLine &);
    void clear();
    void setSilent();
    void setWord(uint8_t index, uint16_t in_word);
    void setLeft(uint16_t in_word);
    void setRight(uint16_t in_word);
    void setLinePart(uint8_t index);
    void setBWLevels(bool bw_ok = true);
    void setCRCValid(bool crc_ok = true);
    bool hasBWSet();
    bool hasPickedLeft();
    bool hasPickedRight();
    bool isCRCValid();
    uint16_t getWord(uint8_t index);
    uint16_t getLeft();
    uint16_t getRight();
    uint8_t getLinePart();
    std::string dumpWordsString();
    std::string dumpContentString();
};

#endif // PCM1SUBLINE_H
