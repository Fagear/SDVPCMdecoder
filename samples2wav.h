/**************************************************************************************************************************************************************
samples2wav.h

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

#ifndef SAMPLES2WAV_H
#define SAMPLES2WAV_H

#include <cstdio>
#include <stdint.h>
#include <string>
#include <QDebug>
#include <QFile>
#include <QObject>
#include <QString>
#include "config.h"
#include "pcmsamplepair.h"

#ifndef QT_VERSION
    #undef TW_EN_DBG_OUT
#endif

class SamplesToWAV : public QObject
{
    Q_OBJECT
public:
    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_PROCESS = (1<<1),   // General stage-by-stage logging.
        LOG_WAVE_SAVE = (1<<2), // Saving chunks of data to file.
    };

    // WAV header structure.
    enum
    {
        HDR_ID_SZ = 4,          // [chunkId] "RIFF"
        HDR_ID_OFS = 0,         // [HDR_ID_SZ] field offset
        HDR_SIZE_SZ = 4,        // [chunkSize]
        HDR_SIZE_OFS = (HDR_ID_OFS+HDR_ID_SZ),
        HDR_WAVE_SZ = 4,        // [format] "WAVE"
        HDR_WAVE_OFS = (HDR_SIZE_OFS+HDR_SIZE_SZ),
        HDR_SUBID1_SZ = 4,      // [subchunk1Id] "fmt "
        HDR_SUBID1_OFS = (HDR_WAVE_OFS+HDR_WAVE_SZ),
        HDR_SUBSIZE1_SZ = 4,    // [subchunk1Size] 16 bytes up to [HDR_SUBID2_SZ]
        HDR_SUBSIZE1_OFS = (HDR_SUBID1_OFS+HDR_SUBID1_SZ),
        HDR_FMT_SZ = 2,         // [audioFormat]
        HDR_FMT_OFS = (HDR_SUBSIZE1_OFS+HDR_SUBSIZE1_SZ),
        HDR_CHNLS_SZ = 2,       // [numChannels] Number of channels
        HDR_CHNLS_OFS = (HDR_FMT_OFS+HDR_FMT_SZ),
        HDR_SRATE_SZ = 4,       // [sampleRate] Samples per second
        HDR_SRATE_OFS = (HDR_CHNLS_OFS+HDR_CHNLS_SZ),
        HDR_BRATE_SZ = 4,       // [byteRate] Bytes per second
        HDR_BRATE_OFS = (HDR_SRATE_OFS+HDR_SRATE_SZ),
        HDR_SSIZE_SZ = 2,       // [blockAlign] Bytes per samples for all channels
        HDR_SSIZE_OFS = (HDR_BRATE_OFS+HDR_BRATE_SZ),
        HDR_SBITS_SZ = 2,       // [bitsPerSample] Bits per one sample
        HDR_SBITS_OFS = (HDR_SSIZE_OFS+HDR_SSIZE_SZ),
        HDR_SUBID2_SZ = 4,      // [subchunk2Id] "data"
        HDR_SUBID2_OFS = (HDR_SBITS_OFS+HDR_SBITS_SZ),
        HDR_SUBSIZE2_SZ = 4,    // [subchunk2Size] Number of bytes of wave data after the header.
        HDR_SUBSIZE2_OFS = (HDR_SUBID2_OFS+HDR_SUBID2_SZ),
        HDR_SZ = (HDR_SUBSIZE2_OFS+HDR_SUBSIZE2_SZ) // Total header size
    };

private:
    QFile file_out;                     // Handler for the output file.
    QString file_path;                  // File directory.
    QString file_name;                  // File name.
    QString output_path;                // Full file name/path.
    uint8_t log_level;                  // Setting for debugging log level.
    uint16_t sample_rate;               // Preset sample rate to write to the header.
    bool error_lock;                    // Prevent opening file if in error.
    static const uint8_t default_header[HDR_SZ];

public:
    SamplesToWAV();

private:
    bool openOutput();
    bool addHeader();
    bool updateHeader();

public slots:
    void setLogLevel(uint8_t in_level);
    void setFolder(QString path);
    void setName(QString name);
    void setSampleRate(uint16_t in_rate);
    void prepareNewFile();
    void saveAudio(PCMSamplePair in_audio);
    void purgeBuffer();
    void releaseFile();

signals:
    void fileError(QString);
};

#endif // SAMPLES2WAV_H
