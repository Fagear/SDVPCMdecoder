#ifndef SAMPLES2WAV_H
#define SAMPLES2WAV_H

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdint.h>
#include <string>
#include "config.h"
#include "pcmsamplepair.h"
#include "stc007datablock.h"

#ifndef QT_VERSION
    #undef TW_EN_DBG_OUT
#endif

#define TW_WAV_HEAD_SIZE    44
#define TW_AUD_BUF_SIZE     32768
#define TW_FS_BUF_SIZE      2097152

class SamplesToWAV
{
public:
    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_PROCESS = (1<<1),   // General stage-by-stage logging.
        LOG_WAVE_SAVE = (1<<2), // Saving chunks of data to file.
    };

private:
    char stream_buffer[TW_FS_BUF_SIZE];
    std::ofstream file_hdl;
    std::string path_for_wav;
    std::string file_name;
    union
    {
        int16_t words[TW_AUD_BUF_SIZE];
        uint8_t bytes[TW_AUD_BUF_SIZE*2];
    } audio;
    bool new_file;
    uint8_t log_level;                  // Setting for debugging log level.
    uint16_t sample_rate;
    uint16_t cur_pos;
    uint32_t written_data;

public:
    SamplesToWAV();
    void setLogLevel(uint8_t in_level);
    void setFolder(std::string path);
    void setName(std::string name);
    void setSampleRate(uint16_t in_rate);
    void prepareNewFile();
    void saveAudio(int16_t in_left, int16_t in_right);
    void purgeBuffer();
    void releaseFile();

private:
    void addHeader();
    void updateHeader();
};

#endif // SAMPLES2WAV_H
