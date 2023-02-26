/**************************************************************************************************************************************************************
pcm16x0datablock.h

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

**************************************************************************************************************************************************************/

#ifndef PCM16X0DATABLOCK_H
#define PCM16X0DATABLOCK_H

#include <deque>
#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcm16x0subline.h"
#include "pcmsamplepair.h"

class PCM16X0DataBlock
{
public:
    // Interleave parameters.
    enum
    {
        SI_INTERLEAVE_OFS = 35,     // [PCM16X0SubLine] interleave offset between words in a data block in SI format.
        EI_INTERLEAVE_OFS = 490,    // [PCM16X0SubLine] interleave offset between words in a data block in EI format.
        INT_BLK_PER_FIELD = 7,      // Number of interleave blocks per field.
    };

    // Lines (horizontal) of interleave block.
    // Used for setting data into [PCM16X0DataBlock] structure.
    enum
    {
        LINE_1,         // Left or right channel word.
        LINE_2,         // Parity word.
        LINE_3,         // Left or right channel word.
        LINE_CNT        // Number of lines in one block.
    };

    // Sub-blocks (vertical) in the block.
    enum
    {
        SUBBLK_1,       // Left sub-block.
        SUBBLK_2,       // Middle sub-block.
        SUBBLK_3,       // Right sub-block.
        SUBBLK_CNT      // Number of sub-blocks.
    };

    // Sub-line offsets for assembling interleave block.
    enum
    {
        LINE_1_SI_OFS = 0,                                  // Sub-line offset from the start of the buffer to words in [LINE_1] in SI format.
        LINE_2_SI_OFS = (LINE_1_SI_OFS+SI_INTERLEAVE_OFS),  // Sub-line offset from the start of the buffer to words in [LINE_2] in SI format.
        LINE_3_SI_OFS = (LINE_2_SI_OFS+SI_INTERLEAVE_OFS),  // Sub-line offset from the start of the buffer to words in [LINE_3] in SI format.
        LINE_1_EI_OFS = 0,                                  // Sub-line offset from the start of the buffer to words in [LINE_1] in EI format.
        LINE_2_EI_OFS = (LINE_1_EI_OFS+EI_INTERLEAVE_OFS),  // Sub-line offset from the start of the buffer to words in [LINE_2] in EI format.
        LINE_3_EI_OFS = (LINE_2_EI_OFS+EI_INTERLEAVE_OFS),  // Sub-line offset from the start of the buffer to words in [LINE_3] in EI format.
    };

    enum
    {
        MIN_DEINT_DATA_SI = (SI_INTERLEAVE_OFS*2),          // Minimum amount of data to perform deinterleaving (SI format).
        MIN_DEINT_DATA_EI = (EI_INTERLEAVE_OFS*2)           // Minimum amount of data to perform deinterleaving (EI format).
    };

    // Word order in the sub-blocks.
    // Used for getting data out of [PCM16X0DataBlock] structure.
    // Does NOT directly correlate with line counts like [LINE_1], depends on sub-block number and block order.
    // See [getWordToLine()] and [getLineToWord()].
    enum
    {
        WORD_L,     // Left channel 16-bit sample in the sub-block.
        WORD_R,     // Right channel 16-bit sample in the sub-block.
        WORD_P,     // Parity 16-bit word that covers [WORD_L] and [WORD_R] samples.
        WORD_CNT    // Number of samples in one sub-block.
    };

    enum
    {
        SAMPLE_CNT = (WORD_P*PCM16X0SubLine::SUBLINES_PER_LINE)     // Number of audio samples in the block.
    };

