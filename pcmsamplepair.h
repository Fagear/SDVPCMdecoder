/**************************************************************************************************************************************************************
pcmsamplepair.h

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

Created: 2020-09

**************************************************************************************************************************************************************/

#ifndef PCMSAMPLEPAIR_H
#define PCMSAMPLEPAIR_H

#include <stdint.h>
#include <string>
#include <QDebug>
#include "config.h"

//------------------------ Single 16-bit audio sample container.
class PCMSample
{
    // Bits placeholders in debug log string.
    enum
    {
        DUMP_BIT_ONE = '#',         // Bit char representing "1" in a valid word.
        DUMP_BIT_ZERO = '-',        // Bit char representing "0" in a valid word.
        DUMP_BIT_ONE_BAD = '1',     // Bit char representing "1" in an invalid word.
        DUMP_BIT_ZERO_BAD = '0',    // Bit char representing "0" in an invalid word.
        DUMP_WBRL_OK = '[',         // Start of the word from valid data block.
        DUMP_WBRR_OK = ']',         // End of the word from valid data block.
        DUMP_WBRL_BAD = '@',        // Start of the word from invalid data block.
        DUMP_WBRR_BAD = '@'         // End of the word from invalid data block.
    };

public:
    int16_t audio_word;             // Audio data (16-bit) zero-centered.
    uint64_t index;                 // Global sample index from the start of the source.
    bool data_block_ok;             // Was source data block valid?
    bool word_valid;                // Does this sample contain valid audio data?
    bool word_fixed;                // Was this sample fixed with error correction while deinterleaving?
    bool word_masked;               // Was this sample altered in audio processor? (mute, interpolation)

public:
    PCMSample();
    PCMSample(const PCMSample &);
    PCMSample& operator= (const PCMSample &);
    void clear();
    void setValue(int16_t in_val);
    void setIndex(uint64_t in_idx);
    void setInvalid();
    void setValid();
    void setFixed();
    void setMasked();
    void setValidityByBlock();
    int16_t getValue();
    uint16_t getAmplitude();
    uint64_t getIndex();
    bool isSilent();
    bool isValid();
    bool isFixed();
    bool isMasked();
    std::string dumpWordsString();
};

//------------------------ [PCMSample] pair container.
class PCMSamplePair
{
public:
    // Sample rates.
    enum
    {
        SAMPLE_RATE_UNKNOWN = 0,
        SAMPLE_RATE_AUTO = 1,
        SAMPLE_RATE_44056 = 44056,
        SAMPLE_RATE_44100 = 44100,
        SAMPLE_RATE_MAX
    };

    // Channel selector.
    enum
    {
        CH_LEFT,
        CH_RIGHT,
        CH_MAX
    };

    // Service tags for [service_type].
    enum
    {
        SRV_NO,                 // Regular PCM line with audio data.
        SRV_NEW_FILE,           // New file opened (with path in [file_path]).
        SRV_END_FILE,           // File ended.
    };

public:
    PCMSample samples[CH_MAX];  // Sample set for this data point.
    uint16_t sample_rate;       // Sample rate for this data point.
    bool emphasis;              // Do samples need de-emphasis?
    uint8_t service_type;       // Type of the service tag.
    std::string file_path;      // Path of decoded file (set with [SRV_NEW_FILE]).

public:
    PCMSamplePair();
    PCMSamplePair(const PCMSamplePair &);
    PCMSamplePair& operator= (const PCMSamplePair &);
    void clear();
    void setServNewFile(std::string path);
    void setServEndFile();
    bool setSample(uint8_t channel, int16_t sample, bool block_ok = true, bool word_ok = true, bool word_fixed = false);
    void setSamplePair(int16_t ch1_sample, int16_t ch2_sample,
                       bool ch1_block, bool ch2_block,
                       bool ch1_word = true, bool ch2_word = true,
                       bool ch1_fixed = false, bool ch2_fixed = false);
    void setValidityByBlock();
    void setSampleRate(uint16_t in_rate);
    void setIndex(uint64_t in_idx);
    void setEmphasis(bool on = true);
    int16_t getSample(uint8_t channel);
    uint16_t getSampleRate();
    static size_t getSampleSize();
    uint64_t getIndex();
    bool isSampleValid(uint8_t channel);
    bool isBlockValid(uint8_t channel);
    bool isEmphasisSet();
    bool isSilent();
    bool isServicePair();
    bool isServNewFile();
    bool isServEndFile();
    bool isReadyForOutput();
    std::string dumpWordsString();
    std::string dumpContentString();
};

#endif // PCMSAMPLEPAIR_H
