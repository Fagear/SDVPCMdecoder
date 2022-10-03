#ifndef PCM1DATABLOCK_H
#define PCM1DATABLOCK_H

#include <deque>
#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcm1line.h"
#include "pcmsamplepair.h"

class PCM1DataBlock
{
public:
    // Interleave parameters.
    enum
    {
        INT_BLK_PER_FIELD = 8       // Number of interleave blocks per field.
    };

    enum
    {
        MIN_DEINT_DATA = 735        // Minimum amount of data to perform deinterleaving.
    };

    // Word offsets in the data block.
    enum
    {
        WORDP_STRIPE_ONE_OFS = 0,   // Word-pair offset of first sample in the first stripe of interleave block (L2,L93,L186,...).
        WORDP_STRIPE_TWO_OFS = 46,  // Word-pair offset of first sample in the first stripe of interleave block (L1,L94,L635,...).
        WORDP_STRIPE_LEN = 46,      // Word-pair count in the same range (L2-R2-L4..., L1-R1-L3...) for the normal length interleave block.
        WORDP_STRIPE_SHORT = 45,    // Word-pair count in the same range (L2-R2-L4..., L1-R1-L3...) for the short length interleave block.
        WORD_FIRST = 0,             // Word offset of first sample in the block.
        WORD_NEXT_OFS = 2,          // Word offset for the next word pair copy.
        WORD_CNT = (WORDP_STRIPE_LEN*4),    // Normal sample count in the interleave block.
        WORD_CNT_SHORT = (WORD_CNT-2)       // Short sample count in the interleave block.
    };

    enum
    {
        SAMPLE_CNT = WORD_CNT       // Number of audio samples in the block.
    };

    // Bits placeholders in debug string.
    enum
    {
        DUMP_BIT_ONE = '#',         // Bit char representing "1" for word with valid CRC.
        DUMP_BIT_ZERO = '-',        // Bit char representing "0" for word with valid CRC.
        DUMP_BIT_ONE_BAD = '1',     // Bit char representing "1" for word with invalid CRC.
        DUMP_BIT_ZERO_BAD = '0',    // Bit char representing "0" for word with invalid CRC.
        DUMP_WBRL_OK = '[',         // Start of the word with valid CRC.
        DUMP_WBRR_OK = ']',         // End of the word with valid CRC.
        DUMP_WBRL_BAD = '@',        // Start of the word with invalid CRC.
        DUMP_WBRR_BAD = '@',        // End of the word with invalid CRC.
        DUMP_SPLIT = 8              // How many samples per line to print in log.
    };

public:
    uint32_t frame_number;          // Number of source frame with first sample.
    uint16_t start_line;            // Number of line with first sample (top left).
    uint16_t stop_line;             // Number of line with last sample (bottom right).
    uint8_t interleave_num;         // Number of interleave block in the field.
    uint16_t sample_rate;           // Sample rate of samples in the block [Hz].
    bool emphasis;                  // Do samples need de-emphasis for playback?
    uint32_t process_time;          // Amount of time spent processing the data block [us].

private:
    uint16_t words[WORD_CNT];       // 13 bits words in the data block.
    bool word_crc[WORD_CNT];        // Flags for each word (was word intact or not by CRC).
    bool picked_left[WORD_CNT];     // Was sample from [SUBBLK_1] picked.
    bool picked_crc[WORD_CNT];      // Was CRC for all samples on the line picked.
    bool short_blk;                 // Is this the short (last) interleaving block with 182 words instead of 184 words?

public:
    PCM1DataBlock();
    PCM1DataBlock(const PCM1DataBlock &);
    PCM1DataBlock& operator= (const PCM1DataBlock &);
    void clear();
    void setWord(uint8_t index, uint16_t in_word, bool in_valid, bool in_picked_left = false, bool in_picked_crc = false);
    void setNormalLength();
    void setShortLength();
    void setEmphasis(bool in_set = true);
    bool canForceCheck();
    bool hasPickedWord(uint8_t word = WORD_CNT);
    bool hasPickedSample(uint8_t word = WORD_CNT);
    bool isWordCRCOk(uint8_t index);
    bool isWordValid(uint8_t index);
    bool isBlockValid();
    bool isNearSilence(uint8_t index);
    bool isAlmostSilent();
    bool isSilent();
    bool isNormalLength();
    bool isShortLength();
    bool hasEmphasis();
    uint8_t getWordCount();
    uint16_t getWord(uint8_t index);
    int16_t getSample(uint8_t index);
    uint8_t getErrorsAudio();
    uint8_t getErrorsTotal();
    std::string dumpWordsString(uint8_t line_index);
    std::string dumpSamplesString(uint8_t line_index);
    std::string dumpInfoString();
};

#endif // PCM1DATABLOCK_H
