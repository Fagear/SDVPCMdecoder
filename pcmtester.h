/**************************************************************************************************************************************************************
pcmtester.h

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

Created: 2020-05

Internal test suite module.
It performs tests of CRC and ECC calculations for each PCM format.

**************************************************************************************************************************************************************/

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

signals:
    void testOk();
    void testFailed();
    void finished();
};

#endif // PCMTESTER_H
