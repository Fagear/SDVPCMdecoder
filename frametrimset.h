#ifndef FRAMETRIMSET_H
#define FRAMETRIMSET_H

#include <stdint.h>

class CoordinatePair
{
public:
    // Bad data coordinates markers.
    enum
    {
        NO_COORD_LEFT = -32768,         // No history for good left data coordinate.
        NO_COORD_RIGHT = 32767          // No history for good right data coordinate.
    };

public:
    int16_t data_start;         // Pixel coordinate of PCM data start.
    int16_t data_stop;          // Pixel coordinate of PCM data stop.
    bool from_doubled;          // Was source of these coordinates a doubled-width [VideoLine]?
    bool not_sure;              // Do these coordinates produce valid results?

public:
    CoordinatePair();
    CoordinatePair(const CoordinatePair &in_object);
    CoordinatePair& operator= (const CoordinatePair &in_object);
    bool operator== (const CoordinatePair &in_object);
    bool operator!= (const CoordinatePair &in_object);
    CoordinatePair& operator- (const CoordinatePair &in_object);
    bool operator< (const CoordinatePair &in_object);
    void clear();
    bool setCoordinates(int16_t in_start, int16_t in_stop);
    void setToZero();
    void setDoubledState(bool in_flag = false);
    void substract(int16_t in_start, int16_t in_stop);
    int16_t getDelta();
    bool isSourceDoubleWidth();
    bool areValid();
    bool areZeroed();
    bool hasDeltaWarning(uint8_t in_delta);
};

// Video "tracking" information.
class FrameBinDescriptor
{
public:
    uint32_t frame_id;          // Frame number.
    uint16_t lines_odd;         // Number of lines in the odd field with video data.
    uint16_t lines_even;        // Number of lines in the even field with video data.
    uint16_t lines_pcm_odd;     // Number of lines in the odd field, containing PCM data (has valid CRC or has markers).
    uint16_t lines_pcm_even;    // Number of lines in the even field, containing PCM data (has valid CRC or has markers).
    uint16_t lines_bad_odd;     // Number of lines in the odd field, containing PCM data with bad CRC (damaged data).
    uint16_t lines_bad_even;    // Number of lines in the even field, containing PCM data with bad CRC (damaged data).
    uint16_t lines_dup_odd;     // Number of lines in the odd field, containing duplicated lines.
    uint16_t lines_dup_even;    // Number of lines in the even field, containing duplicated lines.
    CoordinatePair data_coord;  // Average PCM data starting and ending pixels in source frame.
    uint32_t time_odd;          // Time (in us) spent on processing odd field.
    uint32_t time_even;         // Time (in us) spent on processing even field.

public:
    FrameBinDescriptor();
    FrameBinDescriptor(const FrameBinDescriptor &);
    FrameBinDescriptor& operator= (const FrameBinDescriptor &);
    void clear();
    uint16_t totalLines();
    uint16_t totalWithPCM();
    uint16_t totalWithBadCRCs();
    uint16_t totalDuplicated();
    uint32_t totalProcessTime();
};

// Frame assembling information.
class FrameAsmDescriptor
{
public:
    // Video standards (number of lines in the frame).
    enum
    {
        VID_UNKNOWN,        // Video standard is not detected.
        VID_PAL,            // Video standard detected to be PAL.
        VID_NTSC,           // Video standard detected to be NTSC.
        VID_MAX
    };

    // Field order for the frame.
    enum
    {
        ORDER_UNK,          // Unknown.
        ORDER_TFF,          // Top Field First (Odd Field First).
        ORDER_BFF,          // Bottom Field First (Even Field First).
        ORDER_MAX
    };

public:
    uint32_t frame_number;          // Frame number.
    uint16_t odd_std_lines;         // Number of lines by the standard in the odd field.
    uint16_t even_std_lines;        // Number of lines by the standard in the even field.
    uint16_t odd_data_lines;        // Number of lines with some PCM data in the odd field.
    uint16_t even_data_lines;       // Number of lines with some PCM data in the even field.
    uint16_t odd_valid_lines;       // Number of lines with valid CRC in the odd field.
    uint16_t even_valid_lines;      // Number of lines with valid CRC in the even field.
    uint16_t odd_top_data;          // # of line in the odd field that PCM data starts from.
    uint16_t odd_bottom_data;       // # of line in the odd field that PCM data ends on.
    uint16_t even_top_data;         // # of line in the even field that PCM data starts from.
    uint16_t even_bottom_data;      // # of line in the even field that PCM data ends on.
    uint16_t odd_sample_rate;       // Sample rate of audio data in the odd field.
    uint16_t even_sample_rate;      // Sample rate of audio data in the even field.
    uint8_t field_order;            // Field order of this frame (order to assemble the frame).
    bool odd_emphasis;              // Are audio samples processed with emphasis in the odd field?
    bool even_emphasis;             // Are audio samples processed with emphasis in the even field?
    uint8_t odd_ref;                // Average binarization reference level for lines in the odd field.
    uint8_t even_ref;               // Average binarization reference level for lines in the even field.
    uint16_t blocks_total;          // Total number of data blocks in the assembled frame.
    uint16_t blocks_drop;           // Number of data blocks that carry uncorrected errors.
    uint16_t samples_drop;          // Number of samples with damaged data.
    bool drawn;                     // Is data in this set already visualized?

private:
    bool order_preset;              // Is field order preset externally?
    bool order_guessed;             // Is field order not detected and is guessed (by stats or other)?

public:
    FrameAsmDescriptor();
    FrameAsmDescriptor(const FrameAsmDescriptor &);
    FrameAsmDescriptor& operator= (const FrameAsmDescriptor &);
    void clear();
    void clearMisc();
    void clearAsmStats();
    void presetOrderClear();
    void presetTFF();
    void presetBFF();
    void setOrderUnknown();
    void setOrderTFF();
    void setOrderBFF();
    void setOrderGuessed(bool);
    bool isOrderSet();
    bool isOrderPreset();
    bool isOrderGuessed();
    bool isOrderBFF();
    bool isOrderTFF();
    uint8_t getAvgRef();
};

