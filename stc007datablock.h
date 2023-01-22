/**************************************************************************************************************************************************************
stc007datablock.h

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

Created: 2020-05

**************************************************************************************************************************************************************/

#ifndef STC007DATABLOCK_H
#define STC007DATABLOCK_H

#include <deque>
#include <stdint.h>
#include <string>
#include <QDebug>
#include <QString>
#include "config.h"
#include "pcmsamplepair.h"
#include "stc007line.h"

class STC007DataBlock
{
public:
    // Interleave parameters.
    enum
    {
        INTERLEAVE_OFS = 16     // [STC007Line] interleave offset between words in a data block.
    };

    // Line offsets for assembling diagonal interleaved block.
    enum
    {
        LINE_L0 = 0,                            // Line offset from the start of the buffer to [WORD_L0] word.
        LINE_R0 = (LINE_L0+INTERLEAVE_OFS),     // Line offset from the start of the buffer to [WORD_R0] word.
        LINE_L1 = (LINE_R0+INTERLEAVE_OFS),     // Line offset from the start of the buffer to [WORD_L1] word.
        LINE_R1 = (LINE_L1+INTERLEAVE_OFS),     // Line offset from the start of the buffer to [WORD_R1] word.
        LINE_L2 = (LINE_R1+INTERLEAVE_OFS),     // Line offset from the start of the buffer to [WORD_L2] word.
        LINE_R2 = (LINE_L2+INTERLEAVE_OFS),     // Line offset from the start of the buffer to [WORD_R2] word.
        LINE_P0 = (LINE_R2+INTERLEAVE_OFS),     // Line offset from the start of the buffer to [WORD_P0] word.
        LINE_Q0 = (LINE_P0+INTERLEAVE_OFS)      // Line offset from the start of the buffer to [WORD_Q0] word (contains S0 for PCM-F1).
    };

    enum
    {
        MIN_DEINT_DATA = (LINE_Q0)      // Minimum amount of data to perform deinterleaving.
    };

    // Word order in the data block (line offset = 0).
    enum
    {
        WORD_L0 = 0,        // Left channel 14/16-bit sample for word-offset 0 in the data block.
        WORD_R0 = 1,        // Right channel 14/16-bit sample for word-offset 0 in the data block.
        WORD_L1 = 2,        // Left channel 14/16-bit sample for word-offset 1 in the data block.
        WORD_R1 = 3,        // Right channel 14/16-bit sample for word-offset 1 in the data block.
        WORD_L2 = 4,        // Left channel 14/16-bit sample for word-offset 2 in the data block.
        WORD_R2 = 5,        // Right channel 14/16-bit sample for word-offset 2 in the data block.
        WORD_P0 = 6,        // P-word (parity) 14/16-bit sample for the data block.
        WORD_Q0 = 7,        // Q-word (ECC) 14-bit sample for the data block (not used in 16-bit mode).
        WORD_CNT            // Limiter for word-operations.
    };

    enum
    {
        SAMPLE_CNT = WORD_P0    // Number of audio samples in the block.
    };

    // PCM-F1 specific processing constants.
    enum
    {
        F1_S_MASK = 0x0003, // 2 LSBs of 16-bit word from/for S-word (in place of Q-word).
        F1_S_L0_OFS = 12,   // Number of bits to shift 15 and 16th LSBs from [WORD_L0].
        F1_S_R0_OFS = 10,   // Number of bits to shift 15 and 16th LSBs from [WORD_R0].
        F1_S_L1_OFS = 8,    // Number of bits to shift 15 and 16th LSBs from [WORD_L1].
        F1_S_R1_OFS = 6,    // Number of bits to shift 15 and 16th LSBs from [WORD_R1].
        F1_S_L2_OFS = 4,    // Number of bits to shift 15 and 16th LSBs from [WORD_L2].
        F1_S_R2_OFS = 2,    // Number of bits to shift 15 and 16th LSBs from [WORD_R2].
        F1_S_P0_OFS = 0,    // Number of bits to shift 15 and 16th LSBs from [WORD_P0].
        F1_WORD_OFS = 2,    // Number of bits to shift audio data from/for the 14-bit word.
    };

