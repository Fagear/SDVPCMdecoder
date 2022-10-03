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
        BITS_PCM_DATA = (BITS_PER_WORD*2)           // Number of bits for data.
    };

    // Word order in the [words[]] for the line.
    enum
    {
        WORD_L,                 // Left channel 13-bit sample.
        WORD_R,                 // Right channel 13-bit sample.
        WORD_MAX                // Limiter for word-operations.
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
    uint16_t words[WORD_MAX];   // 13 bit PCM words (3 MSBs unused) + 16 bit CRCC.
    bool bw_set;                // Were BLACK and WHITE levels set for the line?
    bool crc;                   // Are all words ok?

public:
    PCM1SubLine();
    PCM1SubLine(const PCM1SubLine &);
    PCM1SubLine& operator= (const PCM1SubLine &);
    void clear();
    void setWord(uint8_t index, uint16_t in_word);
    void setLeft(uint16_t in_word);
    void setRight(uint16_t in_word);
    void setLinePart(uint8_t index);
    void setBWLevels(bool bw_ok = true);
    void setCRCValid(bool crc_ok = true);
    void setSilent();
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
