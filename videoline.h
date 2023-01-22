/**************************************************************************************************************************************************************
videoline.h

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

Created: 2020-04

Video line container.
Data container used between [VideoInFFMPEG] and [Binarizer] modules.
This object holds grayscale brightnesses for all pixels of the single line of the source frame in [pixel_data].
It also contains information about number of the source frame and line, color channel setting used during capture.
In addition this object can hold service tag, signalling for such events as start of a new source or end of a frame.
When [service_type] is set to something different from [SRVLINE_NO], other fields are not used.

**************************************************************************************************************************************************************/

#ifndef VIDEOLINE_H
#define VIDEOLINE_H

#include "vid_preset_t.h"
#include <stdint.h>
#include <string>
#include <vector>

class VideoLine
{
public:
    // Service tags for [service_type].
    enum
    {
        SRVLINE_NO,             // Regular line with audio data.
        SRVLINE_NEW_FILE,       // New file opened (with path in [file_path]).
        SRVLINE_END_FILE,       // File ended.
        SRVLINE_FILLER,         // Filler line (for frame padding).
        SRVLINE_END_FIELD,      // Field of a frame ended.
        SRVLINE_END_FRAME,      // Frame ended.
    };

public:
    uint32_t frame_number;              // Number of source frame for this line.
    uint16_t line_number;               // Number of line in the frame (#1=topmost).
    uint8_t colors;                     // Source color channel (see [vid_preset_t.h]), used for visualization.
    std::vector<uint8_t> pixel_data;    // Array of grayscale 8-bit pixels.
    std::string file_path;              // Path of decoded file (set with [serv_new_file]).
    uint32_t process_time;              // Amount of time spent processing the line [us].
    bool scan_done;                     // This line was scanned for data coordinates by [Binarizer];

private:
    bool empty;                         // Is line marked as "empty" (no items in [pixel_data]) for fast skipping?
    bool doubled;                       // Was data doubled in width (to aid binarization)?
    uint8_t service_type;               // Type of service line.

public:
    VideoLine();
    VideoLine(const VideoLine &);
    VideoLine& operator= (const VideoLine &);
    void clear();
    void setLength(uint16_t in_length);
    void setBrighness(uint16_t pixel_index, uint8_t data);
    void setEmpty(bool flag);
    void setDoubleWidth(bool flag);
    void setServNo();
    void setServNewFile(std::string path);
    void setServEndFile();
    void setServFiller();
    void setServEndField();
    void setServEndFrame();
    bool isEmpty();
    bool isDoubleWidth();
    bool isServiceLine();
    bool isServNewFile();
    bool isServEndFile();
    bool isServFiller();
    bool isServEndField();
    bool isServEndFrame();
};

#endif // VIDEOLINE_H