    // Audio data state.
    enum
    {
        AUD_ORIG,           // Audio data in original state (not modified since video lines).
        AUD_FIX_P,          // Audio data was fixed by parity (P-code).
        AUD_BROKEN,         // Parity errors were found while no CRC errors were present - incorrectly assembled, BROKEN data block.
        AUD_MAX             // Limiter for audio state operations.
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
    };

public:
    uint32_t frame_number;          // Number of source frame with first sample.
    uint16_t start_line;            // Number of line with first sample (top left).
    uint16_t stop_line;             // Number of line with last sample (bottom right).
    uint8_t start_part;             // Part of the source video line with first sample.
    uint8_t stop_part;              // Part of the source video line with last sample.
    uint16_t queue_order;           // Number of line in the queue.
    uint16_t sample_rate;           // Sample rate of samples in the block [Hz].
    bool emphasis;                  // Do samples need de-emphasis for playback?
    bool ei_format;                 // Is block in EI format (PCM-1630)? Otherwise SI format.
    bool code;                      // Does block contain CODE instead of AUDIO?
    uint32_t process_time;          // Amount of time spent processing the data block [us].

private:
    uint16_t words[SUBBLK_CNT][LINE_CNT];   // 16 bits words.
    bool word_crc[SUBBLK_CNT][LINE_CNT];    // Flags for each word (was word intact or not by CRC).
    bool word_valid[SUBBLK_CNT][LINE_CNT];  // Flags for each word (is word intact after correction).
    bool picked_left[LINE_CNT];             // Was sample from [SUBBLK_1] picked.
    bool picked_crc[LINE_CNT];              // Was CRC for all samples on the line picked.
    uint8_t audio_state[SUBBLK_CNT];        // Audio data state in the block (patched by parity or broken).
    bool order_even;                        // Determines odd or even order of samples.

public:
    PCM16X0DataBlock();
    PCM16X0DataBlock(const PCM16X0DataBlock &);
    PCM16X0DataBlock& operator= (const PCM16X0DataBlock &);
    void clear();
    void setWord(uint8_t blk, uint8_t line, uint16_t in_word, bool in_valid, bool in_picked_left = false, bool in_picked_crc = false);
    void setOrderOdd();
    void setOrderEven();
    void setAudioState(uint8_t blk, uint8_t in_state = AUD_ORIG);
    void setEmphasis(bool in_set = true);
    void fixWord(uint8_t blk, uint8_t word, uint16_t in_word);
    void markAsOriginalData(uint8_t blk = SUBBLK_CNT);
    void markAsFixedByP(uint8_t blk);
    void markAsUnsafe();
    void markAsBroken(uint8_t blk = SUBBLK_CNT);
    void markAsBad(uint8_t blk, uint8_t line);
    bool canForceCheck();
    bool hasPickedLeft(uint8_t line);
    bool hasPickedLeftBySub(uint8_t blk = SUBBLK_CNT);
    bool hasPickedCRC(uint8_t line);
    bool hasPickedCRCBySub(uint8_t blk = SUBBLK_CNT);
    bool hasPickedWord(uint8_t blk, uint8_t word = WORD_CNT);
    bool hasPickedSample(uint8_t blk, uint8_t word = WORD_CNT);
    bool hasPickedParity(uint8_t blk);
    uint8_t getPickedAudioSamples(uint8_t blk);
    bool isWordCRCOk(uint8_t blk, uint8_t word);
    bool isWordValid(uint8_t blk, uint8_t word);
    bool isOrderOdd();
    bool isOrderEven();
    bool isBlockValid(uint8_t blk = SUBBLK_CNT);
    bool isDataFixedByP(uint8_t blk = SUBBLK_CNT);
    bool isDataFixedByBP(uint8_t blk = SUBBLK_CNT);
    bool isDataFixed(uint8_t blk = SUBBLK_CNT);
    bool isDataBroken(uint8_t blk = SUBBLK_CNT);
    bool isNearSilence(uint8_t blk, uint8_t word);
    bool isAlmostSilent();
    bool isSilent();
    bool isInEIFormat();
    bool hasEmphasis();
    bool hasCode();
    uint16_t getWord(uint8_t blk, uint8_t word);
    int16_t getSample(uint8_t blk, uint8_t word);
    uint8_t getAudioState(uint8_t blk = SUBBLK_CNT);
    uint8_t getErrorsAudio(uint8_t blk = SUBBLK_CNT);
    uint8_t getErrorsFixedAudio(uint8_t blk = SUBBLK_CNT);
    uint8_t getErrorsTotal(uint8_t blk = SUBBLK_CNT);
    std::string dumpWordsString();
    std::string dumpContentString();
    std::string dumpServiceBitsString();

private:
    uint8_t getWordToLine(uint8_t blk, uint8_t word);
    uint8_t getLineToWord(uint8_t blk, uint8_t line);
};

#endif // PCM16X0DATABLOCK_H
