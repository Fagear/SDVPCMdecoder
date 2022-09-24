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

class PCMTester : public QObject
{
    Q_OBJECT

    enum KillMode
    {
        MODE_KILL_ANY,
        MODE_KILL_1TO2,
        MODE_KILL_2
    };

    enum
    {
        RUN_COUNT = 4096
    };

public:
    explicit PCMTester(QObject *parent = 0);

private:
    bool testPCM1CRCC();
    bool testPCM16x0CRCC();
    bool testSTC007CRCC();
    bool testPCM16x0ECC();
    bool testSTC007ECC(enum KillMode test_mode = MODE_KILL_2);

public slots:
    void testCRCC();
    void testDataBlock();

signals:
    void testOk();
    void testFailed();
    void finished();
};

#endif // PCMTESTER_H
