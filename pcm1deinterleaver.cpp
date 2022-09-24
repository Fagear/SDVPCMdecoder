#include "pcm1deinterleaver.h"

PCM1Deinterleaver::PCM1Deinterleaver()
{
    input_vector = NULL;
    input_deque = NULL;
    out_data_block = NULL;
    log_level = 0;
    ignore_crc = false;
}

//------------------------ Set debug logging level (LOG_PROCESS, etc...).
void PCM1Deinterleaver::setLogLevel(uint8_t in_level)
{
    log_level = in_level;
}

//------------------------ Set input deque pointer.
void PCM1Deinterleaver::setInput(std::deque<PCM1SubLine> *in_line_buffer)
{
    input_deque = in_line_buffer;
    if(input_deque!=NULL)
    {
        // Remove pointer to vector.
        input_vector = NULL;
    }
}

//------------------------ Set input vector pointer.
void PCM1Deinterleaver::setInput(std::vector<PCM1SubLine> *in_line_buffer)
{
    input_vector = in_line_buffer;
    if(input_vector!=NULL)
    {
        // Remove pointer to deque.
        input_deque = NULL;
    }
}

//------------------------ Set output data block.
void PCM1Deinterleaver::setOutput(PCM1DataBlock *out_data)
{
    out_data_block = out_data;
}

//------------------------ Enable/disable ignoring CRC in video lines.
void PCM1Deinterleaver::setIgnoreCRC(bool flag)
{
#ifdef DI_EN_DBG_OUT
    if(ignore_crc!=flag)
    {
        if((log_level&LOG_SETTINGS)!=0)
        {
            if(flag==false)
            {
                qInfo()<<"[DI-1] CRC discarding set to 'disabled'.";
            }
            else
            {
                qInfo()<<"[DI-1] CRC discarding set to 'enabled'.";
            }
        }
    }
#endif
    ignore_crc = flag;
}

//------------------------ Take buffer of binarized PCM lines, perform deinterleaving.
uint8_t PCM1Deinterleaver::processBlock(uint16_t itl_block_num, uint16_t line_sh)
{
    bool use_vector, suppress_log;

    use_vector = false;

    // Check pointers.
    if(out_data_block==NULL)
    {
#ifdef DI_EN_DBG_OUT
        qWarning()<<DBG_ANCHOR<<"[DI-1] Null input pointer for PCM-1 data block provided in [PCM1Deinterleaver::processBlock()], exiting...";
#endif
        return DI_RET_NULL_BLOCK;
    }
    if(input_deque==NULL)
    {
        if(input_vector==NULL)
        {
#ifdef DI_EN_DBG_OUT
            qWarning()<<DBG_ANCHOR<<"[DI-1] Null output pointer for PCM-1 line buffer provided in [PCM1Deinterleaver::processBlock()], exiting...";
#endif
            return DI_RET_NULL_LINES;
        }
        else
        {
            use_vector = true;
            // Check if there is enough data to process.
            if((input_vector->size())<(size_t)(PCM1DataBlock::MIN_DEINT_DATA+line_sh))
            {
#ifdef DI_EN_DBG_OUT
                if((log_level&LOG_PROCESS)!=0)
                {
                    qInfo()<<"[DI-1] Not enough PCM-1 lines in buffer:"<<input_vector->size()<<", required:"<<(PCM1DataBlock::MIN_DEINT_DATA+line_sh);
                }
#endif
                return DI_RET_NO_DATA;
            }
        }
    }
    else
    {
        // Check if there is enough data to process.
        if((input_deque->size())<(size_t)(PCM1DataBlock::MIN_DEINT_DATA+line_sh))
        {
#ifdef DI_EN_DBG_OUT
            if((log_level&LOG_PROCESS)!=0)
            {
                qInfo()<<"[DI-1] Not enough PCM-1 lines in buffer:"<<input_deque->size()<<", required:"<<(PCM1DataBlock::MIN_DEINT_DATA+line_sh);
            }
#endif
            return DI_RET_NO_DATA;
        }
    }

    // Measure processing time for debuging.
#ifdef QT_VERSION
    QElapsedTimer time_per_block;
    time_per_block.start();
#endif

    suppress_log = !(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_ERROR_CORR)!=0));

#ifdef DI_EN_DBG_OUT
    if(suppress_log==false)
    {
        qInfo()<<"[DI-1] -------------------- Filling data in 13-bit mode";
    }
#endif

    // Save interleave number.
    out_data_block->interleave_num = itl_block_num;

    // Fill even samples L2-R4-L4-R4-L6-R6...
    setWordData(itl_block_num, true, use_vector);
    // Fill odd samples L1-R1-L3-R3-L5-R5...
    setWordData(itl_block_num, false, use_vector);

    return DI_RET_OK;
}

