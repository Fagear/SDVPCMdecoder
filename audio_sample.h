#ifndef AUDIO_SAMPLE_H
#define AUDIO_SAMPLE_H

#include <stdint.h>

typedef union
{
    struct
    {
        int16_t word_left;
        int16_t word_right;
    };
    uint8_t bytes[4];
} sample_pair_t;

#endif // AUDIO_SAMPLE_H

