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
