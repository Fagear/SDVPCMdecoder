#ifndef PCMSAMPLEPAIR_H
#define PCMSAMPLEPAIR_H

#include <stdint.h>
#include <string>
#include "config.h"

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
    int16_t audio_word;
    uint64_t index;
    bool data_block_ok;
    bool word_ok;
    bool word_processed;

public:
    PCMSample();
    PCMSample(const PCMSample &);
    PCMSample& operator= (const PCMSample &);
    void clear();
    void setValue(int16_t in_val);
    void setIndex(uint64_t in_idx);
    void setInvalid();
    void setFixed();
    void setProcessed();
    void setValidityByBlock();
    int16_t getValue();
    uint64_t getIndex();
    bool isSilent();
    bool isValid();
    bool isProcessed();
    std::string dumpWordsString();
};

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
        SRV_NO,             // Regular PCM line with audio data.
        SRV_NEW_FILE,       // New file opened (with path in [file_path]).
        SRV_END_FILE,       // File ended.
    };

public:
    PCMSample samples[CH_MAX];
    uint16_t sample_rate;
    uint64_t index;
    bool emphasis;
    uint8_t service_type;       // Type of the service tag.
    std::string file_path;      // Path of decoded file (set with [SRV_NEW_FILE]).

public:
    PCMSamplePair();
    PCMSamplePair(const PCMSamplePair &);
    PCMSamplePair& operator= (const PCMSamplePair &);
    void clear();
    void setServNewFile(std::string path);
    void setServEndFile();
    bool setSample(uint8_t channel, int16_t sample, bool block_ok, bool word_ok);
    void setSamplePair(int16_t ch1_sample, int16_t ch2_sample, bool ch1_block, bool ch2_block, bool ch1_word, bool ch2_word);
    void setValidityByBlock();
    void setSampleRate(uint16_t in_rate);
    void setIndex(uint64_t in_idx);
    void setEmphasis(bool on = true);
    int16_t getSample(uint8_t channel);
    uint16_t getSampleRate();
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