//---------------------- Set data and CRC state in the data block.
void PCM1Deinterleaver::setWordData(uint16_t itl_block_num, bool even_stripe, bool use_vector, uint16_t line_sh)
{
    bool suppress_log, even_itl_block, subline_crc_ok;
    uint16_t itl_block_ofs, stripe_len, stripe_one_ofs, stripe_two_ofs, subline_ofs, word_ofs;
    PCM1SubLine tmp_subline;

    suppress_log = !(((log_level&LOG_PROCESS)!=0)||((log_level&LOG_ERROR_CORR)!=0));

    // Check if this is even numbered block.
    even_itl_block = (itl_block_num%2==0);
    // Calculate interleave block offset.
    itl_block_ofs = itl_block_num*(2*PCM1DataBlock::WORDP_STRIPE_LEN);
    // Check if this is the last interleave block.
    if(itl_block_num!=(PCM1DataBlock::INT_BLK_PER_FIELD-1))
    {
        // Not the last interleave block in the field.
        // Set normal stripe length.
        stripe_len = PCM1DataBlock::WORDP_STRIPE_LEN;
        // Set normal data block length.
        out_data_block->setNormalLength();
    }
    else
    {
        // The last interleave block in the field.
        if(even_stripe==false)
        {
            // Odd stripe (first) in the block.
            // Set normal stripe length.
            stripe_len = PCM1DataBlock::WORDP_STRIPE_LEN;
        }
        else
        {
            // Even (last) stripe in the block.
            // Set short stripe length.
            stripe_len = PCM1DataBlock::WORDP_STRIPE_SHORT;
        }
        // Set short data block length.
        out_data_block->setShortLength();
    }

    stripe_one_ofs = itl_block_ofs+PCM1DataBlock::WORDP_STRIPE_ONE_OFS+line_sh;
    stripe_two_ofs = itl_block_ofs+PCM1DataBlock::WORDP_STRIPE_TWO_OFS+line_sh;
    // Set starting line in the data block.
    if(use_vector==false)
    {
        tmp_subline = (*input_deque)[stripe_one_ofs];
    }
    else
    {
        tmp_subline = (*input_vector)[stripe_one_ofs];
    }
    out_data_block->frame_number = tmp_subline.frame_number;
    out_data_block->start_line = tmp_subline.line_number;
    // Set ending line in the data block.
    if(use_vector==false)
    {
        tmp_subline = (*input_deque)[stripe_two_ofs+stripe_len-1];
    }
    else
    {
        tmp_subline = (*input_vector)[stripe_two_ofs+stripe_len-1];
    }
    out_data_block->stop_line = tmp_subline.line_number;

    // Reset word offset.
    if(even_stripe==false)
    {
        word_ofs = 0;
    }
    else
    {
        word_ofs = PCM1DataBlock::WORD_NEXT_OFS;
    }

    // Fill up one stripe of samples.
    for(uint16_t word_pair_ofs=0;word_pair_ofs<stripe_len;word_pair_ofs++)
    {
        // Check for even/odd interleave block and even/odd stripe in the block to calculate interleave offset.
        if(even_itl_block==even_stripe)
        {
            // Both interleave block number and stripe are even or odd.
            subline_ofs = stripe_one_ofs;
        }
        else
        {
            // Interleave block number and stripe are not the same.
            subline_ofs = stripe_two_ofs;
        }
        subline_ofs += (word_pair_ofs+line_sh);
        // Take sub-line with samples.
        if(use_vector==false)
        {
            tmp_subline = (*input_deque)[subline_ofs];
        }
        else
        {
            tmp_subline = (*input_vector)[subline_ofs];
        }
        // Check if CRC should be ignored.
        if(ignore_crc==false)
        {
            // Take CRC state from the sub-line.
            subline_crc_ok = tmp_subline.isCRCValid();
        }
        else
        {
            // Always assume that CRC is ok.
            subline_crc_ok  = true;
        }
#ifdef DI_EN_DBG_OUT
        if(suppress_log==false)
        {
            qInfo()<<"iBlock:"<<itl_block_num<<", pair offset:"<<word_pair_ofs<<", sub-line:"<<subline_ofs<<", L/R"<<((word_ofs+2)/2)+(PCM1DataBlock::WORDP_STRIPE_LEN*2*itl_block_num)<<", data:"<<QString::fromStdString(tmp_subline.dumpContentString());
        }
#endif
        // Copy sample for the left channel for the sub-line.
        out_data_block->setWord(word_ofs, tmp_subline.getLeft(), subline_crc_ok, tmp_subline.hasPickedLeft(), tmp_subline.hasPickedRight());
        word_ofs++;
        // Copy sample for the right channel for the sub-line.
        out_data_block->setWord(word_ofs, tmp_subline.getRight(), subline_crc_ok, false, tmp_subline.hasPickedRight());
        word_ofs++;
        // Go to the next sample pair in the data block.
        word_ofs += PCM1DataBlock::WORD_NEXT_OFS;
    }
}