    // Resolution of audio data.
    enum
    {
        RES_14BIT,          // 14-bit EIAJ STC-007/STC-008.
        RES_16BIT,          // 16-bit PCM-F1 (Sony-extension with 15 and 16 bits in place of Q-word).
        RES_AUTO,           // Auto setting for resolution detection.
        RES_MAX             // Limiter for resolution operations.
    };

    // Audio data state.
    enum
    {
        AUD_ORIG,           // Audio data in original state (not modified since video lines).
        AUD_FIX_P,          // Audio data was fixed by parity (P-code).
        AUD_FIX_Q,          // Audio data was fixed by ECC (Q-code).
        AUD_BROKEN,         // Parity or ECC errors were found while no CRC errors were present - incorrectly assembled, BROKEN data block.
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
        DUMP_WBRL_CWD_FIX = '>',    // Start of the word that was fixed with CWD.
        DUMP_WBRR_CWD_FIX = '<',    // End of the word that was fixed with CWD.
        DUMP_WBRL_BAD = '@',        // Start of the word with invalid CRC.
        DUMP_WBRR_BAD = '@',        // End of the word with invalid CRC.
    };

public:
    uint32_t w_frame[WORD_CNT];     // Number of source frames for words.
    uint16_t w_line[WORD_CNT];      // Number of source lines for words.
    uint16_t sample_rate;           // Sample rate of samples in the data block [Hz].
    bool emphasis;                  // Do samples need de-emphasis for playback?
    bool cwd_applied;               // Flag for fixing block with CWD (applied corrections from ECC pre-scan).
    uint32_t process_time;          // Amount of time spent processing the data block [us].

private:
    uint16_t words[WORD_CNT];       // 14/16 bits words in the data block.
    bool line_crc[WORD_CNT];        // Flags for each word (was word intact or not by CRC in source PCM line).
    bool cwd_fixed[WORD_CNT];       // Flags for each word (was word from PCM line that was fixed by CWD).
    bool word_valid[WORD_CNT];      // Flags for each word (is word intact after correction).
    bool m2_format;                 // Are samples formatted for M2?
    uint8_t resolution;             // Resolution of audio data in data block.
    uint8_t audio_state;            // Audio data state in the block (patched by parity, ECC or broken).

public:
    STC007DataBlock();
    STC007DataBlock(const STC007DataBlock &);
    STC007DataBlock& operator= (const STC007DataBlock &);
    void clear();
    void setResolution(uint8_t in_res);
    void setWord(uint8_t index, uint16_t in_word, bool is_line_valid, bool is_cwd_fixed);
    void setSource(uint8_t index, uint32_t frame, uint16_t line);
    void setFixed(uint8_t index);
    void setValid(uint8_t index);
    void setAudioState(uint8_t in_state);
    void setEmphasis(bool in_set = true);
    void setM2Format(bool in_set = false);
    void markAsOriginalData();
    void markAsFixedByCWD();
    void markAsFixedByP();
    void markAsFixedByQ();
    void markAsUnsafe();
    void markAsBroken();
    void clearWordFixedByCWD(uint8_t index);
    void clearFixedByCWD();
    bool canForceCheck();
    bool isWordLineCRCOk(uint8_t index);
    bool isWordCWDFixed(uint8_t index);
    bool isWordValid(uint8_t index);
    bool isBlockValid();
    bool isAudioAlteredByCWD();
    bool isDataAlteredByCWD();
    bool isDataFixedByCWD();
    bool isDataFixedByP();
    bool isDataFixedByQ();
    bool isDataFixed();
    bool isDataBroken();
    bool isData14bit();
    bool isData16bit();
    bool isNearSilence(uint8_t index);
    bool isAlmostSilent();
    bool isSilent();
    bool isOnSeam();
    bool hasEmphasis();
    uint16_t getWord(uint8_t index);
    int16_t getSample(uint8_t index);
    uint8_t getResolution();
    uint8_t getAudioState();
    uint8_t getErrorsAudioSource();
    uint8_t getErrorsAudioCWD();
    uint8_t getErrorsAudioFixed();
    uint8_t getErrorsTotalSource();
    uint8_t getErrorsTotalCWD();
    uint8_t getErrorsTotalFixed();
    uint32_t getStartFrame();
    uint32_t getStopFrame();
    uint16_t getStartLine();
    uint16_t getStopLine();
    std::string dumpWordsString14bit();
    std::string dumpWordsString16bit();
    std::string dumpContentString();
};

#endif // STC007DATABLOCK_H
