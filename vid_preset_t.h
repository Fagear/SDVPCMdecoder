/**************************************************************************************************************************************************************
vid_preset_t.h

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

Created: 2021-12

**************************************************************************************************************************************************************/

#ifndef VID_PRESET_T_H
#define VID_PRESET_T_H

#include <stdint.h>

class vid_preset_t
{
public:
    // Color modes for [colors].
    enum
    {
        COLOR_BW,               // Use all colors, process Y (brightness) channel.
        COLOR_R,                // Use only red channel.
        COLOR_G,                // Use only green channel.
        COLOR_B,                // Use only blue channel.
        COLOR_MAX
    };

public:
    uint8_t colors;             // Color channels to use.
    uint16_t crop_left;         // Number of pixels to crop from the beginning of the video line.
    uint16_t crop_right;        // Number of pixels to crop from the ending of the video line.
    uint16_t crop_top;          // Number of lines to skip from the top of the frame.
    uint16_t crop_bottom;       // Number of lines to skip from the bottom of the frame.

public:
    vid_preset_t();
    vid_preset_t(const vid_preset_t &in_object);
    vid_preset_t& operator= (const vid_preset_t &in_object);
    void clear();
};

#endif // VID_PRESET_T_H
