#ifndef PCMTESTER_H
#define PCMTESTER_H

#include <cstdlib>
#include <ctime>
#include <deque>
#include <stdint.h>
#include <QObject>
#include "binarizer.h"
#include "pcm16x0subline.h"
#include "pcm1line.h"
#include "stc007datablock.h"
#include "stc007deinterleaver.h"
#include "stc007line.h"

//------------------------ Class for testing various functions of PCM decoder.
class PCMTester : public QObject
{
    Q_OBJECT

    enum KillMode
    {
        MODE_KILL_ANY,      // Corrupt any count from 0 to data block word count.
        MODE_KILL_1,        // Always corrupt 1 word
        MODE_KILL_1TO2,     // Corrupt 1 or 2 words
        MODE_KILL_2         // Always corrupt 2 words
    };

    enum
    {
        RUN_COUNT = 2048
    };

public:
    explicit PCMTester(QObject *parent = 0);

private:
    bool testPCM1CRCC();
    bool testPCM16x0CRCC();
    bool testSTC007CRCC();
    bool testPCM16x0ECC();
    bool testSTC007ECC(enum KillMode test_mode = MODE_KILL_1TO2);

public slots:
    void testCRCC();
    void testDataBlock();
    void testHamming();

signals:
    void testOk();
    void testFailed();
    void finished();
};

#endif // PCMTESTER_H