// Frame assembling information for PCM-1.
class FrameAsmPCM1 : public FrameAsmDescriptor
{
public:
    uint16_t odd_top_padding;       // Number of lines to put before odd field to correctly assemble blocks.
    uint16_t odd_bottom_padding;    // Number of lines to put after odd field to correctly assemble blocks.
    uint16_t even_top_padding;      // Number of lines to put before even field to correctly assemble blocks.
    uint16_t even_bottom_padding;   // Number of lines to put after even field to correctly assemble blocks.
    uint16_t blocks_fix_bp;         // Number of data blocks that were fixed with help of Bit Picker.

public:
    FrameAsmPCM1();
    FrameAsmPCM1(const FrameAsmPCM1 &);
    FrameAsmPCM1& operator= (const FrameAsmPCM1 &);
    void clear();
    void clearMisc();
    void clearAsmStats();
};

// Frame assembling information for PCM-16x0.
class FrameAsmPCM16x0 : public FrameAsmDescriptor
{
public:
    uint16_t odd_top_padding;       // Number of lines to put before odd field to correctly assemble blocks.
    uint16_t odd_bottom_padding;    // Number of lines to put after odd field to correctly assemble blocks.
    uint16_t even_top_padding;      // Number of lines to put before even field to correctly assemble blocks.
    uint16_t even_bottom_padding;   // Number of lines to put after even field to correctly assemble blocks.
    bool silence;                   // Is reason for invalid padding - silence in audio?
    bool padding_ok;                // Is padding valid?
    bool ei_format;                 // Is data in EI format?
    uint16_t blocks_broken;         // Number of "BROKEN" data blocks, which have no CRC errors, but non-zero ECC syndromes.
    uint16_t blocks_fix_bp;         // Number of data blocks that were fixed with help of Bit Picker.
    uint16_t blocks_fix_p;          // Number of data blocks that were fixed with P-code.
    uint16_t blocks_fix_cwd;        // Number of data blocks that were fixed with help of CWD.

public:
    FrameAsmPCM16x0();
    FrameAsmPCM16x0(const FrameAsmPCM16x0 &);
    FrameAsmPCM16x0& operator= (const FrameAsmPCM16x0 &);
    void clear();
    void clearMisc();
    void clearAsmStats();
};

// Frame assembling information for STC-007.
class FrameAsmSTC007 : public FrameAsmDescriptor
{
public:
    uint8_t video_standard;         // Video standard for this frame (tells how many lines should be in a field).
    uint8_t tff_cnt;                // Probability of this frame to have TFF field order (# of successfull padding runs in TFF).
    uint8_t bff_cnt;                // Probability of this frame to have BFF field order (# of successfull padding runs in BFF).
    uint8_t odd_resolution;         // Audio resolution in odd field;
    uint8_t even_resolution;        // Audio resolution in even field;
    uint16_t inner_padding;         // Number of lines to put between fields of this frame to correctly assemble data blocks.
    uint16_t outer_padding;         // Number of lines to put between last field of this frame and first field of the next frame.
    bool trim_ok;                   // Is trim data complete?
    bool inner_padding_ok;          // Is inner padding valid? ([inner_padding] is set)
    bool outer_padding_ok;          // Is outer padding valid? ([outer_padding] is set)
    bool inner_silence;             // Is reason for invalid inner padding - silence in audio?
    bool outer_silence;             // Is reason for invalid outer padding - silence in audio?
    bool vid_std_preset;            // Is video standard preset by user?
    bool vid_std_guessed;           // Is video standard not detected and is guessed (by stats or other)?
    uint16_t blocks_broken_field;   // Number of "BROKEN" data blocks, which have no CRC errors, but non-zero ECC syndromes, in fields.
    uint16_t blocks_broken_seam;    // Number of "BROKEN" data blocks, which have no CRC errors, but non-zero ECC syndromes, in fields.
    uint16_t blocks_fix_p;          // Number of data blocks that were fixed with P-code.
    uint16_t blocks_fix_q;          // Number of data blocks that were fixed with Q-code.
    uint16_t blocks_fix_cwd;        // Number of data blocks that were fixed with help of CWD.

public:
    FrameAsmSTC007();
    FrameAsmSTC007(const FrameAsmSTC007 &);
    FrameAsmSTC007& operator= (const FrameAsmSTC007 &);
    void clear();
    void clearMisc();
    void clearAsmStats();
    void updateVidStdSoft(uint8_t in_std);
};

// Frame assembling attempt statistics.
class FieldStitchStats
{
public:
    uint16_t index;         // Index in the padding runs.
    uint16_t valid;         // How many valid blocks were assembled during the run.
    uint16_t silent;        // How many silent blocks were found during the run.
    uint16_t unchecked;     // How many unchecked blocks were found during the run.
    uint16_t broken;        // How many BROKEN blocks were found during the run.

public:
    FieldStitchStats();
    FieldStitchStats(const FieldStitchStats &);
    FieldStitchStats& operator= (const FieldStitchStats &);
    bool operator!= (const FieldStitchStats &);
    bool operator== (const FieldStitchStats &);
    bool operator< (const FieldStitchStats &);
    void clear();
};

#endif // FRAMETRIMSET_H
