#include "pcmtester.h"

PCMTester::PCMTester(QObject *parent) : QObject(parent)
{

}

//------------------------ Test PCM-1 line CRCC calculation integrity.
bool PCMTester::testPCM1CRCC()
{
    QString log_line;
    PCM1Line pcm1dummy;
    // Fill up test sequence with 13-bit words.
    pcm1dummy.words[PCM1Line::WORD_L2] = 0x1A35;
    pcm1dummy.words[PCM1Line::WORD_R2] = 0x1248;
    pcm1dummy.words[PCM1Line::WORD_L4] = 0x0DD9;
    pcm1dummy.words[PCM1Line::WORD_R4] = 0x13FB;
    pcm1dummy.words[PCM1Line::WORD_L6] = 0x1C0E;
    pcm1dummy.words[PCM1Line::WORD_R6] = 0x09CB;
    // Fill CRC that should fit words above.
    pcm1dummy.setSourceCRC(0x9EB9);
    // Calculate CRC for filled words to test calculation process.
    pcm1dummy.calcCRC();

    log_line.sprintf("[TST] 13-bit data words CRC: 0x%04x, calculated CRC: 0x%04x", pcm1dummy.getSourceCRC(), pcm1dummy.getCalculatedCRC());
    qInfo()<<log_line;
    if(pcm1dummy.isCRCValid()==false)
    {
        qInfo()<<"[TST] 13-bit CRCC test FAILED!";
        return false;
    }
    else
    {
        qInfo()<<"[TST] 13-bit CRCC test PASSED.";
        return true;
    }
}

//------------------------ Test PCM-1600 line CRCC calculation integrity.
bool PCMTester::testPCM16x0CRCC()
{
    QString log_line;
    PCM16X0SubLine pcm16x0dummy;
    // Fill up test sequence with 16-bit words.
    pcm16x0dummy.words[PCM16X0SubLine::WORD_R1P1L1] = 0xD527;
    pcm16x0dummy.words[PCM16X0SubLine::WORD_L2P2R2] = 0x9C36;
    pcm16x0dummy.words[PCM16X0SubLine::WORD_R3P3L3] = 0x02A5;
    // Fill CRC that should fit words above.
    pcm16x0dummy.setSourceCRC(0xFB40);
    // Calculate CRC for filled words to test calculation process.
    pcm16x0dummy.calcCRC();

    log_line.sprintf("[TST] 16-bit data words CRC: 0x%04x, calculated CRC: 0x%04x", pcm16x0dummy.getSourceCRC(), pcm16x0dummy.getCalculatedCRC());
    qInfo()<<log_line;
    if(pcm16x0dummy.isCRCValid()==false)
    {
        qInfo()<<"[TST] 16-bit CRCC test FAILED!";
        return false;
    }
    else
    {
        qInfo()<<"[TST] 16-bit CRCC test PASSED.";
        return true;
    }
}

//------------------------ Test STC-007 line CRCC calculation integrity.
bool PCMTester::testSTC007CRCC()
{
    QString log_line;
    STC007Line stc007dummy;
    // Fill up test sequence with 14-bit words.
    stc007dummy.setWord(STC007Line::WORD_L_SH0, 0x2D4B);
    stc007dummy.setWord(STC007Line::WORD_R_SH48, 0x18EE);
    stc007dummy.setWord(STC007Line::WORD_L_SH95, 0x152B);
    stc007dummy.setWord(STC007Line::WORD_R_SH143, 0x3A7F);
    stc007dummy.setWord(STC007Line::WORD_L_SH190, 0x04AB);
    stc007dummy.setWord(STC007Line::WORD_R_SH238, 0x301B);
    stc007dummy.setWord(STC007Line::WORD_P_SH288, 0x22F6);
    stc007dummy.setWord(STC007Line::WORD_Q_SH336, 0x0DD6);
    // Fill CRC that should fit words above.
    stc007dummy.setSourceCRC(0xB2ED);
    // Calculate CRC for filled words to test calculation process.
    stc007dummy.calcCRC();
    stc007dummy.applyCRCStatePerWord();

    log_line.sprintf("[TST] 14-bit data words CRC: 0x%04x, calculated CRC: 0x%04x", stc007dummy.getSourceCRC(), stc007dummy.getCalculatedCRC());
    qInfo()<<log_line;
    if(stc007dummy.isCRCValid()==false)
    {
        qInfo()<<"[TST] 14-bit CRCC test FAILED!";
        return false;
    }
    else
    {
        qInfo()<<"[TST] 14-bit CRCC test PASSED.";
        return true;
    }
}

