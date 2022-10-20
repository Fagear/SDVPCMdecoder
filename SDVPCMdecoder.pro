#-------------------------------------------------
#
# Project created by QtCreator 2020-04-06T04:15:14
#
#-------------------------------------------------

QT       += core gui winextras

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets multimedia concurrent

CONFIG += c++11

TARGET = SDVPCMdecoder
TEMPLATE = app

#LIBS += -L

#QMAKE_CXXFLAGS += -O3 -march=pentium3m
#QMAKE_CFLAGS += -O3 -march=pentium3m

#QMAKE_CXXFLAGS += -fopenmp
#QMAKE_LFLAGS += -fopenmp
QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -O3 -march=pentium3m

QMAKE_CFLAGS_RELEASE -= -O2
QMAKE_CFLAGS_RELEASE += -O3 -march=pentium3m

QMAKE_CXXFLAGS+= -D__STDC_CONSTANT_MACROS -fpermissive

VERSION = 0.99.4
win32: QMAKE_TARGET_COMPANY = Fagear
win32: QMAKE_TARGET_PRODUCT = SD video PCM decoder
win32: QMAKE_TARGET_DESCRIPTION = SD video to digital audio PCM decoder
win32: QMAKE_TARGET_COPYRIGHT = (c) Fagear
win32: RC_LANG = 0x0419

SOURCES += main.cpp\
        mainwindow.cpp \
    videoline.cpp \
    stc007line.cpp \
    stc007datablock.cpp \
    frametrimset.cpp \
    stc007deinterleaver.cpp \
    audioprocessor.cpp \
    stc007datastitcher.cpp \
    vin_ffmpeg.cpp \
    circbuffer.cpp \
    fine_bin_set.cpp \
    fine_deint_set.cpp \
    frame_vis.cpp \
    fine_vidin_set.cpp \
    pcm1line.cpp \
    bin_preset_t.cpp \
    pcmline.cpp \
    binarizer.cpp \
    pcmsamplepair.cpp \
    videotodigital.cpp \
    pcm16x0deinterleaver.cpp \
    pcm16x0datastitcher.cpp \
    pcm16x0datablock.cpp \
    renderpcm.cpp \
    pcm1deinterleaver.cpp \
    pcm1datablock.cpp \
    pcm1subline.cpp \
    pcm1datastitcher.cpp \
    about_wnd.cpp \
    vid_preset_t.cpp \
    pcmtester.cpp \
    pcm16x0subline.cpp \
    samples2audio.cpp \
    samples2wav.cpp \
    capt_sel.cpp \
    ffmpegwrapper.cpp

HEADERS  += mainwindow.h \
    videoline.h \
    stc007line.h \
    config.h \
    stc007datablock.h \
    frametrimset.h \
    stc007deinterleaver.h \
    audioprocessor.h \
    stc007datastitcher.h \
    vin_ffmpeg.h \
    circbuffer.h \
    fine_bin_set.h \
    fine_deint_set.h \
    frame_vis.h \
    fine_vidin_set.h \
    pcm1line.h \
    bin_preset_t.h \
    pcmline.h \
    binarizer.h \
    pcmsamplepair.h \
    videotodigital.h \
    pcm16x0deinterleaver.h \
    pcm16x0datastitcher.h \
    pcm16x0datablock.h \
    renderpcm.h \
    pcm1deinterleaver.h \
    pcm1datablock.h \
    pcm1subline.h \
    pcm1datastitcher.h \
    about_wnd.h \
    vid_preset_t.h \
    pcmtester.h \
    pcm16x0subline.h \
    samples2audio.h \
    samples2wav.h \
    capt_sel.h \
    ffmpegwrapper.h \
    lookup.h

FORMS    += mainwindow.ui \
    fine_bin_set.ui \
    fine_deint_set.ui \
    frame_vis.ui \
    fine_vidin_set.ui \
    about_wnd.ui \
    capt_sel.ui

RESOURCES += \
    icons.qrc \
    images.qrc

win32: RC_ICONS += pcm_ico.ico

TRANSLATIONS += SDVPCMdecoder_en.ts SDVPCMdecoder_pl.ts

CODECFORSRC = UTF-8

unix|win32: LIBS += -L$$PWD/lib/ -lavdevice -lavformat -lavcodec -lavutil -lswscale
