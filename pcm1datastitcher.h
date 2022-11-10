#ifndef PCM1DATASTITCHER_H
#define PCM1DATASTITCHER_H

#include <stdint.h>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QString>
#include <vector>
#include "config.h"
#include "frametrimset.h"
#include "pcm1datablock.h"
#include "pcm1deinterleaver.h"
#include "pcm1line.h"
#include "pcm1subline.h"
#include "pcmsamplepair.h"

#ifndef QT_VERSION
    #undef DI_EN_DBG_OUT
#endif

#ifdef DI_EN_DBG_OUT
    //#define DI_LOG_BUF_WAIT_VERBOSE     1       // Produce verbose output for buffer filling process.
    //#define DI_LOG_BUF_FILL_VERBOSE     1       // Produce verbose output for buffer filling process.
    //#define DI_LOG_TRIM_VERBOSE         1       // Produce verbose output for trimming process.
#endif

class PCM1DataStitcher : public QObject
{
    Q_OBJECT

public:
    // Console logging options (can be used simultaneously).
    enum
    {
        LOG_SETTINGS = (1<<0),          // External operations with settings.
        LOG_PROCESS = (1<<1),           // General stage-by-stage logging.
        LOG_TRIM = (1<<2),              // Detecting frame trimming
        LOG_DEINTERLEAVE = (1<<3),      // Process of converting lines into data block.
        LOG_ERROR_CORR = (1<<4),        // Process of detecting and correcting errors.
        LOG_PADDING = (1<<5),           // Searching for padding.
        LOG_PADDING_LINE = (1<<6),      // Dump PCM lines while adjusting padding.
        LOG_PADDING_BLOCK = (1<<7),     // Show data blocks while adjusting padding.
        LOG_FIELD_ASSEMBLY = (1<<8),    // Show process of assembling new fields with padding.
        LOG_UNSAFE = (1<<9),            // Show process of marking unsafe data blocks.
        LOG_BLOCK_DUMP = (1<<10)        // Dump PCM data block after data block processing is complete.
    };

    // Frame parameters.
    enum
    {
        LINES_PF = (LINES_PER_NTSC_FIELD),      // PCM lines in one field of a frame for PCM-1 (NTSC video standard only).
        SUBLINES_PF = (LINES_PF*PCM1Line::SUBLINES_PER_LINE)    // Number of sub-lines per video field.
    };

    // Buffer limits.
    enum
    {
        BUF_SIZE_TRIM = (MAX_VLINE_QUEUE_SIZE),     // Maximum number of lines to store in [trim_buf] buffer.
        BUF_SIZE_FIELD = (SUBLINES_PF),             // Maximum number of sub-lines to store in per-field buffers.
        MIN_GOOD_LINES_PF = (LINES_PF*4/5)          // Minimum number of lines with good CRC per field to enable aggresive trimming.
    };

private:
    PCM1Deinterleaver lines_to_block;           // Deinterleaver object, used for final lines-block processing.
    FrameAsmPCM1 frasm_f1;                      // Frame assembling data for the current frame.
    QElapsedTimer file_time;                    // Timer for measuring file processing time.
    std::deque<PCM1Line> *in_lines;             // Input PCM line quene (shared).
    std::deque<PCMSamplePair> *out_samples;     // Output sample pairs queue (shared).
    QMutex *mtx_lines;                          // Mutex for input queue.
    QMutex *mtx_samples;                        // Mutex for output queue.
    std::vector<PCM1Line> trim_buf;             // Internal buffer for collecting a frame and detecting trimming.
    std::vector<PCM1SubLine> frame1_odd;        // Internal buffer for Frame odd field for detecting padding.
    std::vector<PCM1SubLine> frame1_even;       // Internal buffer for Frame even field for detecting padding.
    std::deque<PCM1SubLine> conv_queue;         // Output PCM lines buffer after trimming and padding, before converting into data blocks.
    std::string file_name;                      // Name of the file being processed (passed onto audio processing chain).
    uint8_t preset_field_order;                 // Field order, set externally.
    int8_t preset_odd_offset;                   // Odd line offset, set externally.
    int8_t preset_even_offset;                  // Even line offset, set externally.
    uint16_t log_level;                         // Level of debug output.
    uint16_t trim_fill;                         // Number of filled sub-lines in [trim_buf] from input queue.
    uint16_t f1_max_line;                       // Largest line number in Frame.
    bool ignore_CRC;                            // Ignore CRC from video lines or not (and force parity check).
    bool header_present;                        // Topmost line with header is detected in current frame.
    bool emphasis_set;                          // Current frame contains emphasis line flag.
    bool auto_offset;                           // Auto line offset, set externally.
    bool file_start;                            // Detected start of a new file, filename saved to [file_name].
    bool file_end;                              // Detected end of a file.
    bool finish_work;                           // Flag to break executing loop.

public:
    explicit PCM1DataStitcher(QObject *parent = 0);
    void setInputPointers(std::deque<PCM1Line> *in_pcmline = NULL, QMutex *mtx_pcmline = NULL);
    void setOutputPointers(std::deque<PCMSamplePair> *out_pcmsamples = NULL, QMutex *mtx_pcmsamples = NULL);

private:
    void resetState();
    bool waitForOneFrame();
    void fillUntilFullFrame();
    void findFrameTrim();
    void splitLineToSubline(PCM1Line *in_line = NULL, PCM1SubLine *out_sub = NULL, uint8_t part = PCM1SubLine::PART_LEFT);
    void splitFrameToFields();
    void findFramePadding();
    uint16_t getFirstFieldLineNum(uint8_t in_order);
    uint16_t getSecondFieldLineNum(uint8_t in_order);
    uint16_t addLinesFromField(std::vector<PCM1SubLine> *field_buf, uint16_t ind_start, uint16_t count, uint16_t *last_line_num = NULL);
    uint16_t addFieldPadding(uint32_t in_frame, uint16_t line_cnt, uint16_t *last_line_num = NULL);
    void fillFirstFieldForOutput();
    void fillSecondFieldForOutput();
    void setBlockSampleRate(PCM1DataBlock *in_block);
    void outputFileStart();
    void outputDataBlock(PCM1DataBlock *in_block = NULL);
    void outputFileStop();
    void performDeinterleave();

public slots:
    void setLogLevel(uint16_t);             // Set logging level.
    void setFieldOrder(uint8_t);            // Preset field order.
    void setAutoLineOffset(bool);           // Preset auto line offset.
    void setOddLineOffset(int8_t);          // Preset odd line offset from the top.
    void setEvenLineOffset(int8_t);         // Preset even line offset from the top.
    void setFineUseECC(bool);               // Set fine settings: usage of ECC on CRC-marked words.
    void setDefaultFineSettings();          // Set fine settings to defaults.
    void requestCurrentFineSettings();      // Get current fine settings.
    void doFrameReassemble();               // Main execution loop.
    void stop();                            // Set the flag to break execution loop and exit.

signals:
    void guiUpdFrameAsm(FrameAsmPCM1);      // New frame assembling data calculated in DI thread, need to update GUI.
    void guiUpdFineUseECC(bool);            // New fine setting: usage of ECC on CRC-marked words.
    void newLineProcessed(PCM1SubLine);     // Processed PCM-1 sub-line object (for visualization).
    void newBlockProcessed(PCM1DataBlock);  // Processed PCM-1 data block object (for visualization).
    void loopTime(quint64);                 // Useful loop time count.
    void finished();                        // Thread is stopped.
};

#endif // PCM1DATASTITCHER_H
