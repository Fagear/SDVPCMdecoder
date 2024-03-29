﻿#include "config.h"
#include "mainwindow.h"

Q_DECLARE_METATYPE(uint8_t)
Q_DECLARE_METATYPE(uint16_t)
Q_DECLARE_METATYPE(uint32_t)
Q_DECLARE_METATYPE(uint64_t)
Q_DECLARE_METATYPE(int8_t)
Q_DECLARE_METATYPE(int16_t)
Q_DECLARE_METATYPE(int32_t)
Q_DECLARE_METATYPE(int64_t)
Q_DECLARE_METATYPE(VCapList)
Q_DECLARE_METATYPE(VideoLine)
Q_DECLARE_METATYPE(PCM1Line)
Q_DECLARE_METATYPE(PCM1SubLine)
Q_DECLARE_METATYPE(PCM16X0SubLine)
Q_DECLARE_METATYPE(STC007Line)
Q_DECLARE_METATYPE(PCM1DataBlock)
Q_DECLARE_METATYPE(PCM16X0DataBlock)
Q_DECLARE_METATYPE(STC007DataBlock)
Q_DECLARE_METATYPE(vid_preset_t)
Q_DECLARE_METATYPE(bin_preset_t)
Q_DECLARE_METATYPE(FrameBinDescriptor)
Q_DECLARE_METATYPE(FrameAsmDescriptor)
Q_DECLARE_METATYPE(FrameAsmPCM1)
Q_DECLARE_METATYPE(FrameAsmPCM16x0)
Q_DECLARE_METATYPE(FrameAsmSTC007)
Q_DECLARE_METATYPE(PCMSamplePair)

#ifdef _WIN32
    extern Q_CORE_EXPORT int qt_ntfs_permission_lookup;
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // Enable console window pop-up.
    if(AttachConsole(ATTACH_PARENT_PROCESS))
    //if(AttachConsole(ATTACH_PARENT_PROCESS)||AllocConsole())
    {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    qRegisterMetaType<uint8_t>("uint8_t");
    qRegisterMetaType<uint16_t>("uint16_t");
    qRegisterMetaType<uint32_t>("uint32_t");
    qRegisterMetaType<uint64_t>("uint64_t");
    qRegisterMetaType<int8_t>("int8_t");
    qRegisterMetaType<int16_t>("int16_t");
    qRegisterMetaType<int32_t>("int32_t");
    qRegisterMetaType<int64_t>("int64_t");
    qRegisterMetaType<VCapList>("VCapList");
    qRegisterMetaType<vid_preset_t>("vid_preset_t");
    qRegisterMetaType<bin_preset_t>("bin_preset_t");
    qRegisterMetaType<VideoLine>("VideoLine");
    qRegisterMetaType<PCM1Line>("PCM1Line");
    qRegisterMetaType<PCM1SubLine>("PCM1SubLine");
    qRegisterMetaType<PCM16X0SubLine>("PCM16X0SubLine");
    qRegisterMetaType<STC007Line>("STC007Line");
    qRegisterMetaType<PCM1DataBlock>("PCM1DataBlock");
    qRegisterMetaType<PCM16X0DataBlock>("PCM16X0DataBlock");
    qRegisterMetaType<STC007DataBlock>("STC007DataBlock");
    qRegisterMetaType<FrameBinDescriptor>("FrameBinDescriptor");
    qRegisterMetaType<FrameAsmDescriptor>("FrameAsmDescriptor");
    qRegisterMetaType<FrameAsmPCM1>("FrameAsmPCM1");
    qRegisterMetaType<FrameAsmPCM16x0>("FrameAsmPCM16x0");
    qRegisterMetaType<FrameAsmSTC007>("FrameAsmSTC007");
    qRegisterMetaType<PCMSamplePair>("PCMSamplePair");

    // Set misc. app stuff.
    QCoreApplication::setOrganizationName(APP_ORG_NAME);
    QCoreApplication::setOrganizationDomain(APP_ORG_HTTP);
    QCoreApplication::setApplicationName(APP_NAME);
    QCoreApplication::setApplicationVersion(APP_VERSION);

    // Print startup message.
    qInfo()<<"[M] Starting"<<APP_NAME<<"version"<<APP_VERSION<<"from"<<COMPILE_DATE<<COMPILE_TIME;
    QString log_line;
    // Dump Qt versions.
    log_line = QString::fromLocal8Bit(QT_VERSION_STR);
    qInfo()<<"[M] Compiled with Qt"<<log_line;
    log_line = QString::fromLocal8Bit(qVersion());
    qInfo()<<"[M] Qt run-time version"<<log_line;

#ifdef _WIN32
    qt_ntfs_permission_lookup++;
#endif

    QApplication a(argc, argv);
    //a.setStyle(QStyleFactory::create("Fusion"));
    qInfo()<<"[M] Available window styles:"<<QStyleFactory::keys();

    MainWindow w;
    w.show();

    return a.exec();
}