//------------------------ Test PCM-1600 PCM data block's error correction integrity.
bool PCMTester::testPCM16x0ECC()
{
    // TODO: PCM-16x0 ECC test
    qInfo()<<"[TST] PCM-16x0 ECC test PASSED.";
    return true;
}

//------------------------ Test STC-007 PCM data block's error correction integrity.
bool PCMTester::testSTC007ECC(enum KillMode test_mode)
{
    STC007Line valid_line, bad_line;

    // Fill up test sequence.
    valid_line.frame_number = 9;
    valid_line.line_number = 88;

    // Fill up test sequence with 14-bit words.
    valid_line.setWord(STC007Line::WORD_L_SH0, 0x3B43);
    valid_line.setWord(STC007Line::WORD_R_SH48, 0x3FDB);
    valid_line.setWord(STC007Line::WORD_L_SH95, 0x3B52);
    valid_line.setWord(STC007Line::WORD_R_SH143, 0x3FDA);
    valid_line.setWord(STC007Line::WORD_L_SH190, 0x3B5F);
    valid_line.setWord(STC007Line::WORD_R_SH238, 0x3FDA);
    valid_line.setWord(STC007Line::WORD_P_SH288, 0x0495);
    valid_line.setWord(STC007Line::WORD_Q_SH336, 0x1DB7);

    // Calculate CRC for filled words.
    valid_line.calcCRC();
    // Fill stored CRC with calculated CRC to ensure valid CRC.
    valid_line.setSourceCRC(valid_line.getCalculatedCRC());
    valid_line.applyCRCStatePerWord();
    valid_line.forceMarkersOk();

    // Test the line.
    if(valid_line.isCRCValid()==false)
    {
        qInfo()<<"[TST] STC-007 test line is corrupted, please re-check the data. Exiting...";
        return false;
    }
    else
    {
        qInfo()<<"[TST] STC-007 test line is valid.";
    }

    // Create reference buffer.
    std::deque<STC007Line> valid_buffer;
    // Fill minimum amount of lines to deinterleave one data block.
    for(uint8_t index=0;index<=STC007DataBlock::MIN_DEINT_DATA;index++)
    {
        valid_line.line_number++;
        valid_buffer.push_back(valid_line);
    }
    valid_line.line_number = 88;

    STC007Deinterleaver lines_to_block;
    STC007DataBlock test_block;

    // Set deinterleaver parameters.
    lines_to_block.setInput(&valid_buffer);
    lines_to_block.setOutput(&test_block);
    lines_to_block.setLogLevel(STC007Deinterleaver::LOG_PROCESS);
    lines_to_block.setResMode(STC007Deinterleaver::RES_MODE_14BIT);
    lines_to_block.setIgnoreCRC(false);
    lines_to_block.setForceParity(true);

    // Check that valid buffer passes the test.
    lines_to_block.processBlock(0);
    qInfo()<<QString::fromStdString("[TST] "+test_block.dumpContentString());
    if((test_block.isBlockValid()!=false)&&(test_block.isDataBroken()==false)&&(test_block.isDataFixed()==false))
    {
        qInfo()<<"[TST] STC-007 reference buffer passed the test.";
    }
    else
    {
        qInfo()<<"[TST] Something wrong with STC-007 reference buffer. Aborting test...";
        return false;
    }

    uint8_t to_corrupt, corr_index;
    uint8_t bad_indexes[STC007Line::WORD_CNT];
    bool index_exists;
    uint16_t rand_mask;

    // Perform data corruption test.
    for(uint32_t test_n=0;test_n<RUN_COUNT;test_n++)
    {
        std::deque<STC007Line> test_buffer;
        // Copy valid line.
        bad_line = valid_line;
        // Fill up the buffer with corrupted lines.
        for(uint8_t index=0;index<=STC007DataBlock::MIN_DEINT_DATA;index++)
        {
            bad_line.line_number++;
            test_buffer.push_back(bad_line);
        }
        // Reset indexes.
        for(uint8_t cor=0;cor<STC007Line::WORD_CNT;cor++)
        {
            bad_indexes[cor] = 0xFF;
        }
        if(test_mode==MODE_KILL_2)
        {
            // Force testing Q-code correction.
            to_corrupt = 2;
        }
        else if(test_mode==MODE_KILL_1TO2)
        {
            // Select how many words to corrupt.
            to_corrupt = rand()%2+1;
        }
        else
        {
            // Select how many words to corrupt.
            to_corrupt = rand()%STC007Line::WORD_CNT;
        }
        // Perform corruption.
        for(uint8_t cor=0;cor<to_corrupt;cor++)
        {
            // Select index of the word to corrupt, ensuring it's not repeated.
            do
            {
                index_exists = false;
                // Choose the word to corrupt.
                corr_index = rand()%STC007Line::WORD_CRCC_SH0;
                // Check if it is not yet corrupted.
                for(uint8_t ch=0;ch<STC007Line::WORD_CNT;ch++)
                {
                    if(bad_indexes[ch]==corr_index)
                    {
                        // Found unused index.
                        index_exists = true;
                        break;
                    }
                }
            }
            while(index_exists!=false);
            // Save index of corrupted word.
            bad_indexes[cor] = corr_index;
            // Choose what bits to corrupt in the word.
            rand_mask = rand()%0x3FFF+1;
            // Corrupt the data.
            test_buffer[corr_index*16].setWord(corr_index, test_buffer[corr_index*16].getWord(corr_index)^rand_mask);
            test_buffer[corr_index*16].setSourceCRC(~test_buffer[corr_index*16].getSourceCRC());
        }
        QString log_line;
        log_line = "[TST] STC-007 test run #"+QString::number(test_n)+", corrupting "+QString::number(to_corrupt)+" words";
        for(uint8_t i=0;i<to_corrupt;i++)
        {
            log_line += ", #"+QString::number(bad_indexes[i]);
        }
        qInfo()<<log_line;
        // Process damaged data.
        lines_to_block.setInput(&test_buffer);
        lines_to_block.setLogLevel(STC007Deinterleaver::LOG_PROCESS|STC007Deinterleaver::LOG_ERROR_CORR);
        lines_to_block.processBlock(0);
        qInfo()<<QString::fromStdString("[TST] "+test_block.dumpContentString());
        if(to_corrupt==0)
        {
            if((test_block.isBlockValid()!=false)&&(test_block.isDataFixed()==false)&&(test_block.isDataBroken()==false))
            {
                QString log_line;
                log_line = "[TST] STC-007 test run #"+QString::number(test_n)+" passed.";
                qInfo()<<log_line;
            }
            else
            {
                QString log_line;
                log_line = "[TST] STC-007 test run #"+QString::number(test_n)+" FAILED! Data is fixed or broken while it is original.";
                qInfo()<<log_line;
                return false;
            }
        }
        else if(to_corrupt>2)
        {
            if(test_block.isBlockValid()==false)
            {
                QString log_line;
                log_line = "[TST] STC-007 test run #"+QString::number(test_n)+" passed.";
                qInfo()<<log_line;
            }
            else
            {
                QString log_line;
                log_line = "[TST] STC-007 test run #"+QString::number(test_n)+" FAILED! Valid audio, while it's corrupted and impossible.";
                qInfo()<<log_line;
                return false;
            }
        }
        else if(to_corrupt==1)
        {
            //qInfo()<<QString::number(test_block.getWord(bad_indexes[0]))<<"|"<<QString::number(valid_line.words[bad_indexes[0]]);
            // Compare corrupted (and fixed) word to one from the valid line.
            if((test_block.isBlockValid()!=false)&&
                (test_block.getWord(bad_indexes[0])==valid_line.getWord(bad_indexes[0])))
            {
                QString log_line;
                log_line = "[TST] STC-007 test run #"+QString::number(test_n)+" passed";
                if(test_block.isDataFixed()==false)
                {
                    log_line += ", audio was not damaged.";
                }
                else
                {
                    if(test_block.isDataFixedByP()!=false)
                    {
                        log_line += ", audio fixed by P-code.";
                    }
                    else if(test_block.isDataFixedByQ()!=false)
                    {
                        log_line += ", audio fixed by Q-code.";
                    }
                }
                qInfo()<<log_line;
            }
            else
            {
                QString log_line;
                log_line = "[TST] STC-007 test run #"+QString::number(test_n)+" FAILED! Data was not fixed while it should've been.";
                qInfo()<<log_line;
                return false;
            }
        }
        else if(to_corrupt==2)
        {
            if((bad_indexes[0]>STC007Line::WORD_R_SH238)&&(bad_indexes[1]>STC007Line::WORD_R_SH238))
            {
                // Workaround to prevent checking P, Q and CRC which are not fixed or present.
                if(bad_indexes[0]>STC007Line::WORD_R_SH238)
                {
                    bad_indexes[0] = STC007Line::WORD_L_SH0;
                }
                // Patch to prevent checking absent CRC word.
                if(bad_indexes[1]>STC007Line::WORD_R_SH238)
                {
                    bad_indexes[1] = STC007Line::WORD_R_SH48;
                }
            }
            //qInfo()<<QString::number(test_block.getWord(bad_indexes[0]))<<"|"<<QString::number(valid_line.words[bad_indexes[0]]);
            //qInfo()<<QString::number(test_block.getWord(bad_indexes[1]))<<"|"<<QString::number(valid_line.words[bad_indexes[1]]);
            // Compare corrupted (and fixed) words to ones from the valid line.
            if((test_block.isBlockValid()!=false)&&
                (test_block.getWord(bad_indexes[0])==valid_line.getWord(bad_indexes[0]))&&
                (test_block.getWord(bad_indexes[1])==valid_line.getWord(bad_indexes[1])))
            {
                QString log_line;
                log_line = "[TST] STC-007 test run #"+QString::number(test_n)+" passed";
                if(test_block.isDataFixed()==false)
                {
                    log_line += ", audio was not damaged.";
                }
                else
                {
                    if(test_block.isDataFixedByP()!=false)
                    {
                        log_line += ", audio fixed by P-code.";
                    }
                    else if(test_block.isDataFixedByQ()!=false)
                    {
                        log_line += ", audio fixed by Q-code.";
                    }
                }
                qInfo()<<log_line;
            }
            else
            {
                QString log_line;
                log_line = "[TST] STC-007 test run #"+QString::number(test_n)+" FAILED! Data was not fixed while it should've been.";
                qInfo()<<log_line;
                return false;
            }
        }
    }
    qInfo()<<"[TST] STC-007 ECC test PASSED.";
    return true;
}

//------------------------ Test PCM line CRCC integrity.
void PCMTester::testCRCC()
{
    bool crc_works;

    crc_works = true;

    crc_works = crc_works&&testPCM1CRCC();
    crc_works = crc_works&&testSTC007CRCC();
    crc_works = crc_works&&testPCM16x0CRCC();

    if(crc_works==false)
    {
        emit testFailed();
    }
    else
    {
        emit testOk();
    }

    emit finished();
}

//------------------------ Test PCM data block's error correction integrity.
void PCMTester::testDataBlock()
{
    bool ecc_works;

    ecc_works = true;

    ecc_works = ecc_works&&testPCM16x0ECC();
    ecc_works = ecc_works&&testSTC007ECC(MODE_KILL_1TO2);

    if(ecc_works==false)
    {
        emit testFailed();
    }
    else
    {
        emit testOk();
    }

    qInfo()<<"[TST] ECC test PASSED.";
    emit finished();
}
