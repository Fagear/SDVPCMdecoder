﻿#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    VIN_worker = NULL;
    V2D_worker = NULL;
    L2B_PCM1_worker = NULL;
    L2B_PCM16X0_worker = NULL;
    L2B_STC007_worker = NULL;
    AP_worker = NULL;
    visuSource = NULL;
    visuBin = NULL;
    visuAssembled = NULL;
    visuBlocks = NULL;

    shutdown_started = false;
    inhibit_setting_save = false;
    src_draw_deint = false;

    log_level = 0;
    v_decoder_state = VDEC_IDLE;

    set_pcm_type = LIST_TYPE_PCM1;
    pcm1_ofs_diff = 0;

    wl_pcm1_help_idx = wl_pcm16x0_help_idx = wl_stc007_help_idx = 0;

    clearStat();

    ui->lblVersion->setText("v"+QString(APP_VERSION)+" ("+QString(COMPILE_DATE)+")");

    // Disable maximize button.
    setWindowFlags(Qt::Window|Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint);

#ifdef _WIN32
    // Turn debug off if no console detected.
    /*if(GetConsoleWindow()==NULL)
    {
        ui->mnGeneral->setEnabled(false);
        ui->mnVIP->setEnabled(false);
        ui->mnLB->setEnabled(false);
        ui->mnDI->setEnabled(false);
        ui->mnAP->setEnabled(false);
        ui->actDBGOff->setEnabled(false);
    }*/
#endif

    // Window position/size update.
    timResizeUpd.setSingleShot(true);
    timResizeUpd.setInterval(TIM_WIN_POS_INT);
    connect(&timResizeUpd, SIGNAL(timeout()), this, SLOT(updateWindowPosition()));

    // GUI update.
    timUIUpdate.setSingleShot(false);
    timUIUpdate.setInterval(TIM_GUI_INT);
    connect(&timUIUpdate, SIGNAL(timeout()), this, SLOT(updateGUIByTimer()));

    qInfo()<<"[M] GUI thread:"<<this->thread()<<"ID"<<QString::number((uint)QThread::currentThreadId())<<", starting processing threads...";

    // Initialize settings storage.
    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    qInfo()<<"[M] Settings path:"<<settings_hdl.fileName();

    this->setWindowTitle(APP_NAME_LONG);

    // Create pallettes.
    QBrush brs_redlabel(QColor(255, 60, 45, 255));
    brs_redlabel.setStyle(Qt::SolidPattern);
    QBrush brs_greenlabel(QColor(60, 220, 128, 255));
    brs_greenlabel.setStyle(Qt::SolidPattern);
    QBrush brs_yellowlabel(QColor(255, 255, 65, 255));
    brs_yellowlabel.setStyle(Qt::SolidPattern);
    QBrush brs_whitetext(QColor(255, 255, 255, 255));
    QBrush brs_blacktext(QColor(0, 0, 0, 255));
    // Red.
    plt_redlabel.setBrush(QPalette::Active, QPalette::Window, brs_redlabel);
    plt_redlabel.setBrush(QPalette::Active, QPalette::WindowText, brs_whitetext);
    plt_redlabel.setBrush(QPalette::Inactive, QPalette::Window, brs_redlabel);
    plt_redlabel.setBrush(QPalette::Inactive, QPalette::WindowText, brs_whitetext);
    // Green.
    plt_greenlabel.setBrush(QPalette::Active, QPalette::Window, brs_greenlabel);
    plt_greenlabel.setBrush(QPalette::Active, QPalette::WindowText, brs_blacktext);
    plt_greenlabel.setBrush(QPalette::Inactive, QPalette::Window, brs_greenlabel);
    plt_greenlabel.setBrush(QPalette::Inactive, QPalette::WindowText, brs_blacktext);
    // Yellow.
    plt_yellowlabel.setBrush(QPalette::Active, QPalette::Window, brs_yellowlabel);
    plt_yellowlabel.setBrush(QPalette::Active, QPalette::WindowText, brs_blacktext);
    plt_yellowlabel.setBrush(QPalette::Inactive, QPalette::Window, brs_yellowlabel);
    plt_yellowlabel.setBrush(QPalette::Inactive, QPalette::WindowText, brs_blacktext);

    ui->indLivePB->setPalette(plt_redlabel);

    // Create and link video input processor worker.
    input_FPU = NULL;
    input_FPU = new QThread;
    VIN_worker = new VideoInFFMPEG;
    VIN_worker->setOutputPointers(&video_lines, &vl_lock);
    VIN_worker->moveToThread(input_FPU);
    connect(input_FPU, SIGNAL(started()), VIN_worker, SLOT(runFrameDecode()));
    connect(VIN_worker, SIGNAL(finished()), input_FPU, SLOT(quit()));
    connect(VIN_worker, SIGNAL(finished()), VIN_worker, SLOT(deleteLater()));
    connect(input_FPU, SIGNAL(finished()), this, SLOT(exitAction()));
    connect(input_FPU, SIGNAL(finished()), input_FPU, SLOT(deleteLater()));
    connect(this, SIGNAL(aboutToExit()), VIN_worker, SLOT(stop()));
    connect(this, SIGNAL(newVIPLogLevel(uint8_t)), VIN_worker, SLOT(setLogLevel(uint8_t)));
    connect(this, SIGNAL(newTargetPath(QString)), VIN_worker, SLOT(setSourceLocation(QString)));
    connect(this, SIGNAL(newStepPlay(bool)), VIN_worker, SLOT(setStepPlay(bool)));
    connect(this, SIGNAL(newFrameDropDetection(bool)), VIN_worker, SLOT(setDropDetect(bool)));
    connect(this, SIGNAL(doPlayUnload()), VIN_worker, SLOT(mediaUnload()));
    connect(this, SIGNAL(doPlayStart()), VIN_worker, SLOT(mediaPlay()));
    connect(this, SIGNAL(doPlayPause()), VIN_worker, SLOT(mediaPause()));
    connect(this, SIGNAL(doPlayStop()), VIN_worker, SLOT(mediaStop()));
    connect(VIN_worker, SIGNAL(mediaNotFound()), this, SLOT(playerNoSource()));
    connect(VIN_worker, SIGNAL(mediaLoaded(QString)), this, SLOT(playerLoaded(QString)));
    connect(VIN_worker, SIGNAL(mediaPlaying(uint32_t)), this, SLOT(playerStarted(uint32_t)));
    connect(VIN_worker, SIGNAL(mediaPaused()), this, SLOT(playerPaused()));
    connect(VIN_worker, SIGNAL(mediaStopped()), this, SLOT(playerStopped()));
    connect(VIN_worker, SIGNAL(mediaError(QString)), this, SLOT(playerError(QString)));
    connect(VIN_worker, SIGNAL(frameDropDetected()), this, SLOT(updateStatsDroppedFrame()));
    connect(VIN_worker, SIGNAL(frameDecoded(uint32_t)), this, SLOT(updateStatsVIPFrame(uint32_t)));
    // Start new thread with video input processor.
    input_FPU->start(QThread::HighPriority);

    connect(ui->mnLanguage, SIGNAL(triggered(QAction*)), this, SLOT(setLang(QAction*)));

    connect(ui->btnOpen, SIGNAL(clicked(bool)), this, SLOT(loadVideo()));
    connect(ui->actCloseSource, SIGNAL(triggered(bool)), this, SLOT(unloadSource()));
    connect(ui->btnPlay, SIGNAL(clicked(bool)), this, SLOT(usrPlayStop()));
    connect(ui->btnPause, SIGNAL(clicked(bool)), this, SLOT(usrPause()));

    connect(ui->cbxLivePB, SIGNAL(clicked(bool)), this, SLOT(updateGUISettings()));
    connect(ui->cbxWaveSave, SIGNAL(clicked(bool)), this, SLOT(updateGUISettings()));
    connect(ui->cbxFrameDropout, SIGNAL(clicked(bool)), this, SLOT(updateGUISettings()));
    connect(ui->cbxLineDuplicate, SIGNAL(clicked(bool)), this, SLOT(updateGUISettings()));
    connect(ui->cbxFrameStep, SIGNAL(clicked(bool)), this, SLOT(updateGUISettings()));
    connect(ui->lbxBinQuality, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxPCMType, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxPCM1FieldOrder, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->cbxPCM1Offset, SIGNAL(clicked(bool)), this, SLOT(updateGUISettings()));
    connect(ui->sldPCM1OddOfs, SIGNAL(valueChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->sldPCM1EvenOfs, SIGNAL(valueChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->btnPCM1OfsSync, SIGNAL(clicked(bool)), this, SLOT(updateGUISettings()));
    connect(ui->sldPCM1OddOfs, SIGNAL(sliderMoved(int)), this, SLOT(sliderDisplayUpdate()));
    connect(ui->sldPCM1EvenOfs, SIGNAL(sliderMoved(int)), this, SLOT(sliderDisplayUpdate()));
    connect(ui->sldPCM1OddOfs, SIGNAL(valueChanged(int)), this, SLOT(sliderDisplayUpdate()));
    connect(ui->sldPCM1EvenOfs, SIGNAL(valueChanged(int)), this, SLOT(sliderDisplayUpdate()));
    connect(ui->lbxPCM16x0Format, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxPCM16x0FieldOrder, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxPCM16x0ECC, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxPCM16x0SampleRate, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxSTC007VidStandard, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxSTC007FieldOrder, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxSTC007ECC, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxSTC007CWD, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxSTC007Resolution, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxSTC007SampleRate, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxM2VidStandard, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxM2FieldOrder, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxM2ECC, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxM2CWD, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxM2SampleRate, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));
    connect(ui->lbxDropAction, SIGNAL(currentIndexChanged(int)), this, SLOT(updateGUISettings()));

    connect(ui->btnStatReset, SIGNAL(clicked(bool)), this, SLOT(clearStat()));

    enableGUIEvents();

    connect(ui->actLoadPCM, SIGNAL(triggered(bool)), this, SLOT(loadVideo()));
    connect(ui->actDecoderReset, SIGNAL(triggered(bool)), this, SLOT(resetOptDecoder()));
    connect(ui->actWinPosReset, SIGNAL(triggered(bool)), this, SLOT(resetVisPositions()));
    connect(ui->actFullReset, SIGNAL(triggered(bool)), this, SLOT(resetFull()));
    connect(ui->actAbout, SIGNAL(triggered(bool)), this, SLOT(showAbout()));
    connect(ui->actOpenCapture, SIGNAL(triggered(bool)), this, SLOT(showCaptureSelector()));
    connect(ui->actVidInFineSettings, SIGNAL(triggered(bool)), this, SLOT(showVidInFineSettings()));
    connect(ui->actBinFineSettings, SIGNAL(triggered(bool)), this, SLOT(showBinFineSettings()));
    connect(ui->actDeintFineSettings, SIGNAL(triggered(bool)), this, SLOT(showDeintFineSettings()));
    connect(ui->actVisSource, SIGNAL(toggled(bool)), this, SLOT(showVisSource(bool)));
    connect(ui->actVisBin, SIGNAL(toggled(bool)), this, SLOT(showVisBin(bool)));
    connect(ui->actVisAssembled, SIGNAL(toggled(bool)), this, SLOT(showVisAssembled(bool)));
    connect(ui->actVisBlocks, SIGNAL(toggled(bool)), this, SLOT(showVisBlocks(bool)));
    connect(ui->actExit, SIGNAL(triggered(bool)), this, SLOT(exitAction()));

#ifdef GLOBAL_DEBUG_EN
    connect(ui->actTestCRCC, SIGNAL(triggered(bool)), this, SLOT(testStartCRCC()));
    connect(ui->actTestECC, SIGNAL(triggered(bool)), this, SLOT(testStartECC()));

    connect(ui->actGenSettings, SIGNAL(triggered(bool)), this, SLOT(updateSetMainLog()));
    connect(ui->actGenProcess, SIGNAL(triggered(bool)), this, SLOT(updateSetMainLog()));
    connect(ui->actGenOff, SIGNAL(triggered(bool)), this, SLOT(clearMainPLog()));
    connect(ui->actVIPSettings, SIGNAL(triggered(bool)), this, SLOT(updateSetVIPLog()));
    connect(ui->actVIPProcess, SIGNAL(triggered(bool)), this, SLOT(updateSetVIPLog()));
    connect(ui->actVIPFrame, SIGNAL(triggered(bool)), this, SLOT(updateSetVIPLog()));
    connect(ui->actVIPLine, SIGNAL(triggered(bool)), this, SLOT(updateSetVIPLog()));
    connect(ui->actVIPOff, SIGNAL(triggered(bool)), this, SLOT(clearVIPLog()));
    connect(ui->actLBSettings, SIGNAL(triggered(bool)), this, SLOT(updateSetLBLog()));
    connect(ui->actLBProcess, SIGNAL(triggered(bool)), this, SLOT(updateSetLBLog()));
    connect(ui->actLBBright, SIGNAL(triggered(bool)), this, SLOT(updateSetLBLog()));
    connect(ui->actLBSweep, SIGNAL(triggered(bool)), this, SLOT(updateSetLBLog()));
    connect(ui->actLBCoords, SIGNAL(triggered(bool)), this, SLOT(updateSetLBLog()));
    connect(ui->actLBRawBin, SIGNAL(triggered(bool)), this, SLOT(updateSetLBLog()));
    connect(ui->actGenLines, SIGNAL(triggered(bool)), this, SLOT(updateSetLBLog()));
    connect(ui->actLBOff, SIGNAL(triggered(bool)), this, SLOT(clearLBLog()));
    connect(ui->actDISettings, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDIProcess, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDIErrorCorr, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDIDeinterleave, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDITrim, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDIPadding, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDIFields, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDIBlocks, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDIAssembly, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actGenBlocks, SIGNAL(triggered(bool)), this, SLOT(updateSetDILog()));
    connect(ui->actDIOff, SIGNAL(triggered(bool)), this, SLOT(clearDILog()));
    connect(ui->actAPSettings, SIGNAL(triggered(bool)), this, SLOT(updateSetAPLog()));
    connect(ui->actAPProcess, SIGNAL(triggered(bool)), this, SLOT(updateSetAPLog()));
    connect(ui->actAPDropAct, SIGNAL(triggered(bool)), this, SLOT(updateSetAPLog()));
    connect(ui->actAPBuffer, SIGNAL(triggered(bool)), this, SLOT(updateSetAPLog()));
    connect(ui->actAPFile, SIGNAL(triggered(bool)), this, SLOT(updateSetAPLog()));
    connect(ui->actAPLive, SIGNAL(triggered(bool)), this, SLOT(updateSetAPLog()));
    connect(ui->actAPOff, SIGNAL(triggered(bool)), this, SLOT(clearAPLog()));
    connect(ui->actDBGOff, SIGNAL(triggered(bool)), this, SLOT(clearAllLogging()));
#else
    ui->mnDebug->setEnabled(false);
#endif

    // Create and link binarizer worker.
    conv_V2D = NULL;
    conv_V2D = new QThread;
    V2D_worker = new VideoToDigital();
    V2D_worker->setInputPointers(&video_lines, &vl_lock);
    V2D_worker->setOutPCM1Pointers(&pcm1_lines, &pcm1line_lock);
    V2D_worker->setOutPCM16X0Pointers(&pcm16x0_sublines, &pcm16x0subline_lock);
    V2D_worker->setOutSTC007Pointers(&stc007_lines, &stcline_lock);
    V2D_worker->moveToThread(conv_V2D);
    connect(conv_V2D, SIGNAL(started()), V2D_worker, SLOT(doBinarize()));
    connect(V2D_worker, SIGNAL(finished()), conv_V2D, SLOT(quit()));
    connect(V2D_worker, SIGNAL(finished()), V2D_worker, SLOT(deleteLater()));
    connect(conv_V2D, SIGNAL(finished()), this, SLOT(exitAction()));
    connect(conv_V2D, SIGNAL(finished()), conv_V2D, SLOT(deleteLater()));
    connect(this, SIGNAL(aboutToExit()), V2D_worker, SLOT(stop()));
    connect(this, SIGNAL(newV2DLogLevel(uint8_t)), V2D_worker, SLOT(setLogLevel(uint8_t)));
    connect(this, SIGNAL(newBinPCMType(uint8_t)), V2D_worker, SLOT(setPCMType(uint8_t)));
    connect(this, SIGNAL(newBinMode(uint8_t)), V2D_worker, SLOT(setBinarizationMode(uint8_t)));
    connect(this, SIGNAL(newLineDupMode(bool)), V2D_worker, SLOT(setCheckLineDup(bool)));
    connect(V2D_worker, SIGNAL(newLine(PCM1Line)), this, SLOT(receiveBinLine(PCM1Line)));
    connect(V2D_worker, SIGNAL(newLine(PCM16X0SubLine)), this, SLOT(receiveBinLine(PCM16X0SubLine)));
    connect(V2D_worker, SIGNAL(newLine(STC007Line)), this, SLOT(receiveBinLine(STC007Line)));
    connect(V2D_worker, SIGNAL(guiUpdFrameBin(FrameBinDescriptor)), this, SLOT(updateStatsVideoTracking(FrameBinDescriptor)));
    //connect(V2D_worker, SIGNAL(loopTime(quint64)), this, SLOT(updateDebugBar(quint64)));

    connect(this, SIGNAL(newFrameAssembled(uint32_t)), this, SLOT(updateStatsDIFrame(uint32_t)));

    // Create and link PCM-1 deinterleaver worker.
    conv_L2B_PCM1 = NULL;
    conv_L2B_PCM1 = new QThread;
    L2B_PCM1_worker = new PCM1DataStitcher;
    L2B_PCM1_worker->setInputPointers(&pcm1_lines, &pcm1line_lock);
    L2B_PCM1_worker->setOutputPointers(&audio_data, &audio_lock);
    L2B_PCM1_worker->moveToThread(conv_L2B_PCM1);
    connect(conv_L2B_PCM1, SIGNAL(started()), L2B_PCM1_worker, SLOT(doFrameReassemble()));
    connect(L2B_PCM1_worker, SIGNAL(finished()), conv_L2B_PCM1, SLOT(quit()));
    connect(L2B_PCM1_worker, SIGNAL(finished()), L2B_PCM1_worker, SLOT(deleteLater()));
    connect(conv_L2B_PCM1, SIGNAL(finished()), this, SLOT(exitAction()));
    connect(conv_L2B_PCM1, SIGNAL(finished()), conv_L2B_PCM1, SLOT(deleteLater()));
    connect(this, SIGNAL(aboutToExit()), L2B_PCM1_worker, SLOT(stop()));
    connect(this, SIGNAL(newL2BLogLevel(uint16_t)), L2B_PCM1_worker, SLOT(setLogLevel(uint16_t)));
    connect(this, SIGNAL(newPCM1FieldOrder(uint8_t)), L2B_PCM1_worker, SLOT(setFieldOrder(uint8_t)));
    connect(this, SIGNAL(newPCM1AutoOffset(bool)), L2B_PCM1_worker, SLOT(setAutoLineOffset(bool)));
    connect(this, SIGNAL(newPCM1OddOffset(int8_t)), L2B_PCM1_worker, SLOT(setOddLineOffset(int8_t)));
    connect(this, SIGNAL(newPCM1EvenOffset(int8_t)), L2B_PCM1_worker, SLOT(setEvenLineOffset(int8_t)));
    connect(L2B_PCM1_worker, SIGNAL(newLineProcessed(PCM1SubLine)), this, SLOT(receiveAsmLine(PCM1SubLine)));
    connect(L2B_PCM1_worker, SIGNAL(newBlockProcessed(PCM1DataBlock)), this, SLOT(receivePCMDataBlock(PCM1DataBlock)));
    connect(L2B_PCM1_worker, SIGNAL(guiUpdFrameAsm(FrameAsmPCM1)), this, SLOT(updateStatsFrameAsm(FrameAsmPCM1)));
    //connect(L2B_PCM1_worker, SIGNAL(loopTime(quint64)), this, SLOT(updateDebugBar(quint64)));

    // Create and link PCM-16x0 deinterleaver worker.
    conv_L2B_PCM16X0 = NULL;
    conv_L2B_PCM16X0 = new QThread;
    L2B_PCM16X0_worker = new PCM16X0DataStitcher;
    L2B_PCM16X0_worker->setInputPointers(&pcm16x0_sublines, &pcm16x0subline_lock);
    L2B_PCM16X0_worker->setOutputPointers(&audio_data, &audio_lock);
    L2B_PCM16X0_worker->moveToThread(conv_L2B_PCM16X0);
    connect(conv_L2B_PCM16X0, SIGNAL(started()), L2B_PCM16X0_worker, SLOT(doFrameReassemble()));
    connect(L2B_PCM16X0_worker, SIGNAL(finished()), conv_L2B_PCM16X0, SLOT(quit()));
    connect(L2B_PCM16X0_worker, SIGNAL(finished()), L2B_PCM16X0_worker, SLOT(deleteLater()));
    connect(conv_L2B_PCM16X0, SIGNAL(finished()), this, SLOT(exitAction()));
    connect(conv_L2B_PCM16X0, SIGNAL(finished()), conv_L2B_PCM16X0, SLOT(deleteLater()));
    connect(this, SIGNAL(aboutToExit()), L2B_PCM16X0_worker, SLOT(stop()));
    connect(this, SIGNAL(newL2BLogLevel(uint16_t)), L2B_PCM16X0_worker, SLOT(setLogLevel(uint16_t)));
    connect(this, SIGNAL(newPCM16x0Format(uint8_t)), L2B_PCM16X0_worker, SLOT(setFormat(uint8_t)));
    connect(this, SIGNAL(newPCM16x0FieldOrder(uint8_t)), L2B_PCM16X0_worker, SLOT(setFieldOrder(uint8_t)));
    connect(this, SIGNAL(newPCM16x0PCorrection(bool)), L2B_PCM16X0_worker, SLOT(setPCorrection(bool)));
    connect(this, SIGNAL(newPCM16x0SampleRatePreset(uint16_t)), L2B_PCM16X0_worker, SLOT(setSampleRatePreset(uint16_t)));
    connect(L2B_PCM16X0_worker, SIGNAL(newLineProcessed(PCM16X0SubLine)), this, SLOT(receiveAsmLine(PCM16X0SubLine)));
    connect(L2B_PCM16X0_worker, SIGNAL(newBlockProcessed(PCM16X0DataBlock)), this, SLOT(receivePCMDataBlock(PCM16X0DataBlock)));
    connect(L2B_PCM16X0_worker, SIGNAL(guiUpdFrameAsm(FrameAsmPCM16x0)), this, SLOT(updateStatsFrameAsm(FrameAsmPCM16x0)));
    //connect(L2B_PCM16X0_worker, SIGNAL(loopTime(quint64)), this, SLOT(updateDebugBar(quint64)));

    // Create and link STC-007 deinterleaver worker.
    conv_L2B_STC007 = NULL;
    conv_L2B_STC007 = new QThread;
    L2B_STC007_worker = new STC007DataStitcher;
    L2B_STC007_worker->setInputPointers(&stc007_lines, &stcline_lock);
    L2B_STC007_worker->setOutputPointers(&audio_data, &audio_lock);
    L2B_STC007_worker->moveToThread(conv_L2B_STC007);
    connect(conv_L2B_STC007, SIGNAL(started()), L2B_STC007_worker, SLOT(doFrameReassemble()));
    connect(L2B_STC007_worker, SIGNAL(finished()), conv_L2B_STC007, SLOT(quit()));
    connect(L2B_STC007_worker, SIGNAL(finished()), L2B_STC007_worker, SLOT(deleteLater()));
    connect(conv_L2B_STC007, SIGNAL(finished()), this, SLOT(exitAction()));
    connect(conv_L2B_STC007, SIGNAL(finished()), conv_L2B_STC007, SLOT(deleteLater()));
    connect(this, SIGNAL(aboutToExit()), L2B_STC007_worker, SLOT(stop()));
    connect(this, SIGNAL(newL2BLogLevel(uint16_t)), L2B_STC007_worker, SLOT(setLogLevel(uint16_t)));
    connect(this, SIGNAL(newSTC007VidStandard(uint8_t)), L2B_STC007_worker, SLOT(setVideoStandard(uint8_t)));
    connect(this, SIGNAL(newSTC007FieldOrder(uint8_t)), L2B_STC007_worker, SLOT(setFieldOrder(uint8_t)));
    connect(this, SIGNAL(newSTC007PCorrection(bool)), L2B_STC007_worker, SLOT(setPCorrection(bool)));
    connect(this, SIGNAL(newSTC007QCorrection(bool)), L2B_STC007_worker, SLOT(setQCorrection(bool)));
    connect(this, SIGNAL(newSTC007CWDCorrection(bool)), L2B_STC007_worker, SLOT(setCWDCorrection(bool)));
    connect(this, SIGNAL(newSTC007M2Sampling(bool)), L2B_STC007_worker, SLOT(setM2SampleFormat(bool)));
    connect(this, SIGNAL(newSTC007ResolutionPreset(uint8_t)), L2B_STC007_worker, SLOT(setResolutionPreset(uint8_t)));
    connect(this, SIGNAL(newSTC007SampleRatePreset(uint16_t)), L2B_STC007_worker, SLOT(setSampleRatePreset(uint16_t)));
    connect(L2B_STC007_worker, SIGNAL(newLineProcessed(STC007Line)), this, SLOT(receiveAsmLine(STC007Line)));
    connect(L2B_STC007_worker, SIGNAL(newBlockProcessed(STC007DataBlock)), this, SLOT(receivePCMDataBlock(STC007DataBlock)));
    connect(L2B_STC007_worker, SIGNAL(newBlockProcessed(STC007DataBlock)), this, SLOT(updateStatsBlockTime(STC007DataBlock)));
    connect(L2B_STC007_worker, SIGNAL(guiUpdFrameAsm(FrameAsmSTC007)), this, SLOT(updateStatsFrameAsm(FrameAsmSTC007)));
    //connect(L2B_STC007_worker, SIGNAL(loopTime(quint64)), this, SLOT(updateDebugBar(quint64)));

    // Create and link audio processor worker.
    audio_PU = NULL;
    audio_PU = new QThread;
    AP_worker = new AudioProcessor;
    AP_worker->setOutputToFile(true);
    AP_worker->setOutputToLive(true);
    AP_worker->setInputPointers(&audio_data, &audio_lock);
    AP_worker->moveToThread(audio_PU);
    connect(audio_PU, SIGNAL(started()), AP_worker, SLOT(processAudio()));
    connect(AP_worker, SIGNAL(finished()), audio_PU, SLOT(quit()));
    connect(AP_worker, SIGNAL(finished()), AP_worker, SLOT(deleteLater()));
    connect(audio_PU, SIGNAL(finished()), this, SLOT(exitAction()));
    connect(audio_PU, SIGNAL(finished()), audio_PU, SLOT(deleteLater()));
    connect(this, SIGNAL(aboutToExit()), AP_worker, SLOT(stop()));
    connect(this, SIGNAL(newAPLogLevel(uint8_t)), AP_worker, SLOT(setLogLevel(uint8_t)));
    connect(this, SIGNAL(newMaskingMode(uint8_t)), AP_worker, SLOT(setMasking(uint8_t)));
    connect(this, SIGNAL(newEnableLivePB(bool)), AP_worker, SLOT(setOutputToLive(bool)));
    connect(this, SIGNAL(newEnableWaveSave(bool)), AP_worker, SLOT(setOutputToFile(bool)));
    connect(AP_worker, SIGNAL(mediaError(QString)), this, SLOT(displayErrorMessage(QString)));
    connect(AP_worker, SIGNAL(guiAddMask(uint16_t)), this, SLOT(updateStatsMaskes(uint16_t)));
    connect(AP_worker, SIGNAL(guiLivePB(bool)), this, SLOT(livePBUpdate(bool)));
    connect(AP_worker, SIGNAL(outVULevels(uint8_t,uint8_t)), this, SLOT(receiveVUMeters(uint8_t,uint8_t)));

    connect(this, SIGNAL(newFineReset()), this, SLOT(setDefaultFineSettings()));
    connect(this, SIGNAL(newFineReset()), VIN_worker, SLOT(setDefaultFineSettings()));
    connect(this, SIGNAL(newFineReset()), V2D_worker, SLOT(setDefaultFineSettings()));
    connect(this, SIGNAL(newFineReset()), L2B_PCM16X0_worker, SLOT(setDefaultFineSettings()));
    connect(this, SIGNAL(newFineReset()), L2B_STC007_worker, SLOT(setDefaultFineSettings()));

    // Start new thread with binarizer.
    conv_V2D->start(QThread::InheritPriority);
    // Start new thread with deinterleaver/frame assembler.
    conv_L2B_PCM1->start(QThread::InheritPriority);
    conv_L2B_PCM16X0->start(QThread::InheritPriority);
    conv_L2B_STC007->start(QThread::InheritPriority);
    // Start new thread with audio processor.
    audio_PU->start(QThread::InheritPriority);

    qInfo()<<"[M] Application path:"<<qApp->applicationDirPath();
    // Code can hang here if antivirus software locks access to storage or storage has problems.
    qInfo()<<"[M] Waiting for translations to load...";
    // Regenerate language submenu.
    updateGUILangList();
    // Try to apply appropriate language.
    setGUILanguage(QLocale::system().name(), true);
    //setGUILanguage("pl_PL", true);

    // Start UI counters updater.
    timUIUpdate.start();
}

MainWindow::~MainWindow()
{
    delete ui;
}

//------------------------ Main window was moved.
void MainWindow::moveEvent(QMoveEvent *event)
{
    // (Re)start timer to update window position and size.
    timResizeUpd.start();
    event->accept();
}

//------------------------ Main window was resized.
void MainWindow::resizeEvent(QResizeEvent *event)
{
    // (Re)start timer to update window position and size.
    timResizeUpd.start();
    event->accept();
}

//------------------------ Application is about to close.
void MainWindow::closeEvent(QCloseEvent *event)
{
    if(shutdown_started==false)
    {
        shutdown_started = true;
        qInfo()<<"[M] Shutting down...";
        // Stop GUI update and thread check.
        timUIUpdate.stop();

        // Notify all threads about exiting.
        emit aboutToExit();
        // Wait for threads to stop.
        if(input_FPU!=NULL)
        {
            qInfo()<<"[M] Video decoder shutting down...";
            conv_V2D->quit();
            conv_V2D->wait(1000);
        }
        if(conv_V2D!=NULL)
        {
            qInfo()<<"[M] Binarizer shutting down...";
            conv_V2D->quit();
            conv_V2D->wait(250);
        }
        if(conv_L2B_PCM1!=NULL)
        {
            qInfo()<<"[M] PCM-1 deinterleaver shutting down...";
            conv_L2B_PCM16X0->quit();
            conv_L2B_PCM16X0->wait(250);
        }
        if(conv_L2B_PCM16X0!=NULL)
        {
            qInfo()<<"[M] PCM-16x0 deinterleaver shutting down...";
            conv_L2B_PCM16X0->quit();
            conv_L2B_PCM16X0->wait(250);
        }
        if(conv_L2B_STC007!=NULL)
        {
            qInfo()<<"[M] STC-007 deinterleaver shutting down...";
            conv_L2B_STC007->quit();
            conv_L2B_STC007->wait(250);
        }
        if(audio_PU!=NULL)
        {
            qInfo()<<"[M] Audio processor shutting down...";
            audio_PU->quit();
            audio_PU->wait(500);
        }
        event->accept();
    }
}

//------------------------ Generate proper full name for translation file.
QString MainWindow::generateTranslationPath(QString in_locale)
{
    QString lang_file;
    lang_file = QString(APP_EXEC)+QString("_%1").arg(in_locale);
    return lang_file;
}

//------------------------ Generate list of available translation files.
QStringList MainWindow::getTranslationList()
{
    QString app_name, lang_path;
    QStringList name_filters, files_list;
    QDir lang_dir;
    app_name = QString(APP_EXEC);
    // Get path to executable (translations should be nearby).
    lang_path = APP_LANG_PATH;
    // Set parameters for directory listing.
    lang_dir.setFilter(QDir::Files|QDir::Readable|QDir::Hidden|QDir::NoSymLinks);
    lang_dir.setSorting(QDir::Name|QDir::IgnoreCase);
    lang_dir.setPath(lang_path);
    name_filters.append(app_name+QString("_??.qm"));
    lang_dir.setNameFilters(name_filters);
    // Get available translations' filenames.
    files_list = lang_dir.entryList();
    // Remove app name and file extension.
    files_list.replaceInStrings(app_name+"_", "", Qt::CaseInsensitive);
    files_list.replaceInStrings(".qm", "", Qt::CaseInsensitive);
    for(auto& i:files_list)
    {
        i = i.toLower();
    }
    files_list.removeDuplicates();

    return files_list;
}

//------------------------ Update list of menu items (actions) for language selection.
void MainWindow::updateGUILangList()
{
    QString lang_name;
    QStringList lang_list;
    // Get list of available translations.
    lang_list = getTranslationList();

    // Remove all menu actions except the default one.
    for(auto& menu_entry:ui->mnLanguage->actions())
    {
        if(menu_entry->objectName()!="actLangRU")
        {
            menu_entry->disconnect();
            delete menu_entry;
        }
        else
        {
            // Refresh default language ID.
            menu_entry->setData(QString("ru_RU"));
        }
    }

    for(const auto& new_locale:lang_list)
    {
        QLocale locale_data(new_locale);
        lang_name = locale_data.languageToString(locale_data.language());
        qInfo()<<"[M] Translation available:"<<lang_name<<"("<<new_locale<<")";
        // Create new action to change language.
        QAction *new_action = new QAction(lang_name, ui->mnLanguage);
        // Set IDs for processing.
        new_action->setObjectName("actLang"+new_locale.toUpper());
        new_action->setData(locale_data.name());
        new_action->setCheckable(true);
        // Add action to menu (action handler is already connected to menu).
        ui->mnLanguage->addAction(new_action);
    }
}

//------------------------ Set another GUI language.
void MainWindow::setGUILanguage(QString in_locale, bool suppress)
{
    QString short_locale, lang_file, lang_path;
    lang_file = generateTranslationPath(in_locale);
    lang_path = APP_LANG_PATH;

    qInfo()<<"[M] New locale:"<<in_locale;

    short_locale = in_locale.left(in_locale.indexOf("_"));
    if(short_locale==PCM_LCL_DEFAULT)
    {
        // This is the default language of the app.
        qApp->removeTranslator(&trUI);
    }
    else
    {
        // Try to load translation file.
        if(trUI.load(lang_file, lang_path)==false)
        {
            qWarning()<<DBG_ANCHOR<<"[M] Unable to load translation file!";
            // Fallback to English translation.
            short_locale = PCM_LCL_FALLBACK;
            lang_file = generateTranslationPath(short_locale);
            if(trUI.load(lang_file, lang_path)!=false)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Defaulted to English translation!";
                if(suppress==false)
                {
                    displayErrorMessage("Unable to load translation file for '"+in_locale+"' locale, defaulted to English!");
                }
            }
        }
        if(trUI.isEmpty()==false)
        {
            // Apply translation.
            qApp->installTranslator(&trUI);
            qInfo()<<"[M] Translation applied:"<<lang_file;
        }
        else
        {
            short_locale = PCM_LCL_DEFAULT;
            if(suppress==false)
            {
                displayErrorMessage("Unable to load translation file, defaulted to Russian!");
            }
        }
    }

    // Select appropriate action in the menu.
    QLocale applied_locale(short_locale);
    for(auto& menu_entry:ui->mnLanguage->actions())
    {
        if(menu_entry->data().toString()!=applied_locale.name())
        {
            // Deselect all actions, that is not the triggered one.
            menu_entry->setChecked(false);
        }
        else
        {
            // Mark the triggered action.
            menu_entry->setChecked(true);
        }
    }

    // Prevent false events from GUI elements (comboboxes) on retranslate.
    disableGUIEvents();
    // Update window strings.
    ui->retranslateUi(this);
    // Re-apply all GUI settings.
    readGUISettings();
    // Re-open visualizations to update strings.
    reopenVisSource();
    reopenVisualizers();
}

//------------------------ Set options for [VideoInFFMPEG] module.
void MainWindow::setVIPOptions()
{
    // Stepped playback.
    emit newStepPlay(ui->cbxFrameStep->isChecked());
    // Set frame drop detection.
    emit newFrameDropDetection(ui->cbxFrameDropout->isChecked());
}

//------------------------ Set options for [VideoToDigital] and [Binarizer] modules.
void MainWindow::setLBOptions()
{
    // Set PCM type.
    if(set_pcm_type!=ui->lbxPCMType->currentIndex())
    {
        if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM1)
        {
            emit newBinPCMType(VideoToDigital::TYPE_PCM1);
        }
        else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM16X0)
        {
            emit newBinPCMType(VideoToDigital::TYPE_PCM16X0);
        }
        else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_STC007)
        {
            emit newBinPCMType(VideoToDigital::TYPE_STC007);
        }
        else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_M2)
        {
            emit newBinPCMType(VideoToDigital::TYPE_M2);
        }
        else
        {
            qWarning()<<DBG_ANCHOR<<"[M] Logic error, PCM type list count mismatch!";
        }
        set_pcm_type = ui->lbxPCMType->currentIndex();
    }
    // Set binarization mode.
    if(ui->lbxBinQuality->currentIndex()==LIST_BQ_FAST)
    {
        emit newBinMode(Binarizer::MODE_FAST);
    }
    else if(ui->lbxBinQuality->currentIndex()==LIST_BQ_DRAFT)
    {
        emit newBinMode(Binarizer::MODE_DRAFT);
    }
    else if(ui->lbxBinQuality->currentIndex()==LIST_BQ_NORMAL)
    {
        emit newBinMode(Binarizer::MODE_NORMAL);
    }
    else if(ui->lbxBinQuality->currentIndex()==LIST_BQ_INSANE)
    {
        emit newBinMode(Binarizer::MODE_INSANE);
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[M] Logic error, binarization mode list count mismatch!";
    }
    // Set line duplication detection.
    emit newLineDupMode(ui->cbxLineDuplicate->isChecked());
}

//------------------------ Set options for [xDataStitcher] and [xDeinterleaver] modules.
void MainWindow::setDIOptions()
{
    // PCM-1 settings.
    // Preset PCM-1 field order.
    if(ui->lbxPCM1FieldOrder->currentIndex()==LIST_PCM1_FO_BFF)
    {
        emit newPCM1FieldOrder(FrameAsmDescriptor::ORDER_BFF);
    }
    else
    {
        emit newPCM1FieldOrder(FrameAsmDescriptor::ORDER_TFF);
        if(ui->lbxPCM1FieldOrder->currentIndex()!=LIST_PCM1_FO_TFF)
        {
            qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxPCM1FieldOrder] out of bounds:"<<ui->lbxPCM1FieldOrder->currentIndex();
        }
    }
    // Preset line offset.
    if(ui->cbxPCM1Offset->isChecked()!=false)
    {
        ui->sldPCM1OddOfs->setEnabled(false);
        ui->sldPCM1EvenOfs->setEnabled(false);
        ui->lblPCM1OddOfs->setEnabled(false);
        ui->lblPCM1EvenOfs->setEnabled(false);
        ui->edtPCM1OddOfs->setEnabled(false);
        ui->edtPCM1EvenOfs->setEnabled(false);
    }
    else
    {
        ui->sldPCM1OddOfs->setEnabled(true);
        if(ui->btnPCM1OfsSync->isChecked()!=false)
        {
            ui->sldPCM1EvenOfs->setEnabled(false);
            ui->sldPCM1EvenOfs->setValue(ui->sldPCM1OddOfs->value()-1);
        }
        else
        {
            ui->sldPCM1EvenOfs->setEnabled(true);
        }
        ui->lblPCM1OddOfs->setEnabled(true);
        ui->lblPCM1EvenOfs->setEnabled(true);
        ui->edtPCM1OddOfs->setEnabled(true);
        ui->edtPCM1EvenOfs->setEnabled(true);
    }
    emit newPCM1AutoOffset(ui->cbxPCM1Offset->isChecked());
    emit newPCM1OddOffset((int8_t)ui->sldPCM1OddOfs->value());
    emit newPCM1EvenOffset((int8_t)ui->sldPCM1EvenOfs->value());

    // PCM-16x0 settings.
    // Preset PCM-16x0 mode/format.
    if(ui->lbxPCM16x0Format->currentIndex()==LIST_PCM16X0_FMT_EI)
    {
        emit newPCM16x0Format(PCM16X0Deinterleaver::FORMAT_EI);
    }
    else if(ui->lbxPCM16x0Format->currentIndex()==LIST_PCM16X0_FMT_SI)
    {
        emit newPCM16x0Format(PCM16X0Deinterleaver::FORMAT_SI);
    }
    else
    {
        // TODO: create algorythm for PCM-16x0 format auto-detection.
        emit newPCM16x0Format(PCM16X0Deinterleaver::FORMAT_SI);
        ui->lbxPCM16x0Format->setCurrentIndex(LIST_PCM16X0_FMT_SI);
        /*emit new16x0Format(PCM16X0Deinterleaver::FORMAT_AUTO);
        if(ui->lbxPCM16x0Format->currentIndex()!=LIST_16X0_AUTO)
        {
            qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxPCM16x0Format] out of bounds:"<<ui->lbxPCM16x0Format->currentIndex();
        }*/
    }
    // Preset PCM-16x0 field order.
    if(ui->lbxPCM16x0FieldOrder->currentIndex()==LIST_PCM16X0_FO_BFF)
    {
        emit newPCM16x0FieldOrder(FrameAsmDescriptor::ORDER_BFF);
    }
    else
    {
        emit newPCM16x0FieldOrder(FrameAsmDescriptor::ORDER_TFF);
        if(ui->lbxPCM16x0FieldOrder->currentIndex()!=LIST_PCM16X0_FO_TFF)
        {
            qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxPCM16x0FieldOrder] out of bounds:"<<ui->lbxPCM16x0FieldOrder->currentIndex();
        }
    }
    // Preset PCM-16x0 error correction settings.
    if(ui->lbxPCM16x0ECC->currentIndex()==LIST_PCM16X0_ECC_NONE)
    {
        emit newPCM16x0PCorrection(false);
    }
    else
    {
        emit newPCM16x0PCorrection(true);
        if(ui->lbxPCM16x0ECC->currentIndex()!=LIST_PCM16X0_ECC_PARITY)
        {
            qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxPCM16x0ECC] out of bounds:"<<ui->lbxPCM16x0ECC->currentIndex();
        }
    }
    // Preset PCM-16x0 audio sample rate.
    if(ui->lbxPCM16x0SampleRate->currentIndex()==LIST_PCM16X0_SRATE_44100)
    {
        emit newPCM16x0SampleRatePreset(PCMSamplePair::SAMPLE_RATE_44100);
    }
    else if(ui->lbxPCM16x0SampleRate->currentIndex()==LIST_PCM16X0_SRATE_44056)
    {
        emit newPCM16x0SampleRatePreset(PCMSamplePair::SAMPLE_RATE_44056);
    }
    else
    {
        emit newPCM16x0SampleRatePreset(PCMSamplePair::SAMPLE_RATE_AUTO);
        if(ui->lbxPCM16x0SampleRate->currentIndex()!=LIST_PCM16X0_SRATE_AUTO)
        {
            qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxPCM16x0SampleRate] out of bounds:"<<ui->lbxPCM16x0SampleRate->currentIndex();
        }
    }

    if(set_pcm_type==LIST_TYPE_STC007)
    {
        // STC-007 settings.
        // Preset STC-007 video standard.
        if(ui->lbxSTC007VidStandard->currentIndex()==LIST_STC007_VID_NTSC)
        {
            emit newSTC007VidStandard(FrameAsmDescriptor::VID_NTSC);
        }
        else if(ui->lbxSTC007VidStandard->currentIndex()==LIST_STC007_VID_PAL)
        {
            emit newSTC007VidStandard(FrameAsmDescriptor::VID_PAL);
        }
        else
        {
            emit newSTC007VidStandard(FrameAsmDescriptor::VID_UNKNOWN);
            if(ui->lbxSTC007VidStandard->currentIndex()!=LIST_STC007_VID_AUTO)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxSTC007VidStandard] out of bounds:"<<ui->lbxSTC007VidStandard->currentIndex();
            }
        }
        // Preset STC-007 field order.
        if(ui->lbxSTC007FieldOrder->currentIndex()==LIST_STC007_FO_TFF)
        {
            emit newSTC007FieldOrder(FrameAsmDescriptor::ORDER_TFF);
        }
        else if(ui->lbxSTC007FieldOrder->currentIndex()==LIST_STC007_FO_BFF)
        {
            emit newSTC007FieldOrder(FrameAsmDescriptor::ORDER_BFF);
        }
        else
        {
            emit newSTC007FieldOrder(FrameAsmDescriptor::ORDER_UNK);
            if(ui->lbxSTC007FieldOrder->currentIndex()!=LIST_STC007_FO_AUTO)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxSTC007FieldOrder] out of bounds:"<<ui->lbxSTC007FieldOrder->currentIndex();
            }
        }
        // Set STC-007 error correction settings.
        if(ui->lbxSTC007ECC->currentIndex()==LIST_STC007_ECC_NONE)
        {
            emit newSTC007PCorrection(false);
            emit newSTC007QCorrection(false);
        }
        else if(ui->lbxSTC007ECC->currentIndex()==LIST_STC007_ECC_PARITY)
        {
            emit newSTC007PCorrection(true);
            emit newSTC007QCorrection(false);
        }
        else
        {
            emit newSTC007PCorrection(true);
            emit newSTC007QCorrection(true);
            if(ui->lbxSTC007ECC->currentIndex()!=LIST_STC007_ECC_FULL)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxSTC007ECC] out of bounds:"<<ui->lbxSTC007ECC->currentIndex();
            }
        }
        // Set STC-007 CWD correction settings.
        if(ui->lbxSTC007CWD->currentIndex()==LIST_STC007_CWD_DIS)
        {
            emit newSTC007CWDCorrection(false);
        }
        else
        {
            emit newSTC007CWDCorrection(true);
            if(ui->lbxSTC007CWD->currentIndex()!=LIST_STC007_CWD_EN)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxSTC007CWD] out of bounds:"<<ui->lbxSTC007CWD->currentIndex();
            }
        }
        // Preset STC-007 audio resolution.
        if(ui->lbxSTC007Resolution->currentIndex()==LIST_STC007_RES_14BIT)
        {
            emit newSTC007ResolutionPreset(STC007DataStitcher::SAMPLE_RES_14BIT);
        }
        else if(ui->lbxSTC007Resolution->currentIndex()==LIST_STC007_RES_16BIT)
        {
            emit newSTC007ResolutionPreset(STC007DataStitcher::SAMPLE_RES_16BIT);
        }
        else
        {
            emit newSTC007ResolutionPreset(STC007DataStitcher::SAMPLE_RES_UNKNOWN);
            if(ui->lbxSTC007Resolution->currentIndex()!=LIST_STC007_RES_AUTO)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxSTC007Resolution] out of bounds:"<<ui->lbxSTC007Resolution->currentIndex();
            }
        }
        // Preset STC-007 audio sample rate.
        if(ui->lbxSTC007SampleRate->currentIndex()==LIST_STC007_SRATE_44056)
        {
            emit newSTC007SampleRatePreset(PCMSamplePair::SAMPLE_RATE_44056);
        }
        else if(ui->lbxSTC007SampleRate->currentIndex()==LIST_STC007_SRATE_44100)
        {
            emit newSTC007SampleRatePreset(PCMSamplePair::SAMPLE_RATE_44100);
        }
        else
        {
            emit newSTC007SampleRatePreset(PCMSamplePair::SAMPLE_RATE_AUTO);
            if(ui->lbxSTC007SampleRate->currentIndex()!=LIST_STC007_SRATE_AUTO)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxSTC007SampleRate] out of bounds:"<<ui->lbxSTC007SampleRate->currentIndex();
            }
        }
        emit newSTC007M2Sampling(false);
    }
    else if(set_pcm_type==LIST_TYPE_M2)
    {
        // M2 settings.
        // Preset M2 video standard.
        if(ui->lbxM2VidStandard->currentIndex()==LIST_M2_VID_NTSC)
        {
            emit newSTC007VidStandard(FrameAsmDescriptor::VID_NTSC);
        }
        else if(ui->lbxM2VidStandard->currentIndex()==LIST_M2_VID_PAL)
        {
            emit newSTC007VidStandard(FrameAsmDescriptor::VID_PAL);
        }
        else
        {
            emit newSTC007VidStandard(FrameAsmDescriptor::VID_UNKNOWN);
            if(ui->lbxM2VidStandard->currentIndex()!=LIST_M2_VID_AUTO)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxM2VidStandard] out of bounds:"<<ui->lbxM2VidStandard->currentIndex();
            }
        }
        // Preset M2 field order.
        if(ui->lbxM2FieldOrder->currentIndex()==LIST_M2_FO_TFF)
        {
            emit newSTC007FieldOrder(FrameAsmDescriptor::ORDER_TFF);
        }
        else if(ui->lbxM2FieldOrder->currentIndex()==LIST_M2_FO_BFF)
        {
            emit newSTC007FieldOrder(FrameAsmDescriptor::ORDER_BFF);
        }
        else
        {
            emit newSTC007FieldOrder(FrameAsmDescriptor::ORDER_UNK);
            if(ui->lbxM2FieldOrder->currentIndex()!=LIST_M2_FO_AUTO)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxM2FieldOrder] out of bounds:"<<ui->lbxM2FieldOrder->currentIndex();
            }
        }
        // Set M2 error correction settings.
        if(ui->lbxM2ECC->currentIndex()==LIST_M2_ECC_NONE)
        {
            emit newSTC007PCorrection(false);
            emit newSTC007QCorrection(false);
        }
        else if(ui->lbxM2ECC->currentIndex()==LIST_M2_ECC_PARITY)
        {
            emit newSTC007PCorrection(true);
            emit newSTC007QCorrection(false);
        }
        else
        {
            emit newSTC007PCorrection(true);
            emit newSTC007QCorrection(true);
            if(ui->lbxM2ECC->currentIndex()!=LIST_M2_ECC_FULL)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxM2ECC] out of bounds:"<<ui->lbxM2ECC->currentIndex();
            }
        }
        // Set M2 CWD correction settings.
        if(ui->lbxM2CWD->currentIndex()==LIST_M2_CWD_DIS)
        {
            emit newSTC007CWDCorrection(false);
        }
        else
        {
            emit newSTC007CWDCorrection(true);
            if(ui->lbxM2CWD->currentIndex()!=LIST_M2_CWD_EN)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxM2CWD] out of bounds:"<<ui->lbxM2CWD->currentIndex();
            }
        }
        // Preset M2 audio sample rate.
        if(ui->lbxM2SampleRate->currentIndex()==LIST_M2_SRATE_44056)
        {
            emit newSTC007SampleRatePreset(PCMSamplePair::SAMPLE_RATE_44056);
        }
        else if(ui->lbxM2SampleRate->currentIndex()==LIST_M2_SRATE_44100)
        {
            emit newSTC007SampleRatePreset(PCMSamplePair::SAMPLE_RATE_44100);
        }
        else
        {
            emit newSTC007SampleRatePreset(PCMSamplePair::SAMPLE_RATE_AUTO);
            if(ui->lbxM2SampleRate->currentIndex()!=LIST_M2_SRATE_AUTO)
            {
                qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxM2SampleRate] out of bounds:"<<ui->lbxM2SampleRate->currentIndex();
            }
        }
        emit newSTC007M2Sampling(true);
    }
}

//------------------------ Set options for [AudioProcessor], [SamplesToAudio] and [SamplesToWAV] modules.
void MainWindow::setAPOptions()
{
    int index;
    // Set dropout masking settings for AP.
    index = ui->lbxDropAction->currentIndex();
    if(index==LIST_DOA_INTER_WORD)
    {
        emit newMaskingMode(AudioProcessor::DROP_INTER_LIN_WORD);
    }
    else if(index==LIST_DOA_INTER_BLOCK)
    {
        emit newMaskingMode(AudioProcessor::DROP_INTER_LIN_BLOCK);
    }
    else if(index==LIST_DOA_HOLD_WORD)
    {
        emit newMaskingMode(AudioProcessor::DROP_HOLD_WORD);
    }
    else if(index==LIST_DOA_HOLD_BLOCK)
    {
        emit newMaskingMode(AudioProcessor::DROP_HOLD_BLOCK);
    }
    else if(index==LIST_DOA_MUTE_WORD)
    {
        emit newMaskingMode(AudioProcessor::DROP_MUTE_WORD);
    }
    else if(index==LIST_DOA_MUTE_BLOCK)
    {
        emit newMaskingMode(AudioProcessor::DROP_MUTE_BLOCK);
    }
    else if(index==LIST_DOA_SKIP)
    {
        emit newMaskingMode(AudioProcessor::DROP_IGNORE);
    }
    else
    {
        qWarning()<<DBG_ANCHOR<<"[M] Logic error: index of [lbxDropAction] out of bounds:"<<ui->lbxDropAction->currentIndex();
    }
    // Set live playback settings.
    emit newEnableLivePB(ui->cbxLivePB->isChecked());
    // Set save to file settings.
    emit newEnableWaveSave(ui->cbxWaveSave->isChecked());
}

//------------------------ Set debug logging mode for main module.
void MainWindow::setMainLogMode()
{
    log_level = 0;
    if(ui->actGenSettings->isChecked()!=false) log_level |= LOG_SETTINGS;
    if(ui->actGenProcess->isChecked()!=false) log_level |= LOG_PROCESS;
}

//------------------------ Set debug logging mode for [VideoInFFMPEG] module.
void MainWindow::setVIPLogMode()
{
    uint8_t ext_log_vip = 0;
    if(ui->actVIPSettings->isChecked()!=false) ext_log_vip |= VideoInFFMPEG::LOG_SETTINGS;
    if(ui->actVIPProcess->isChecked()!=false) ext_log_vip |= VideoInFFMPEG::LOG_PROCESS;
    if(ui->actVIPFrame->isChecked()!=false) ext_log_vip |= VideoInFFMPEG::LOG_FRAME;
    if(ui->actVIPLine->isChecked()!=false) ext_log_vip |= VideoInFFMPEG::LOG_LINES;
    emit newVIPLogLevel(ext_log_vip);
}

//------------------------ Set debug logging mode for [VideoToDigital] and [Binarizer] modules.
void MainWindow::setLBLogMode()
{
    uint8_t ext_log_lb = 0;
    if(ui->actLBSettings->isChecked()!=false) ext_log_lb |= Binarizer::LOG_SETTINGS;
    if(ui->actLBProcess->isChecked()!=false) ext_log_lb |= Binarizer::LOG_PROCESS;
    if(ui->actLBBright->isChecked()!=false) ext_log_lb |= Binarizer::LOG_BRIGHT;
    if(ui->actLBSweep->isChecked()!=false) ext_log_lb |= Binarizer::LOG_REF_SWEEP;
    if(ui->actLBCoords->isChecked()!=false) ext_log_lb |= Binarizer::LOG_COORD;
    if(ui->actLBRawBin->isChecked()!=false) ext_log_lb |= Binarizer::LOG_RAWBIN;
    if(ui->actGenLines->isChecked()!=false) ext_log_lb |= Binarizer::LOG_LINE_DUMP;
    emit newV2DLogLevel(ext_log_lb);
}

//------------------------ Set debug logging mode for [xDataStitcher] and [xDeinterleaver] modules.
void MainWindow::setDILogMode()
{
    uint16_t ext_log_di = 0;
    if(ui->actDISettings->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_SETTINGS;
    if(ui->actDIProcess->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_PROCESS;
    if(ui->actDIErrorCorr->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_ERROR_CORR;
    if(ui->actDIDeinterleave->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_DEINTERLEAVE;
    if(ui->actDITrim->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_TRIM;
    if(ui->actDIPadding->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_PADDING;
    if(ui->actDIFields->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_PADDING_LINE;
    if(ui->actDIBlocks->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_PADDING_BLOCK;
    if(ui->actDIAssembly->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_FIELD_ASSEMBLY;
    if(ui->actGenBlocks->isChecked()!=false) ext_log_di |= STC007DataStitcher::LOG_BLOCK_DUMP;
    emit newL2BLogLevel(ext_log_di);
}

//------------------------ Set debug logging mode for [AudioProcessor], [SamplesToAudio] and [SamplesToWAV] modules.
void MainWindow::setAPLogMode()
{
    uint8_t ext_log_ap = 0;
    if(ui->actAPSettings->isChecked()!=false) ext_log_ap |= AudioProcessor::LOG_SETTINGS;
    if(ui->actAPProcess->isChecked()!=false) ext_log_ap |= AudioProcessor::LOG_PROCESS;
    if(ui->actAPDropAct->isChecked()!=false) ext_log_ap |= AudioProcessor::LOG_DROP_ACT;
    if(ui->actAPBuffer->isChecked()!=false) ext_log_ap |= AudioProcessor::LOG_BUF_DUMP;
    if(ui->actAPFile->isChecked()!=false) ext_log_ap |= AudioProcessor::LOG_FILE_OP;
    if(ui->actAPLive->isChecked()!=false) ext_log_ap |= AudioProcessor::LOG_LIVE_OP;
    emit newAPLogLevel(ext_log_ap);
}


//------------------------ Disable GUI events to prevent switching settings to default while changing GUI translation.
void MainWindow::disableGUIEvents()
{
    inhibit_setting_save = true;
}

//------------------------ Re-enable GUI events after changing translation.
void MainWindow::enableGUIEvents()
{
    inhibit_setting_save = false;
}

//------------------------ Apply GUI settings to different modules.
void MainWindow::applyGUISettings()
{
    // First, switch some things in GUI.
    // Toggle live playback indicator.
    if(ui->cbxLivePB->isChecked()==false)
    {
        ui->indLivePB->setEnabled(false);
    }
    else
    {
        ui->indLivePB->setEnabled(true);
    }
    // Toggle frame drop indicator.
    if(ui->cbxFrameDropout->isChecked()==false)
    {
        ui->lblFrameDrop->setEnabled(false);
        ui->lcdFrameDrop->setEnabled(false);
    }
    else
    {
        ui->lblFrameDrop->setEnabled(true);
        ui->lcdFrameDrop->setEnabled(true);
    }
    // Toggle duplicated line indicator.
    if(ui->cbxLineDuplicate->isChecked()==false)
    {
        ui->lblDupErr->setEnabled(false);
        ui->lcdDupErr->setEnabled(false);
    }
    else
    {
        ui->lblDupErr->setEnabled(true);
        ui->lcdDupErr->setEnabled(true);
    }
    // Choose what to display according to selected PCM type.
    if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM1)
    {
        // Select pages for the format.
        ui->stcDecoderSettings->setCurrentIndex(LIST_TYPE_PCM1);
        ui->stcFrameAsm->setCurrentIndex(LIST_TYPE_PCM1);
        // Enable controls on showed pages, disable on others.
        ui->pgPCM1Settings->setEnabled(true);
        ui->pgPCM1Frame->setEnabled(true);
        ui->pgPCM16x0Settings->setEnabled(false);
        ui->pgPCM16x0Frame->setEnabled(false);
        ui->pgSTC007Settings->setEnabled(false);
        ui->pgSTC007Frame->setEnabled(false);
        ui->pgM2Settings->setEnabled(false);
        ui->pgM2Frame->setEnabled(false);
        // Show/hide stats indicators.
        ui->lblBadStitch->setEnabled(false);
        ui->lcdBadStitch->setEnabled(false);
        ui->lblPCorr->setEnabled(false);
        ui->lcdPCorr->setEnabled(false);
        ui->lblQCorr->setEnabled(false);
        ui->lcdQCorr->setEnabled(false);
        ui->lblCWDCorr->setEnabled(false);
        ui->lcdCWDCorr->setEnabled(false);
        ui->lblBroken->setEnabled(false);
        ui->lcdBroken->setEnabled(false);
    }
    else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM16X0)
    {
        // Select pages for the format.
        ui->stcDecoderSettings->setCurrentIndex(LIST_TYPE_PCM16X0);
        ui->stcFrameAsm->setCurrentIndex(LIST_TYPE_PCM16X0);
        // Enable controls on showed pages, disable on others.
        ui->pgPCM1Settings->setEnabled(false);
        ui->pgPCM1Frame->setEnabled(false);
        ui->pgPCM16x0Settings->setEnabled(true);
        ui->pgPCM16x0Frame->setEnabled(true);
        ui->pgSTC007Settings->setEnabled(false);
        ui->pgSTC007Frame->setEnabled(false);
        ui->pgM2Settings->setEnabled(false);
        ui->pgM2Frame->setEnabled(false);
        // Show/hide stats indicators.
        ui->lblBadStitch->setEnabled(false);
        ui->lcdBadStitch->setEnabled(false);
        if(ui->lbxPCM16x0ECC->currentIndex()==LIST_PCM16X0_ECC_NONE)
        {
            ui->lblPCorr->setEnabled(false);
            ui->lcdPCorr->setEnabled(false);
            ui->lblQCorr->setEnabled(false);
            ui->lcdQCorr->setEnabled(false);
            ui->lblCWDCorr->setEnabled(false);
            ui->lcdCWDCorr->setEnabled(false);
        }
        else
        {
            ui->lblPCorr->setEnabled(true);
            ui->lcdPCorr->setEnabled(true);
            ui->lblQCorr->setEnabled(false);
            ui->lcdQCorr->setEnabled(false);
            ui->lblCWDCorr->setEnabled(false);
            ui->lcdCWDCorr->setEnabled(false);
        }
        ui->lblBroken->setEnabled(true);
        ui->lcdBroken->setEnabled(true);
    }
    else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_STC007)
    {
        // Select pages for the format.
        ui->stcDecoderSettings->setCurrentIndex(LIST_TYPE_STC007);
        ui->stcFrameAsm->setCurrentIndex(LIST_TYPE_STC007);
        // Enable controls on showed pages, disable on others.
        ui->pgPCM1Settings->setEnabled(false);
        ui->pgPCM1Frame->setEnabled(false);
        ui->pgPCM16x0Settings->setEnabled(false);
        ui->pgPCM16x0Frame->setEnabled(false);
        ui->pgSTC007Settings->setEnabled(true);
        ui->pgSTC007Frame->setEnabled(true);
        ui->pgM2Settings->setEnabled(false);
        ui->pgM2Frame->setEnabled(false);
        // Show/hide stats indicators.
        ui->lblBadStitch->setEnabled(true);
        ui->lcdBadStitch->setEnabled(true);
        if(ui->lbxSTC007ECC->currentIndex()==LIST_STC007_ECC_FULL)
        {
            ui->lblPCorr->setEnabled(true);
            ui->lcdPCorr->setEnabled(true);
            ui->lblQCorr->setEnabled(true);
            ui->lcdQCorr->setEnabled(true);
        }
        else if(ui->lbxSTC007ECC->currentIndex()==LIST_STC007_ECC_PARITY)
        {
            ui->lblPCorr->setEnabled(true);
            ui->lcdPCorr->setEnabled(true);
            ui->lblQCorr->setEnabled(false);
            ui->lcdQCorr->setEnabled(false);
        }
        else
        {
            ui->lblPCorr->setEnabled(false);
            ui->lcdPCorr->setEnabled(false);
            ui->lblQCorr->setEnabled(false);
            ui->lcdQCorr->setEnabled(false);
        }
        if(ui->lbxSTC007CWD->currentIndex()==LIST_STC007_CWD_DIS)
        {
            ui->lblCWDCorr->setEnabled(false);
            ui->lcdCWDCorr->setEnabled(false);
        }
        else if(ui->lbxSTC007ECC->currentIndex()!=LIST_STC007_ECC_NONE)
        {
            ui->lblCWDCorr->setEnabled(true);
            ui->lcdCWDCorr->setEnabled(true);
        }
        else
        {
            ui->lblCWDCorr->setEnabled(false);
            ui->lcdCWDCorr->setEnabled(false);
        }
        ui->lblBroken->setEnabled(true);
        ui->lcdBroken->setEnabled(true);
    }
    else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_M2)
    {
        // Select pages for the format.
        ui->stcDecoderSettings->setCurrentIndex(LIST_TYPE_M2);
        ui->stcFrameAsm->setCurrentIndex(LIST_TYPE_M2);
        // Enable controls on showed pages, disable on others.
        ui->pgPCM1Settings->setEnabled(false);
        ui->pgPCM1Frame->setEnabled(false);
        ui->pgPCM16x0Settings->setEnabled(false);
        ui->pgPCM16x0Frame->setEnabled(false);
        ui->pgSTC007Settings->setEnabled(false);
        ui->pgSTC007Frame->setEnabled(false);
        ui->pgM2Settings->setEnabled(true);
        ui->pgM2Frame->setEnabled(true);
        // Show/hide stats indicators.
        ui->lblBadStitch->setEnabled(true);
        ui->lcdBadStitch->setEnabled(true);
        if(ui->lbxM2ECC->currentIndex()==LIST_M2_ECC_FULL)
        {
            ui->lblPCorr->setEnabled(true);
            ui->lcdPCorr->setEnabled(true);
            ui->lblQCorr->setEnabled(true);
            ui->lcdQCorr->setEnabled(true);
        }
        else if(ui->lbxM2ECC->currentIndex()==LIST_M2_ECC_PARITY)
        {
            ui->lblPCorr->setEnabled(true);
            ui->lcdPCorr->setEnabled(true);
            ui->lblQCorr->setEnabled(false);
            ui->lcdQCorr->setEnabled(false);
        }
        else
        {
            ui->lblPCorr->setEnabled(false);
            ui->lcdPCorr->setEnabled(false);
            ui->lblQCorr->setEnabled(false);
            ui->lcdQCorr->setEnabled(false);
        }
        if(ui->lbxM2CWD->currentIndex()==LIST_M2_CWD_DIS)
        {
            ui->lblCWDCorr->setEnabled(false);
            ui->lcdCWDCorr->setEnabled(false);
        }
        else if(ui->lbxM2ECC->currentIndex()!=LIST_M2_ECC_NONE)
        {
            ui->lblCWDCorr->setEnabled(true);
            ui->lcdCWDCorr->setEnabled(true);
        }
        else
        {
            ui->lblCWDCorr->setEnabled(false);
            ui->lcdCWDCorr->setEnabled(false);
        }
        ui->lblBroken->setEnabled(true);
        ui->lcdBroken->setEnabled(true);
    }
    // Choose what counter display for sample errors.
    if((ui->lbxDropAction->currentIndex()==LIST_DOA_MUTE_WORD)||(ui->lbxDropAction->currentIndex()==LIST_DOA_MUTE_BLOCK))
    {
        ui->lblMask->setEnabled(false);
        ui->lcdMask->setEnabled(false);
    }
    else if(ui->lbxDropAction->currentIndex()==LIST_DOA_SKIP)
    {
        ui->lblMask->setEnabled(false);
        ui->lcdMask->setEnabled(false);
    }
    else
    {
        ui->lblMask->setEnabled(true);
        ui->lcdMask->setEnabled(true);
    }
    // Finally, perform per-module settings applying.
    setVIPOptions();
    setLBOptions();
    setDIOptions();
    setAPOptions();
}

//------------------------ Read settings into GUI.
void MainWindow::readGUISettings()
{
    disableGUIEvents();

    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    settings_hdl.sync();
    // Read main window position from settings.
    settings_hdl.beginGroup("main_window");
    if(settings_hdl.contains("position")!=false)
    {
        // Set window position.
        this->setGeometry(settings_hdl.value("size").toRect());
        this->move(settings_hdl.value("position").toPoint());
    }
    settings_hdl.endGroup();

    settings_hdl.beginGroup("player");
    // Read video processing settings (and preset defaults if settings not found).
    ui->cbxLivePB->setChecked(settings_hdl.value("live_pb", true).toBool());
    ui->cbxWaveSave->setChecked(settings_hdl.value("wave_save", true).toBool());
    ui->cbxLineDuplicate->setChecked(settings_hdl.value("line_duplicate", true).toBool());
    ui->cbxFrameDropout->setChecked(settings_hdl.value("drop_detect", true).toBool());
    ui->cbxFrameStep->setChecked(settings_hdl.value("framestep", false).toBool());
    if(settings_hdl.contains("path")!=false)
    {
        QDir::setCurrent(settings_hdl.value("path", QDir::home().absolutePath()).toString());
    }
    settings_hdl.endGroup();

    settings_hdl.beginGroup("decoder");
    // Read decoder settings (and preset defaults if settings not found).
    ui->lbxBinQuality->setCurrentIndex(settings_hdl.value("bin_quality", LIST_BQ_FAST).toInt());
    ui->lbxPCMType->setCurrentIndex(settings_hdl.value("pcm_type", LIST_TYPE_STC007).toInt());
    ui->lbxPCM1FieldOrder->setCurrentIndex(settings_hdl.value("pcm1_field_order", LIST_PCM1_FO_TFF).toInt());
    ui->cbxPCM1Offset->setChecked(settings_hdl.value("pcm1_auto_offset", true).toBool());
    ui->sldPCM1OddOfs->setValue(settings_hdl.value("pcm1_odd_offset", 0).toInt());
    ui->sldPCM1EvenOfs->setValue(settings_hdl.value("pcm1_even_offset", -1).toInt());
    ui->lbxPCM16x0Format->setCurrentIndex(settings_hdl.value("pcm16x0_fmt", LIST_PCM16X0_FMT_SI).toInt());
    ui->lbxPCM16x0FieldOrder->setCurrentIndex(settings_hdl.value("pcm16x0_field_order", LIST_PCM16X0_FO_TFF).toInt());
    ui->lbxPCM16x0ECC->setCurrentIndex(settings_hdl.value("pcm16x0_ecc", LIST_PCM16X0_ECC_PARITY).toInt());
    ui->lbxPCM16x0SampleRate->setCurrentIndex(settings_hdl.value("pcm16x0_sample_rate", LIST_PCM16X0_SRATE_AUTO).toInt());
    ui->lbxSTC007VidStandard->setCurrentIndex(settings_hdl.value("stc007_video_std", LIST_STC007_VID_AUTO).toInt());
    ui->lbxSTC007FieldOrder->setCurrentIndex(settings_hdl.value("stc007_field_order", LIST_STC007_FO_AUTO).toInt());
    ui->lbxSTC007ECC->setCurrentIndex(settings_hdl.value("stc007_ecc", LIST_STC007_ECC_FULL).toInt());
    ui->lbxSTC007CWD->setCurrentIndex(settings_hdl.value("stc007_cwd", LIST_STC007_CWD_EN).toInt());
    ui->lbxSTC007Resolution->setCurrentIndex(settings_hdl.value("stc007_resolution", LIST_STC007_RES_AUTO).toInt());
    ui->lbxSTC007SampleRate->setCurrentIndex(settings_hdl.value("stc007_sample_rate", LIST_STC007_SRATE_AUTO).toInt());
    ui->lbxM2VidStandard->setCurrentIndex(settings_hdl.value("m2_video_std", LIST_M2_VID_AUTO).toInt());
    ui->lbxM2FieldOrder->setCurrentIndex(settings_hdl.value("m2_field_order", LIST_M2_FO_AUTO).toInt());
    ui->lbxM2ECC->setCurrentIndex(settings_hdl.value("m2_ecc", LIST_M2_ECC_FULL).toInt());
    ui->lbxM2CWD->setCurrentIndex(settings_hdl.value("m2_cwd", LIST_M2_CWD_EN).toInt());
    ui->lbxM2SampleRate->setCurrentIndex(settings_hdl.value("m2_sample_rate", LIST_M2_SRATE_AUTO).toInt());
    ui->lbxDropAction->setCurrentIndex(settings_hdl.value("drop_action", LIST_DOA_INTER_WORD).toInt());
    settings_hdl.endGroup();

    // Read debug menu settings.
    settings_hdl.beginGroup("debug_main");
    ui->actGenSettings->setChecked(settings_hdl.value("size", false).toBool());
    ui->actGenProcess->setChecked(settings_hdl.value("process", false).toBool());
    settings_hdl.endGroup();

    settings_hdl.beginGroup("debug_video");
    ui->actVIPSettings->setChecked(settings_hdl.value("settings", false).toBool());
    ui->actVIPProcess->setChecked(settings_hdl.value("process", false).toBool());
    ui->actVIPFrame->setChecked(settings_hdl.value("frames", false).toBool());
    ui->actVIPLine->setChecked(settings_hdl.value("lines", false).toBool());
    settings_hdl.endGroup();

    settings_hdl.beginGroup("debug_binarizer");
    ui->actLBSettings->setChecked(settings_hdl.value("settings", false).toBool());
    ui->actLBProcess->setChecked(settings_hdl.value("process", false).toBool());
    ui->actLBBright->setChecked(settings_hdl.value("brightness", false).toBool());
    ui->actLBSweep->setChecked(settings_hdl.value("sweep", false).toBool());
    ui->actLBCoords->setChecked(settings_hdl.value("coordinates", false).toBool());
    ui->actLBRawBin->setChecked(settings_hdl.value("raw_bin", false).toBool());
    ui->actGenLines->setChecked(settings_hdl.value("dump_lines", false).toBool());
    settings_hdl.endGroup();

    settings_hdl.beginGroup("debug_deinterleaver");
    ui->actDISettings->setChecked(settings_hdl.value("settings", false).toBool());
    ui->actDIProcess->setChecked(settings_hdl.value("process", false).toBool());
    ui->actDIErrorCorr->setChecked(settings_hdl.value("err_corr", false).toBool());
    ui->actDIDeinterleave->setChecked(settings_hdl.value("deinterleave", false).toBool());
    ui->actDITrim->setChecked(settings_hdl.value("trimming", false).toBool());
    ui->actDIPadding->setChecked(settings_hdl.value("padding", false).toBool());
    ui->actDIFields->setChecked(settings_hdl.value("pad_fields", false).toBool());
    ui->actDIBlocks->setChecked(settings_hdl.value("pad_blocks", false).toBool());
    ui->actDIAssembly->setChecked(settings_hdl.value("assembly", false).toBool());
    ui->actGenBlocks->setChecked(settings_hdl.value("dump_blocks", false).toBool());
    settings_hdl.endGroup();

    settings_hdl.beginGroup("debug_audio");
    ui->actAPSettings->setChecked(settings_hdl.value("settings", false).toBool());
    ui->actAPProcess->setChecked(settings_hdl.value("process", false).toBool());
    ui->actAPDropAct->setChecked(settings_hdl.value("drop_action", false).toBool());
    ui->actAPBuffer->setChecked(settings_hdl.value("buffer", false).toBool());
    ui->actAPFile->setChecked(settings_hdl.value("file", false).toBool());
    ui->actAPLive->setChecked(settings_hdl.value("live", false).toBool());
    settings_hdl.endGroup();

    qInfo()<<"[M] Loaded GUI parameterts";

    ui->edtSTC007SetOrder->setText(LIST_ORDER_UNK);
    ui->edtSTC007LineCnt->setText(LIST_VIDSTD_UNK);

    set_pcm_type = 0xFF;

    // Apply debug menu settings to modules.
    setMainLogMode();
    setVIPLogMode();
    setLBLogMode();
    setDILogMode();
    setAPLogMode();

    // Apply settings to all modules.
    applyGUISettings();

    enableGUIEvents();
}

//------------------------ Find and return coordinates from video tracking history.
FrameBinDescriptor MainWindow::getBinDescriptorByFrameNo(uint32_t frame_num)
{
    FrameBinDescriptor bin_stats;
    for(uint8_t idx=0;idx<TRACKING_BUF_LEN;idx++)
    {
        if(stat_tracking_arr[idx].frame_id==frame_num)
        {
            bin_stats = stat_tracking_arr[idx];
            break;
        }
    }
    return bin_stats;
}

//------------------------ Exit application.
void MainWindow::exitAction()
{
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[M] Exiting...";
    }
    emit doPlayStop();
    emit doPlayUnload();
    this->close();
}

//------------------------ User request to set GUI language.
void MainWindow::setLang(QAction *in_act)
{
    setGUILanguage(in_act->data().toString());
}

//------------------------ Save new main window position and size in setting storage.
void MainWindow::updateWindowPosition()
{
    qInfo()<<"[M] Position saved"<<this->geometry()<<this->pos();
    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    settings_hdl.beginGroup("main_window");
    settings_hdl.setValue("size", this->geometry());
    settings_hdl.setValue("position", this->pos());
    settings_hdl.endGroup();
}

//------------------------ Display window with an error, stop playback.
void MainWindow::displayErrorMessage(QString in_string)
{
    // Stop playback if any.
    emit doPlayStop();
    // Display error message.
    QMessageBox::critical(this, tr("Ошибка"), in_string);
}

//------------------------ User request to open a new source.
void MainWindow::loadVideo()
{
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[M] Creating 'Open video' dialog...";
    }
    QString file_path;

    file_path = QFileDialog::getOpenFileName(this,
                                             tr("Открыть видео с PCM"),
                                             QDir::currentPath(),
                                             tr("Видео-файлы (*.avi *.mkv *.mov *.mpg *.mp4 *.wmv)"),
                                             0,
                                             QFileDialog::HideNameFilterDetails);
    if(file_path.isNull()==false)
    {
        //if((log_level&LOG_PROCESS)!=0)
        {
            qInfo()<<"[M] File selected:"<<file_path;
        }

        QFileInfo in_file(file_path);

        // Signal about new source file.
        emit newTargetPath(file_path);

        // Block playback and source control and wait for response from [VIN_worker].
        ui->btnOpen->setEnabled(false);
        ui->btnPlay->setEnabled(false);
        ui->btnPlay->setChecked(false);
        ui->btnPlay->repaint();
        ui->btnPause->setEnabled(false);
        ui->btnPause->setChecked(false);
        ui->btnPause->repaint();
        ui->cbxWaveSave->setEnabled(false);

        // Set current application directory to source file's path.
        QDir::setCurrent(in_file.absolutePath());
        // Save that path in setting storage.
        QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
        settings_hdl.beginGroup("player");
        settings_hdl.setValue("path", in_file.absolutePath());
        settings_hdl.endGroup();
    }
}

//------------------------ User request to close the source and free resources.
void MainWindow::unloadSource()
{
    emit doPlayUnload();
}

//------------------------ User request to start/stop video playback/decode.
void MainWindow::usrPlayStop()
{
    ui->btnPlay->setEnabled(false);
    ui->btnPause->setEnabled(false);
    ui->cbxWaveSave->setEnabled(false);
    if(v_decoder_state==VDEC_STOP)
    {
        qDebug()<<"[M] Play request";
        emit doPlayStart();
    }
    else
    {
        qDebug()<<"[M] Stop request";
        emit doPlayStop();
    }
}

//------------------------ User request to pause video playback/decode.
void MainWindow::usrPause()
{
    ui->btnPause->setEnabled(false);
    emit doPlayPause();
}

//------------------------ Save settings for GUI options.
void MainWindow::updateGUISettings()
{
    // Check if settings updating is disabled due to GUI switching.
    if(inhibit_setting_save!=false) return;
    // Open settings storage.
    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    settings_hdl.beginGroup("decoder");
    // Check if PCM type was changed (not saved in settings yet)
    // and vizualizer windows should be re-opened.
    if(ui->lbxPCMType->currentIndex()!=settings_hdl.value("pcm_type", LIST_TYPE_STC007).toInt())
    {
        // Re-open vizualizer windows.
        reopenVisualizers();
        // Reset frame assembling data.
        frame_asm_pcm1.clear();
        frame_asm_pcm16x0.clear();
        frame_asm_stc007.clear();
        frame_asm_m2.clear();
    }
    settings_hdl.endGroup();
    // Save GUI settings.
    settings_hdl.beginGroup("player");
    settings_hdl.setValue("live_pb", ui->cbxLivePB->isChecked());
    settings_hdl.setValue("wave_save", ui->cbxWaveSave->isChecked());
    settings_hdl.setValue("line_duplicate", ui->cbxLineDuplicate->isChecked());
    settings_hdl.setValue("drop_detect", ui->cbxFrameDropout->isChecked());
    settings_hdl.setValue("framestep", ui->cbxFrameStep->isChecked());
    settings_hdl.endGroup();
    settings_hdl.beginGroup("decoder");
    settings_hdl.setValue("bin_quality", ui->lbxBinQuality->currentIndex());
    settings_hdl.setValue("pcm_type", ui->lbxPCMType->currentIndex());
    settings_hdl.setValue("pcm1_field_order", ui->lbxPCM1FieldOrder->currentIndex());
    settings_hdl.setValue("pcm1_auto_offset", ui->cbxPCM1Offset->isChecked());
    settings_hdl.setValue("pcm1_odd_offset", ui->sldPCM1OddOfs->value());
    settings_hdl.setValue("pcm1_even_offset", ui->sldPCM1EvenOfs->value());
    settings_hdl.setValue("pcm16x0_fmt", ui->lbxPCM16x0Format->currentIndex());
    settings_hdl.setValue("pcm16x0_field_order", ui->lbxPCM16x0FieldOrder->currentIndex());
    settings_hdl.setValue("pcm16x0_ecc", ui->lbxPCM16x0ECC->currentIndex());
    settings_hdl.setValue("pcm16x0_sample_rate", ui->lbxPCM16x0SampleRate->currentIndex());
    settings_hdl.setValue("stc007_video_std", ui->lbxSTC007VidStandard->currentIndex());
    settings_hdl.setValue("stc007_field_order", ui->lbxSTC007FieldOrder->currentIndex());
    settings_hdl.setValue("stc007_ecc", ui->lbxSTC007ECC->currentIndex());
    settings_hdl.setValue("stc007_cwd", ui->lbxSTC007CWD->currentIndex());
    settings_hdl.setValue("stc007_resolution", ui->lbxSTC007Resolution->currentIndex());
    settings_hdl.setValue("stc007_sample_rate", ui->lbxSTC007SampleRate->currentIndex());
    settings_hdl.setValue("m2_video_std", ui->lbxM2VidStandard->currentIndex());
    settings_hdl.setValue("m2_field_order", ui->lbxM2FieldOrder->currentIndex());
    settings_hdl.setValue("m2_ecc", ui->lbxM2ECC->currentIndex());
    settings_hdl.setValue("m2_cwd", ui->lbxM2CWD->currentIndex());
    settings_hdl.setValue("m2_sample_rate", ui->lbxM2SampleRate->currentIndex());
    settings_hdl.setValue("drop_action", ui->lbxDropAction->currentIndex());
    settings_hdl.endGroup();

    // Apply all settings from the GUI to all modules.
    applyGUISettings();
}

//------------------------ Clear decoding stats.
void MainWindow::clearStat()
{
    frame_asm_pcm1.clear();
    frame_asm_pcm16x0.clear();
    frame_asm_stc007.clear();
    frame_asm_m2.clear();

    stat_dbg_index = 0;
    for(uint8_t i=0;i<DBG_AVG_LEN;i++)
    {
        stat_debug_avg[i] = 0;
    }
    ui->pgrDebug->setValue(0);
    ui->pgrDebug->setMaximum(1);

    while(stat_tracking_arr.empty()==false)
    {
        stat_tracking_arr.pop();
    }
    stat_video_tracking.clear();
    flag_bad_stitch_cnt = flag_p_corr_cnt = flag_q_corr_cnt = flag_cwd_corr_cnt = flag_broken_cnt = flag_dropout_cnt = 0;
    stat_ref_level = 0;
    stat_total_frame_cnt = stat_read_frame_cnt = stat_drop_frame_cnt = 0;
    stat_no_pcm_cnt = stat_crc_err_cnt = stat_dup_err_cnt = 0;
    stat_bad_stitch_cnt = 0;
    stat_p_fix_cnt = stat_q_fix_cnt = stat_cwd_fix_cnt = 0;
    stat_broken_block_cnt = stat_drop_block_cnt = 0;
    stat_drop_sample_cnt = stat_mask_cnt = 0;
    stat_processed_frame_cnt = 0;
    stat_line_cnt = 0;
    stat_min_di_time = 0xFFFFFFFF;
    stat_max_di_time = 0;
    vu_left = vu_right = 0;
    ui->lcdTotalNumber->display(0);
    ui->lcdReadFrames->display(0);
    ui->lcdRefLevel->display(0);
    ui->lcdNoPCM->display(0);
    ui->lcdCrcErr->display(0);
    ui->lcdDupErr->display(0);
    ui->lcdBadStitch->display(0);
    ui->lcdPCorr->display(0);
    ui->lcdQCorr->display(0);
    ui->lcdCWDCorr->display(0);
    ui->lcdDropout->display(0);
    ui->lcdMask->display(0);
    ui->lcdProcessedFrames->display(0);
}

//------------------------ Update PCM-1 vertical offset indicators.
void MainWindow::sliderDisplayUpdate()
{
    ui->edtPCM1OddOfs->setText(QString::number(ui->sldPCM1OddOfs->sliderPosition()));
    ui->edtPCM1EvenOfs->setText(QString::number(ui->sldPCM1EvenOfs->sliderPosition()));
}

//------------------------ Confirm and reset decoder settings.
void MainWindow::resetOptDecoder()
{
    QMessageBox usrConfirm(this);
    usrConfirm.setWindowTitle(tr("Сброс настроек"));
    usrConfirm.setText(tr("Сброс настроек декодера"));
    usrConfirm.setInformativeText(tr("Вернуть настройки к состоянию по умолчанию?"));
    usrConfirm.setIcon(QMessageBox::Question);
    usrConfirm.setStandardButtons(QMessageBox::Ok|QMessageBox::Cancel);
    usrConfirm.setDefaultButton(QMessageBox::Cancel);
    if(usrConfirm.exec()==QMessageBox::Ok)
    {
        QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
        // Remove decoder settings.
        settings_hdl.beginGroup("decoder");
        settings_hdl.remove("");
        settings_hdl.endGroup();
        // Reload default settings into GUI.
        readGUISettings();
    }
}

//------------------------ Confirm and reset visualizer windows positions.
void MainWindow::resetVisPositions()
{
    QMessageBox usrConfirm(this);
    usrConfirm.setWindowTitle(tr("Сброс расположения"));
    usrConfirm.setText(tr("Сброс расположения окон визуализации"));
    usrConfirm.setInformativeText(tr("Сбросить положения окон визуализации?"));
    usrConfirm.setIcon(QMessageBox::Question);
    usrConfirm.setStandardButtons(QMessageBox::Ok|QMessageBox::Cancel);
    usrConfirm.setDefaultButton(QMessageBox::Cancel);
    if(usrConfirm.exec()==QMessageBox::Ok)
    {
        QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
        // Remove visualization windows positions.
        settings_hdl.beginGroup("vis_src");
        settings_hdl.remove("");
        settings_hdl.endGroup();
        settings_hdl.beginGroup("vis_bin");
        settings_hdl.remove("");
        settings_hdl.endGroup();
        settings_hdl.beginGroup("vis_assembled");
        settings_hdl.remove("");
        settings_hdl.endGroup();
        settings_hdl.beginGroup("vis_blocks");
        settings_hdl.remove("");
        settings_hdl.endGroup();
        // Re-open visualization windows.
        reopenVisualizers();
    }
}

//------------------------ Confirm and reset all settings.
void MainWindow::resetFull()
{
    QMessageBox usrConfirm(this);
    usrConfirm.setWindowTitle(tr("Сброс настроек"));
    usrConfirm.setText(tr("Сброс ВСЕХ настроек"));
    usrConfirm.setInformativeText(tr("Вернуть ВСЕ настройки программы к состоянию по умолчанию?"));
    usrConfirm.setIcon(QMessageBox::Warning);
    usrConfirm.setStandardButtons(QMessageBox::Ok|QMessageBox::Cancel);
    usrConfirm.setDefaultButton(QMessageBox::Cancel);
    if(usrConfirm.exec()==QMessageBox::Ok)
    {
        QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
        // Remove ALL settings.
        settings_hdl.clear();
        // Reload default settings into GUI.
        readGUISettings();
        // Reset all fine settings.
        emit newFineReset();
    }
}

//------------------------ Display "About" window.
void MainWindow::showAbout()
{
    about_wnd about_dlg(this);
    about_dlg.exec();
}

//------------------------ Display video capture selection dialog.
void MainWindow::showCaptureSelector()
{
    //captureSelectDialog = new capt_sel(this);
    capt_sel captureSelectDialog(this);
    captureSelectDialog.exec();
    //captureSelectDialog->deleteLater();
}

//------------------------ Display video processor fine settings dialog.
void MainWindow::showVidInFineSettings()
{
    fine_vidin_set vipFineSetDialog(this);
    //connect(VIN_worker, SIGNAL(guiUpdFineLineSkip(bool)), vipFineSetDialog, SLOT(newSkipLines(bool)));
    connect(VIN_worker, SIGNAL(guiUpdFineSettings(vid_preset_t)), &vipFineSetDialog, SLOT(newSettings(vid_preset_t)));
    connect(this, SIGNAL(guiUpdFineDrawDeint(bool)), &vipFineSetDialog, SLOT(newDrawDeint(bool)));
    connect(&vipFineSetDialog, SIGNAL(setFineDefaults()), VIN_worker, SLOT(setDefaultFineSettings()));
    connect(&vipFineSetDialog, SIGNAL(requestFineCurrent()), VIN_worker, SLOT(requestCurrentFineSettings()));
    connect(&vipFineSetDialog, SIGNAL(setFineCurrent(vid_preset_t)), VIN_worker, SLOT(setFineSettings(vid_preset_t)));
    connect(&vipFineSetDialog, SIGNAL(setFineDefaults()), this, SLOT(setDefaultFineSettings()));
    connect(&vipFineSetDialog, SIGNAL(requestFineCurrent()), this, SLOT(requestCurrentFineSettings()));
    connect(&vipFineSetDialog, SIGNAL(setDrawDeint(bool)), this, SLOT(setFineDrawDeint(bool)));
    vipFineSetDialog.exec();
}

//------------------------ Display binarizator fine settings dialog.
void MainWindow::showBinFineSettings()
{
    fine_bin_set binFineSetDialog(this);
    connect(V2D_worker, SIGNAL(guiUpdFineSettings(bin_preset_t)), &binFineSetDialog, SLOT(newSettings(bin_preset_t)));
    connect(&binFineSetDialog, SIGNAL(setFineDefaults()), V2D_worker, SLOT(setDefaultFineSettings()));
    connect(&binFineSetDialog, SIGNAL(requestFineCurrent()), V2D_worker, SLOT(requestCurrentFineSettings()));
    connect(&binFineSetDialog, SIGNAL(setFineCurrent(bin_preset_t)), V2D_worker, SLOT(setFineSettings(bin_preset_t)));
    binFineSetDialog.exec();
}

//------------------------ Display deinterleaver fine settings dialog.
void MainWindow::showDeintFineSettings()
{
    fine_deint_set deintFineSetDialog(this);
    if(set_pcm_type==LIST_TYPE_PCM1)
    {
        connect(L2B_PCM1_worker, SIGNAL(guiUpdFineUseECC(bool)), &deintFineSetDialog, SLOT(newUseECC(bool)));

        connect(&deintFineSetDialog, SIGNAL(setFineDefaults()), L2B_PCM1_worker, SLOT(setDefaultFineSettings()));
        connect(&deintFineSetDialog, SIGNAL(requestFineCurrent()), L2B_PCM1_worker, SLOT(requestCurrentFineSettings()));
        connect(&deintFineSetDialog, SIGNAL(setUseECC(bool)), L2B_PCM1_worker, SLOT(setFineUseECC(bool)));
    }
    else if(set_pcm_type==LIST_TYPE_PCM16X0)
    {
        connect(L2B_PCM16X0_worker, SIGNAL(guiUpdFineUseECC(bool)), &deintFineSetDialog, SLOT(newUseECC(bool)));
        connect(L2B_PCM16X0_worker, SIGNAL(guiUpdFineMaskSeams(bool)), &deintFineSetDialog, SLOT(newMaskSeams(bool)));
        connect(L2B_PCM16X0_worker, SIGNAL(guiUpdFineBrokeMask(uint8_t)), &deintFineSetDialog, SLOT(newBrokeMask(uint8_t)));

        connect(&deintFineSetDialog, SIGNAL(setFineDefaults()), L2B_PCM16X0_worker, SLOT(setDefaultFineSettings()));
        connect(&deintFineSetDialog, SIGNAL(requestFineCurrent()), L2B_PCM16X0_worker, SLOT(requestCurrentFineSettings()));
        connect(&deintFineSetDialog, SIGNAL(setUseECC(bool)), L2B_PCM16X0_worker, SLOT(setFineUseECC(bool)));
        connect(&deintFineSetDialog, SIGNAL(setMaskSeams(bool)), L2B_PCM16X0_worker, SLOT(setFineMaskSeams(bool)));
        connect(&deintFineSetDialog, SIGNAL(setBrokeMask(uint8_t)), L2B_PCM16X0_worker, SLOT(setFineBrokeMask(uint8_t)));
    }
    else if((set_pcm_type==LIST_TYPE_STC007)||(set_pcm_type==LIST_TYPE_M2))
    {
        connect(L2B_STC007_worker, SIGNAL(guiUpdFineMaxUnch14(uint8_t)), &deintFineSetDialog, SLOT(newMaxUnchecked14(uint8_t)));
        connect(L2B_STC007_worker, SIGNAL(guiUpdFineMaxUnch16(uint8_t)), &deintFineSetDialog, SLOT(newMaxUnchecked16(uint8_t)));
        connect(L2B_STC007_worker, SIGNAL(guiUpdFineUseECC(bool)), &deintFineSetDialog, SLOT(newUseECC(bool)));
        connect(L2B_STC007_worker, SIGNAL(guiUpdFineTopLineFix(bool)), &deintFineSetDialog, SLOT(newInsertLine(bool)));
        connect(L2B_STC007_worker, SIGNAL(guiUpdFineMaskSeams(bool)), &deintFineSetDialog, SLOT(newMaskSeams(bool)));
        connect(L2B_STC007_worker, SIGNAL(guiUpdFineBrokeMask(uint8_t)), &deintFineSetDialog, SLOT(newBrokeMask(uint8_t)));

        connect(&deintFineSetDialog, SIGNAL(setFineDefaults()), L2B_STC007_worker, SLOT(setDefaultFineSettings()));
        connect(&deintFineSetDialog, SIGNAL(requestFineCurrent()), L2B_STC007_worker, SLOT(requestCurrentFineSettings()));
        connect(&deintFineSetDialog, SIGNAL(setMaxUnchecked14(uint8_t)), L2B_STC007_worker, SLOT(setFineMaxUnch14(uint8_t)));
        connect(&deintFineSetDialog, SIGNAL(setMaxUnchecked16(uint8_t)), L2B_STC007_worker, SLOT(setFineMaxUnch16(uint8_t)));
        connect(&deintFineSetDialog, SIGNAL(setUseECC(bool)), L2B_STC007_worker, SLOT(setFineUseECC(bool)));
        connect(&deintFineSetDialog, SIGNAL(setInsertLine(bool)), L2B_STC007_worker, SLOT(setFineTopLineFix(bool)));
        connect(&deintFineSetDialog, SIGNAL(setMaskSeams(bool)), L2B_STC007_worker, SLOT(setFineMaskSeams(bool)));
        connect(&deintFineSetDialog, SIGNAL(setBrokeMask(uint8_t)), L2B_STC007_worker, SLOT(setFineBrokeMask(uint8_t)));
    }
    deintFineSetDialog.exec();
}

//------------------------ Receive request for fine settings reset from video processor fine settings dialog.
void MainWindow::setDefaultFineSettings()
{
    src_draw_deint = false;
    emit guiUpdFineDrawDeint(src_draw_deint);
}

//------------------------ Display deinterleaver fine settings dialog.
void MainWindow::requestCurrentFineSettings()
{
    emit guiUpdFineDrawDeint(src_draw_deint);
}

//------------------------ Save new fine setting for source drawing deinterlacing.
void MainWindow::setFineDrawDeint(bool in_flag)
{
    src_draw_deint = in_flag;
    reopenVisSource();
    emit guiUpdFineDrawDeint(src_draw_deint);
}

//------------------------ Display source visualization window.
void MainWindow::showVisSource(bool is_checked)
{
    if(is_checked!=false)
    {
        QThread *vis_thread;

        vis_thread = NULL;
        vis_thread = new QThread;

        RenderPCM *renderSource = new RenderPCM();
        renderSource->moveToThread(vis_thread);

        visuSource = new frame_vis(this);
        visuSource->setSettingsLabel("vis_src");
        visuSource->setTitle(TITLE_RENDER_SOURCE);

        connect(vis_thread, SIGNAL(started()), renderSource, SLOT(dumpThreadDebug()));
        connect(vis_thread, SIGNAL(started()), visuSource, SLOT(show()));

        connect(vis_thread, SIGNAL(finished()), renderSource, SLOT(deleteLater()));
        connect(vis_thread, SIGNAL(finished()), vis_thread, SLOT(deleteLater()));

        connect(visuSource, SIGNAL(finished(int)), vis_thread, SLOT(quit()));
        connect(visuSource, SIGNAL(finished(int)), visuSource, SLOT(deleteLater()));
        connect(visuSource, SIGNAL(finished(int)), this, SLOT(hideVisSource()));

        connect(this, SIGNAL(aboutToExit()), visuSource, SLOT(close()));

        renderSource->setLivePlay(ui->cbxLivePB->isChecked());
        connect(ui->cbxLivePB, SIGNAL(clicked(bool)), renderSource, SLOT(setLivePlay(bool)));
        connect(this, SIGNAL(newVideoStandard(uint8_t)), renderSource, SLOT(setFrameTime(uint8_t)));
        connect(VIN_worker, SIGNAL(newFrame(uint16_t,uint16_t)), renderSource, SLOT(startNewFrame(uint16_t,uint16_t)));
        if(src_draw_deint==false)
        {
            connect(VIN_worker, SIGNAL(newLine(VideoLine)), renderSource, SLOT(renderNewLineInOrder(VideoLine)));
        }
        else
        {
            connect(VIN_worker, SIGNAL(newLine(VideoLine)), renderSource, SLOT(renderNewLine(VideoLine)));
        }
        connect(VIN_worker, SIGNAL(frameDecoded(uint32_t)), renderSource, SLOT(finishNewFrame(uint32_t)));
        connect(renderSource, SIGNAL(renderedFrame(QImage)), visuSource, SLOT(drawFrame(QImage)));
        connect(visuSource, SIGNAL(readyToDraw()), renderSource, SLOT(displayIsReady()));

        vis_thread->start(QThread::LowPriority);
    }
    else if(visuSource!=NULL)
    {
        visuSource->close();
    }
}

//------------------------ Display binarized visualization window.
void MainWindow::showVisBin(bool is_checked)
{
    if(is_checked!=false)
    {
        QThread *vis_thread;

        vis_thread = NULL;
        vis_thread = new QThread;

        RenderPCM *renderBin = new RenderPCM();
        renderBin->moveToThread(vis_thread);

        visuBin = new frame_vis(this);
        visuBin->setSettingsLabel("vis_bin");
        visuBin->setTitle(TITLE_RENDER_BIN);

        connect(vis_thread, SIGNAL(started()), renderBin, SLOT(dumpThreadDebug()));
        connect(vis_thread, SIGNAL(started()), visuBin, SLOT(show()));

        connect(vis_thread, SIGNAL(finished()), renderBin, SLOT(deleteLater()));
        connect(vis_thread, SIGNAL(finished()), vis_thread, SLOT(deleteLater()));

        connect(visuBin, SIGNAL(finished(int)), visuBin, SLOT(deleteLater()));
        connect(visuBin, SIGNAL(finished(int)), vis_thread, SLOT(quit()));
        connect(visuBin, SIGNAL(finished(int)), this, SLOT(hideVisBin()));

        connect(this, SIGNAL(aboutToExit()), visuBin, SLOT(close()));

        renderBin->setLivePlay(ui->cbxLivePB->isChecked());
        connect(ui->cbxLivePB, SIGNAL(clicked(bool)), renderBin, SLOT(setLivePlay(bool)));
        connect(this, SIGNAL(newVideoStandard(uint8_t)), renderBin, SLOT(setFrameTime(uint8_t)));
        connect(this, SIGNAL(newFrameBinarized(uint32_t)), renderBin, SLOT(prepareNewFrame(uint32_t)));

        if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM1)
        {
            renderBin->startPCM1Frame();
            connect(this, SIGNAL(retransmitBinLine(PCM1Line)), renderBin, SLOT(renderNewLine(PCM1Line)));
        }
        else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM16X0)
        {
            renderBin->startPCM1600Frame();
            connect(this, SIGNAL(retransmitBinLine(PCM16X0SubLine)), renderBin, SLOT(renderNewLine(PCM16X0SubLine)));
        }
        else if((ui->lbxPCMType->currentIndex()==LIST_TYPE_STC007)||(ui->lbxPCMType->currentIndex()==LIST_TYPE_M2))
        {
            renderBin->startSTC007NTSCFrame();
            renderBin->setLineCount(FrameAsmDescriptor::VID_UNKNOWN);
            connect(this, SIGNAL(retransmitBinLine(STC007Line)), renderBin, SLOT(renderNewLine(STC007Line)));
        }
        connect(renderBin, SIGNAL(renderedFrame(QImage)), visuBin, SLOT(drawFrame(QImage)));
        connect(visuBin, SIGNAL(readyToDraw()), renderBin, SLOT(displayIsReady()));

        vis_thread->start(QThread::LowPriority);
    }
    else if(visuBin!=NULL)
    {
        visuBin->close();
    }
}

//------------------------ Display re-assembled visualization window.
void MainWindow::showVisAssembled(bool is_checked)
{
    if(is_checked!=false)
    {
        QThread *vis_thread;

        vis_thread = NULL;
        vis_thread = new QThread;

        RenderPCM *renderAssembled = new RenderPCM();
        renderAssembled->moveToThread(vis_thread);

        visuAssembled = new frame_vis(this);
        visuAssembled->setSettingsLabel("vis_assembled");
        visuAssembled->setTitle(TITLE_RENDER_REASM);

        connect(vis_thread, SIGNAL(started()), renderAssembled, SLOT(dumpThreadDebug()));
        connect(vis_thread, SIGNAL(started()), visuAssembled, SLOT(show()));

        connect(vis_thread, SIGNAL(finished()), renderAssembled, SLOT(deleteLater()));
        connect(vis_thread, SIGNAL(finished()), vis_thread, SLOT(deleteLater()));

        connect(visuAssembled, SIGNAL(finished(int)), visuAssembled, SLOT(deleteLater()));
        connect(visuAssembled, SIGNAL(finished(int)), vis_thread, SLOT(quit()));
        connect(visuAssembled, SIGNAL(finished(int)), this, SLOT(hideVisAssembled()));

        connect(this, SIGNAL(aboutToExit()), visuAssembled, SLOT(close()));

        renderAssembled->setLivePlay(ui->cbxLivePB->isChecked());
        connect(ui->cbxLivePB, SIGNAL(clicked(bool)), renderAssembled, SLOT(setLivePlay(bool)));
        connect(this, SIGNAL(newVideoStandard(uint8_t)), renderAssembled, SLOT(setFrameTime(uint8_t)));
        connect(this, SIGNAL(newVideoStandard(uint8_t)), renderAssembled, SLOT(setLineCount(uint8_t)));
        connect(this, SIGNAL(newFrameAssembled(uint32_t)), renderAssembled, SLOT(prepareNewFrame(uint32_t)));

        if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM1)
        {
            renderAssembled->startPCM1SubFrame();
            connect(this, SIGNAL(retransmitAsmLine(PCM1SubLine)), renderAssembled, SLOT(renderNewLine(PCM1SubLine)));
        }
        else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM16X0)
        {
            renderAssembled->startPCM1600Frame();
            connect(this, SIGNAL(retransmitAsmLine(PCM16X0SubLine)), renderAssembled, SLOT(renderNewLine(PCM16X0SubLine)));
        }
        else if((ui->lbxPCMType->currentIndex()==LIST_TYPE_STC007)||(ui->lbxPCMType->currentIndex()==LIST_TYPE_M2))
        {
            renderAssembled->startSTC007NTSCFrame();
            connect(this, SIGNAL(retransmitAsmLine(STC007Line)), renderAssembled, SLOT(renderNewLine(STC007Line)));
        }
        connect(renderAssembled, SIGNAL(renderedFrame(QImage)), visuAssembled, SLOT(drawFrame(QImage)));
        connect(visuAssembled, SIGNAL(readyToDraw()), renderAssembled, SLOT(displayIsReady()));

        vis_thread->start(QThread::LowPriority);
    }
    else if(visuAssembled!=NULL)
    {
        visuAssembled->close();
    }
}

//------------------------ Display data block visualization window.
void MainWindow::showVisBlocks(bool is_checked)
{
    if(is_checked!=false)
    {
        QThread *vis_thread;

        vis_thread = NULL;
        vis_thread = new QThread;

        RenderPCM *renderBlocks = new RenderPCM();
        renderBlocks->moveToThread(vis_thread);

        visuBlocks = new frame_vis(this);
        visuBlocks->setSettingsLabel("vis_blocks");
        visuBlocks->setTitle(TITLE_RENDER_BLOCKS);

        connect(vis_thread, SIGNAL(started()), renderBlocks, SLOT(dumpThreadDebug()));
        connect(vis_thread, SIGNAL(started()), visuBlocks, SLOT(show()));

        connect(vis_thread, SIGNAL(finished()), renderBlocks, SLOT(deleteLater()));
        connect(vis_thread, SIGNAL(finished()), vis_thread, SLOT(deleteLater()));

        connect(visuBlocks, SIGNAL(finished(int)), visuBlocks, SLOT(deleteLater()));
        connect(visuBlocks, SIGNAL(finished(int)), vis_thread, SLOT(quit()));
        connect(visuBlocks, SIGNAL(finished(int)), this, SLOT(hideVisBlocks()));

        connect(this, SIGNAL(aboutToExit()), visuBlocks, SLOT(close()));

        renderBlocks->setLivePlay(ui->cbxLivePB->isChecked());
        connect(ui->cbxLivePB, SIGNAL(clicked(bool)), renderBlocks, SLOT(setLivePlay(bool)));
        connect(this, SIGNAL(newVideoStandard(uint8_t)), renderBlocks, SLOT(setFrameTime(uint8_t)));
        connect(this, SIGNAL(newVideoStandard(uint8_t)), renderBlocks, SLOT(setLineCount(uint8_t)));
        connect(this, SIGNAL(newFrameAssembled(uint32_t)), renderBlocks, SLOT(prepareNewFrame(uint32_t)));

        if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM1)
        {
            renderBlocks->startPCM1DBFrame();
            connect(this, SIGNAL(retransmitPCMDataBlock(PCM1DataBlock)), renderBlocks, SLOT(renderNewBlock(PCM1DataBlock)));
        }
        else if(ui->lbxPCMType->currentIndex()==LIST_TYPE_PCM16X0)
        {
            renderBlocks->startPCM1600DBFrame();
            connect(this, SIGNAL(retransmitPCMDataBlock(PCM16X0DataBlock)), renderBlocks, SLOT(renderNewBlock(PCM16X0DataBlock)));
        }
        else if((ui->lbxPCMType->currentIndex()==LIST_TYPE_STC007)||(ui->lbxPCMType->currentIndex()==LIST_TYPE_M2))
        {
            renderBlocks->startSTC007DBFrame();
            connect(this, SIGNAL(retransmitPCMDataBlock(STC007DataBlock)), renderBlocks, SLOT(renderNewBlock(STC007DataBlock)));
        }
        connect(renderBlocks, SIGNAL(renderedFrame(QImage)), visuBlocks, SLOT(drawFrame(QImage)));
        connect(visuBlocks, SIGNAL(readyToDraw()), renderBlocks, SLOT(displayIsReady()));

        vis_thread->start(QThread::LowPriority);
    }
    else if(visuBlocks!=NULL)
    {
        visuBlocks->close();
    }
}

//------------------------ Hide source visualization window.
void MainWindow::hideVisSource()
{
    visuSource = NULL;
    ui->actVisSource->setChecked(false);
}

//------------------------ Hide binarized visualization window.
void MainWindow::hideVisBin()
{
    visuBin = NULL;
    ui->actVisBin->setChecked(false);
}

//------------------------ Hide re-assembled visualization window.
void MainWindow::hideVisAssembled()
{
    visuAssembled = NULL;
    ui->actVisAssembled->setChecked(false);
}

//------------------------ Hide data block visualization window.
void MainWindow::hideVisBlocks()
{
    visuBlocks = NULL;
    ui->actVisBlocks->setChecked(false);
}

//------------------------ Re-open source visualization window.
void MainWindow::reopenVisSource()
{
    if(visuSource!=NULL)
    {
        // Close old windows then reopen and re-link it.
        showVisSource(false);
        ui->actVisSource->setChecked(true);
    }
}

//------------------------ Re-open all vizualization windows.
void MainWindow::reopenVisualizers()
{
    reopenVisSource();
    if(visuBin!=NULL)
    {
        // Close old windows then reopen and re-link it.
        showVisBin(false);
        ui->actVisBin->setChecked(true);
    }
    if(visuAssembled!=NULL)
    {
        // Close old windows then reopen and re-link it.
        showVisAssembled(false);
        ui->actVisAssembled->setChecked(true);
    }
    if(visuBlocks!=NULL)
    {
        // Close old windows then reopen and re-link it.
        showVisBlocks(false);
        ui->actVisBlocks->setChecked(true);
    }
}

//------------------------ Save settings for logging mode for main module.
void MainWindow::updateSetMainLog()
{
    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    settings_hdl.beginGroup("debug_main");
    settings_hdl.setValue("settings", ui->actGenSettings->isChecked());
    settings_hdl.setValue("process", ui->actGenProcess->isChecked());
    settings_hdl.endGroup();
    setMainLogMode();
    setLBLogMode();
    setDILogMode();
}

//------------------------ Save settings for [VideoInFFMPEG] module.
void MainWindow::updateSetVIPLog()
{
    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    settings_hdl.beginGroup("debug_video");
    settings_hdl.setValue("settings", ui->actVIPSettings->isChecked());
    settings_hdl.setValue("process", ui->actVIPProcess->isChecked());
    settings_hdl.setValue("frames", ui->actVIPFrame->isChecked());
    settings_hdl.setValue("lines", ui->actVIPLine->isChecked());
    settings_hdl.endGroup();
    setVIPLogMode();
}

//------------------------ Save settings for [VideoToDigital] and [Binarizer] modules.
void MainWindow::updateSetLBLog()
{
    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    settings_hdl.beginGroup("debug_binarizer");
    settings_hdl.setValue("settings", ui->actLBSettings->isChecked());
    settings_hdl.setValue("process", ui->actLBProcess->isChecked());
    settings_hdl.setValue("brightness", ui->actLBBright->isChecked());
    settings_hdl.setValue("sweep", ui->actLBSweep->isChecked());
    settings_hdl.setValue("coordinates", ui->actLBCoords->isChecked());
    settings_hdl.setValue("raw_bin", ui->actLBRawBin->isChecked());
    settings_hdl.setValue("dump_lines", ui->actGenLines->isChecked());
    settings_hdl.endGroup();
    setLBLogMode();
}

//------------------------ Save settings for [xDataStitcher] and [xDeinterleaver] modules.
void MainWindow::updateSetDILog()
{
    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    settings_hdl.beginGroup("debug_deinterleaver");
    settings_hdl.setValue("settings", ui->actDISettings->isChecked());
    settings_hdl.setValue("process", ui->actDIProcess->isChecked());
    settings_hdl.setValue("err_corr", ui->actDIErrorCorr->isChecked());
    settings_hdl.setValue("deinterleave", ui->actDIDeinterleave->isChecked());
    settings_hdl.setValue("trimming", ui->actDITrim->isChecked());
    settings_hdl.setValue("padding", ui->actDIPadding->isChecked());
    settings_hdl.setValue("pad_fields", ui->actDIFields->isChecked());
    settings_hdl.setValue("pad_blocks", ui->actDIBlocks->isChecked());
    settings_hdl.setValue("assembly", ui->actDIAssembly->isChecked());
    settings_hdl.setValue("dump_blocks", ui->actGenBlocks->isChecked());
    settings_hdl.endGroup();
    setDILogMode();
}

//------------------------ Save settings for [AudioProcessor], [SamplesToAudio] and [SamplesToWAV] modules.
void MainWindow::updateSetAPLog()
{
    QSettings settings_hdl(QSettings::IniFormat, QSettings::UserScope, APP_ORG_NAME, APP_INI_NAME);
    settings_hdl.beginGroup("debug_audio");
    settings_hdl.setValue("settings", ui->actAPSettings->isChecked());
    settings_hdl.setValue("process", ui->actAPProcess->isChecked());
    settings_hdl.setValue("drop_action", ui->actAPDropAct->isChecked());
    settings_hdl.setValue("buffer", ui->actAPBuffer->isChecked());
    settings_hdl.setValue("file", ui->actAPFile->isChecked());
    settings_hdl.setValue("live", ui->actAPLive->isChecked());
    settings_hdl.endGroup();
    setAPLogMode();
}

//------------------------ Turn off logging for main.
void MainWindow::clearMainPLog()
{
    // Clear marks
    ui->actGenSettings->setChecked(false);
    ui->actGenProcess->setChecked(false);
    // Update setting storage and actual flags.
    updateSetMainLog();
}

//------------------------ Turn off logging for [VideoInFFMPEG] module.
void MainWindow::clearVIPLog()
{
    // Clear marks.
    ui->actVIPSettings->setChecked(false);
    ui->actVIPProcess->setChecked(false);
    ui->actVIPFrame->setChecked(false);
    ui->actVIPLine->setChecked(false);
    // Update setting storage and actual flags.
    updateSetVIPLog();
}

//------------------------ Turn off logging for [VideoToDigital] and [Binarizer] modules.
void MainWindow::clearLBLog()
{
    // Clear marks.
    ui->actLBSettings->setChecked(false);
    ui->actLBProcess->setChecked(false);
    ui->actLBBright->setChecked(false);
    ui->actLBSweep->setChecked(false);
    ui->actLBCoords->setChecked(false);
    ui->actLBRawBin->setChecked(false);
    ui->actGenLines->setChecked(false);
    // Update setting storage and actual flags.
    updateSetLBLog();
}

//------------------------ Turn off logging for [xDataStitcher] and [xDeinterleaver] modules.
void MainWindow::clearDILog()
{
    // Clear marks.
    ui->actDISettings->setChecked(false);
    ui->actDIProcess->setChecked(false);
    ui->actDIErrorCorr->setChecked(false);
    ui->actDIDeinterleave->setChecked(false);
    ui->actDITrim->setChecked(false);
    ui->actDIPadding->setChecked(false);
    ui->actDIFields->setChecked(false);
    ui->actDIBlocks->setChecked(false);
    ui->actDIAssembly->setChecked(false);
    ui->actGenBlocks->setChecked(false);
    // Update setting storage and actual flags.
    updateSetDILog();
}

//------------------------ Turn off debug logging for [AudioProcessor], [SamplesToAudio] and [SamplesToWAV] modules.
void MainWindow::clearAPLog()
{
    // Clear marks.
    ui->actAPSettings->setChecked(false);
    ui->actAPProcess->setChecked(false);
    ui->actAPDropAct->setChecked(false);
    ui->actAPBuffer->setChecked(false);
    ui->actAPFile->setChecked(false);
    ui->actAPLive->setChecked(false);
    // Update setting storage and actual flags.
    updateSetAPLog();
}

//------------------------ Turn off all debug logging.
void MainWindow::clearAllLogging()
{
    clearMainPLog();
    clearVIPLog();
    clearLBLog();
    clearDILog();
    clearAPLog();
}

//------------------------ React on video decoder asking for a source.
void MainWindow::playerNoSource()
{
    // Open dialog for selecting a file.
    ui->btnOpen->setEnabled(true);
    ui->btnPlay->setEnabled(true);
    ui->btnPlay->setCheckable(false);
    ui->btnPlay->setChecked(false);
    ui->btnPlay->repaint();
    ui->btnPause->setEnabled(false);
    ui->btnPause->setCheckable(false);
    ui->btnPause->setChecked(false);
    ui->btnPause->repaint();
    ui->cbxWaveSave->setEnabled(true);
    v_decoder_state = VDEC_STOP;
    // TODO: remember last source type and open dialog accordingly.
    loadVideo();
}

//------------------------ React on video decoder loading a new source.
void MainWindow::playerLoaded(QString in_path)
{
    ui->lblFileName->setText(in_path);
    ui->btnOpen->setEnabled(true);
    ui->btnPlay->setEnabled(true);
    ui->btnPlay->setCheckable(false);
    ui->btnPlay->setChecked(false);
    ui->btnPlay->repaint();
    ui->btnPause->setEnabled(false);
    ui->btnPause->setCheckable(false);
    ui->btnPause->setChecked(false);
    ui->btnPause->repaint();
    ui->cbxWaveSave->setEnabled(true);
}

//------------------------ React on video decoder starting playback.
void MainWindow::playerStarted(uint32_t in_frames_total)
{
    stat_total_frame_cnt = in_frames_total;
    ui->btnOpen->setEnabled(true);
    ui->btnPlay->setEnabled(true);
    ui->btnPlay->setCheckable(true);
    ui->btnPlay->setChecked(true);
    ui->btnPlay->repaint();
    ui->btnPause->setEnabled(true);
    ui->btnPause->setCheckable(false);
    ui->btnPause->setChecked(false);
    ui->btnPause->repaint();
    ui->cbxWaveSave->setEnabled(false);
    v_decoder_state = VDEC_PLAY;
}

//------------------------ React on video decoder stopping playback.
void MainWindow::playerStopped()
{
    ui->btnOpen->setEnabled(true);
    ui->btnPlay->setEnabled(true);
    ui->btnPlay->setCheckable(false);
    ui->btnPlay->setChecked(false);
    ui->btnPlay->repaint();
    ui->btnPause->setEnabled(false);
    ui->btnPause->setCheckable(false);
    ui->btnPause->setChecked(false);
    ui->btnPause->repaint();
    ui->cbxWaveSave->setEnabled(true);
    v_decoder_state = VDEC_STOP;
}

//------------------------ React on video decoder pausing playback.
void MainWindow::playerPaused()
{
    ui->btnOpen->setEnabled(true);
    ui->btnPlay->setEnabled(true);
    ui->btnPlay->setCheckable(true);
    ui->btnPlay->setChecked(true);
    ui->btnPlay->repaint();
    ui->btnPause->setEnabled(true);
    ui->btnPause->setCheckable(true);
    ui->btnPause->setChecked(true);
    ui->btnPause->repaint();
    ui->cbxWaveSave->setEnabled(false);
    v_decoder_state = VDEC_PAUSE;
}

//------------------------ React on video decoder error.
void MainWindow::playerError(QString error_text)
{
    ui->btnOpen->setEnabled(true);
    ui->btnPlay->setEnabled(true);
    ui->btnPlay->setCheckable(false);
    ui->btnPlay->setChecked(false);
    ui->btnPlay->repaint();
    ui->btnPause->setEnabled(false);
    ui->btnPause->setCheckable(false);
    ui->btnPause->setChecked(false);
    ui->btnPause->repaint();
    ui->cbxWaveSave->setEnabled(true);
    ui->lblFileName->clear();
    v_decoder_state = VDEC_IDLE;
    this->displayErrorMessage(error_text);
}

//------------------------ React on live playback state.
void MainWindow::livePBUpdate(bool flag)
{
    if(flag==false)
    {
        ui->indLivePB->setPalette(plt_redlabel);
    }
    else
    {
        ui->indLivePB->setPalette(plt_greenlabel);
    }
}

//------------------------ Update GUI counters and bars with data.
void MainWindow::updateGUIByTimer()
{
    // Update GUI elements.
    // Update frame assembling indication.
    if(frame_asm_pcm1.drawn==false)
    {
        updatePCM1FrameData();
    }
    if(frame_asm_pcm16x0.drawn==false)
    {
        updatePCM16x0FrameData();
    }
    if(frame_asm_stc007.drawn==false)
    {
        updateSTC007FrameData();
    }
    if(frame_asm_m2.drawn==false)
    {
        updateM2FrameData();
    }

    // Update decoder stats.
    ui->lcdTotalNumber->display((int)stat_total_frame_cnt);
    ui->lcdReadFrames->display((int)stat_read_frame_cnt);
    ui->lcdFrameDrop->display((int)stat_drop_frame_cnt);
    if(set_pcm_type==LIST_TYPE_PCM1)
    {
        ui->pgrTracking->setMaximum(frame_asm_pcm1.odd_std_lines+frame_asm_pcm1.even_std_lines);
        ui->pgrTracking->setValue(frame_asm_pcm1.odd_data_lines+frame_asm_pcm1.even_data_lines);
        ui->pgrDataQuality->setMaximum(frame_asm_pcm1.odd_std_lines+frame_asm_pcm1.even_std_lines);
        ui->pgrDataQuality->setValue(frame_asm_pcm1.odd_valid_lines+frame_asm_pcm1.even_valid_lines);
    }
    else if(set_pcm_type==LIST_TYPE_PCM16X0)
    {
        ui->pgrTracking->setMaximum(frame_asm_pcm16x0.odd_std_lines+frame_asm_pcm16x0.even_std_lines);
        ui->pgrTracking->setValue(frame_asm_pcm16x0.odd_data_lines+frame_asm_pcm16x0.even_data_lines);
        ui->pgrDataQuality->setMaximum(frame_asm_pcm16x0.odd_std_lines+frame_asm_pcm16x0.even_std_lines);
        ui->pgrDataQuality->setValue(frame_asm_pcm16x0.odd_valid_lines+frame_asm_pcm16x0.even_valid_lines);
    }
    else if(set_pcm_type==LIST_TYPE_STC007)
    {
        ui->pgrTracking->setMaximum(frame_asm_stc007.odd_std_lines+frame_asm_stc007.even_std_lines);
        ui->pgrTracking->setValue(frame_asm_stc007.odd_data_lines+frame_asm_stc007.even_data_lines);
        ui->pgrDataQuality->setMaximum(frame_asm_stc007.odd_std_lines+frame_asm_stc007.even_std_lines);
        ui->pgrDataQuality->setValue(frame_asm_stc007.odd_valid_lines+frame_asm_stc007.even_valid_lines);
    }
    else if(set_pcm_type==LIST_TYPE_M2)
    {
        ui->pgrTracking->setMaximum(frame_asm_m2.odd_std_lines+frame_asm_m2.even_std_lines);
        ui->pgrTracking->setValue(frame_asm_m2.odd_data_lines+frame_asm_m2.even_data_lines);
        ui->pgrDataQuality->setMaximum(frame_asm_m2.odd_std_lines+frame_asm_m2.even_std_lines);
        ui->pgrDataQuality->setValue(frame_asm_m2.odd_valid_lines+frame_asm_m2.even_valid_lines);
    }

    ui->lcdRefLevel->display((int)stat_ref_level);
    ui->lcdNoPCM->display((int)stat_no_pcm_cnt);
    ui->lcdCrcErr->display((int)stat_crc_err_cnt);
    ui->lcdDupErr->display((int)stat_dup_err_cnt);
    ui->lcdBadStitch->display((int)stat_bad_stitch_cnt);
    ui->lcdPCorr->display((int)stat_p_fix_cnt);
    ui->lcdQCorr->display((int)stat_q_fix_cnt);
    ui->lcdCWDCorr->display((int)stat_cwd_fix_cnt);
    ui->lcdBroken->display((int)stat_broken_block_cnt);
    ui->lcdDropout->display((int)stat_drop_block_cnt);
    ui->lcdSampleDrops->display((int)stat_drop_sample_cnt);
    ui->lcdMask->display((int)stat_mask_cnt);
    ui->lcdProcessedFrames->display((int)stat_processed_frame_cnt);

    // Update flag indicators.
    if(flag_bad_stitch_cnt>0)
    {
        // Light indicator up.
        ui->indBadStitch->setPalette(plt_yellowlabel);
        flag_bad_stitch_cnt--;
    }
    else
    {
        // Reset indicator's palette to default.
        ui->indBadStitch->setPalette(QApplication::palette());
    }
    if(flag_p_corr_cnt>0)
    {
        ui->indPCorr->setPalette(plt_greenlabel);
        flag_p_corr_cnt--;
    }
    else
    {
        ui->indPCorr->setPalette(QApplication::palette());
    }
    if(flag_q_corr_cnt>0)
    {
        ui->indQCorr->setPalette(plt_greenlabel);
        flag_q_corr_cnt--;
    }
    else
    {
        ui->indQCorr->setPalette(QApplication::palette());
    }
    if(flag_cwd_corr_cnt>0)
    {
        ui->indCWDCorr->setPalette(plt_greenlabel);
        flag_cwd_corr_cnt--;
    }
    else
    {
        ui->indCWDCorr->setPalette(QApplication::palette());
    }
    if(flag_broken_cnt>0)
    {
        ui->indBroken->setPalette(plt_redlabel);
        flag_broken_cnt--;
    }
    else
    {
        ui->indBroken->setPalette(QApplication::palette());
    }
    if(flag_dropout_cnt>0)
    {
        ui->indDropout->setPalette(plt_redlabel);
        flag_dropout_cnt--;
    }
    else
    {
        ui->indDropout->setPalette(QApplication::palette());
    }

    // Update VU-meters.
    ui->pgrVULeft->setValue(vu_left);
    ui->pgrVURight->setValue(vu_right);

    // Update queue fills.
    size_t buf_size;
    if(vl_lock.tryLock(5)!=false)
    {
        buf_size = video_lines.size();
        vl_lock.unlock();
        ui->pgrVideoBuf->setValue(buf_size*100/MAX_VLINE_QUEUE_SIZE);
    }
    if(set_pcm_type==LIST_TYPE_PCM1)
    {
        if(pcm1line_lock.tryLock(5)!=false)
        {
            buf_size = pcm1_lines.size();
            pcm1line_lock.unlock();
            ui->pgrLineBuf->setValue(buf_size*100/MAX_PCMLINE_QUEUE_SIZE);
        }
    }
    else if(set_pcm_type==LIST_TYPE_PCM16X0)
    {
        if(pcm16x0subline_lock.tryLock(5)!=false)
        {
            buf_size = pcm16x0_sublines.size();
            pcm16x0subline_lock.unlock();
            ui->pgrLineBuf->setValue(buf_size*100/MAX_PCMLINE_QUEUE_SIZE);
        }
    }
    else if((set_pcm_type==LIST_TYPE_STC007)||(set_pcm_type==LIST_TYPE_M2))
    {
        if(stcline_lock.tryLock(5)!=false)
        {
            buf_size = stc007_lines.size();
            stcline_lock.unlock();
            ui->pgrLineBuf->setValue(buf_size*100/MAX_PCMLINE_QUEUE_SIZE);
        }
    }
    if(audio_lock.tryLock(5)!=false)
    {
        buf_size = audio_data.size();
        audio_lock.unlock();
        ui->pgrBlockBuf->setValue(buf_size*100/MAX_SAMPLEPAIR_QUEUE_SIZE);
    }
}

//------------------------ Update PCM-1 frame assembling data.
void MainWindow::updatePCM1FrameData()
{
    QString odd_number_str, even_number_str;
    FrameBinDescriptor bin_data;
    // Update frame number.
    ui->edtPCM1FrameNo->setText(QString::number(frame_asm_pcm1.frame_number, 10));
    // Update field order.
    if(frame_asm_pcm1.isOrderTFF()!=false)
    {
        ui->edtPCM1SetOrder->setText(LIST_ORDER_TFF+LIST_ORDER_FORCE);
        ui->edtPCM1SetOrder->setEnabled(true);
    }
    else if(frame_asm_pcm1.isOrderBFF()!=false)
    {
        ui->edtPCM1SetOrder->setText(LIST_ORDER_BFF+LIST_ORDER_FORCE);
        ui->edtPCM1SetOrder->setEnabled(true);
    }
    else
    {
        ui->edtPCM1SetOrder->setText(LIST_ORDER_UNK);
        ui->edtPCM1SetOrder->setEnabled(false);
    }
    // Update data coordinates.
    bin_data = getBinDescriptorByFrameNo(frame_asm_pcm1.frame_number);
    if(bin_data.data_coord.isSourceDoubleWidth()!=false)
    {
        bin_data.line_length = bin_data.line_length/2;
    }
    ui->spbPCM1LineLen->setValue(bin_data.line_length);
    if(bin_data.data_coord.areValid()==false)
    {
        ui->spbPCM1DataCoordStart->setValue(0);
        ui->spbPCM1DataCoordStart->setEnabled(false);
        ui->spbPCM1DataCoordStop->setValue(0);
        ui->spbPCM1DataCoordStop->setEnabled(false);
    }
    else
    {
        if(bin_data.data_coord.isSourceDoubleWidth()!=false)
        {
            bin_data.data_coord.data_start = bin_data.data_coord.data_start/2;
            bin_data.data_coord.data_stop = bin_data.data_coord.data_stop/2;
        }
        ui->spbPCM1DataCoordStart->setValue(bin_data.data_coord.data_start);
        ui->spbPCM1DataCoordStop->setValue((int16_t)bin_data.line_length-bin_data.data_coord.data_stop);
        if(bin_data.data_coord.not_sure!=false)
        {
            ui->spbPCM1DataCoordStart->setEnabled(false);
            ui->spbPCM1DataCoordStop->setEnabled(false);
        }
        else
        {
            ui->spbPCM1DataCoordStart->setEnabled(true);
            ui->spbPCM1DataCoordStop->setEnabled(true);
        }
    }

    // Update frame trim indicators.
    ui->spbPCM1OddTopCut->setValue(frame_asm_pcm1.odd_top_data);
    ui->spbPCM1EvenTopCut->setValue(frame_asm_pcm1.even_top_data);
    // Update line count indicators.
    odd_number_str = QString::number(frame_asm_pcm1.odd_data_lines, 10);
    even_number_str = QString::number(frame_asm_pcm1.even_data_lines, 10);
    odd_number_str += "/"+QString::number(frame_asm_pcm1.odd_std_lines, 10);
    even_number_str += "/"+QString::number(frame_asm_pcm1.even_std_lines, 10);
    ui->edtPCM1OddCount->setText(odd_number_str);
    ui->edtPCM1EvenCount->setText(even_number_str);
    // Update frame padding indicators.
    ui->spbPCM1OddTopPad->setValue(frame_asm_pcm1.odd_top_padding);
    ui->spbPCM1EvenTopPad->setValue(frame_asm_pcm1.even_top_padding);
    ui->spbPCM1OddBottomPad->setValue(frame_asm_pcm1.odd_bottom_padding);
    ui->spbPCM1EvenBottomPad->setValue(frame_asm_pcm1.even_bottom_padding);
    // Prevent updating fields in the window until [frame_asm_pcm1] is updated/rewritten.
    frame_asm_pcm1.drawn = true;
}

//------------------------ Update PCM-16x0 frame assembling data.
void MainWindow::updatePCM16x0FrameData()
{
    QString odd_number_str, even_number_str;
    FrameBinDescriptor bin_data;
    // Update frame number.
    ui->edtPCM16x0FrameNo->setText(QString::number(frame_asm_pcm16x0.frame_number, 10));
    // Update field order.
    if(frame_asm_pcm16x0.isOrderTFF()!=false)
    {
        ui->edtPCM16x0SetOrder->setText(LIST_ORDER_TFF+LIST_ORDER_FORCE);
        ui->edtPCM16x0SetOrder->setEnabled(true);
    }
    else if(frame_asm_pcm16x0.isOrderBFF()!=false)
    {
        ui->edtPCM16x0SetOrder->setText(LIST_ORDER_BFF+LIST_ORDER_FORCE);
        ui->edtPCM16x0SetOrder->setEnabled(true);
    }
    else
    {
        ui->edtPCM16x0SetOrder->setText(LIST_ORDER_UNK);
        ui->edtPCM16x0SetOrder->setEnabled(false);
    }
    // Update data coordinates.
    bin_data = getBinDescriptorByFrameNo(frame_asm_pcm16x0.frame_number);
    if(bin_data.data_coord.isSourceDoubleWidth()!=false)
    {
        bin_data.line_length = bin_data.line_length/2;
    }
    ui->spbPCM16x0LineLen->setValue(bin_data.line_length);
    if(bin_data.data_coord.areValid()==false)
    {
        ui->spbPCM16x0DataCoordStart->setValue(0);
        ui->spbPCM16x0DataCoordStart->setEnabled(false);
        ui->spbPCM16x0DataCoordStop->setValue(0);
        ui->spbPCM16x0DataCoordStop->setEnabled(false);
    }
    else
    {
        if(bin_data.data_coord.isSourceDoubleWidth()!=false)
        {
            bin_data.data_coord.data_start = bin_data.data_coord.data_start/2;
            bin_data.data_coord.data_stop = bin_data.data_coord.data_stop/2;
        }
        ui->spbPCM16x0DataCoordStart->setValue(bin_data.data_coord.data_start);
        ui->spbPCM16x0DataCoordStop->setValue((int16_t)bin_data.line_length-bin_data.data_coord.data_stop);
        if(bin_data.data_coord.not_sure!=false)
        {
            ui->spbPCM16x0DataCoordStart->setEnabled(false);
            ui->spbPCM16x0DataCoordStop->setEnabled(false);
        }
        else
        {
            ui->spbPCM16x0DataCoordStart->setEnabled(true);
            ui->spbPCM16x0DataCoordStop->setEnabled(true);
        }
    }
    // Update PCM-1630 data format.
    if(frame_asm_pcm16x0.ei_format==false)
    {
        ui->edtPCM16x0DataFormat->setText(LIST_PCM16X_SI);
    }
    else
    {
        ui->edtPCM16x0DataFormat->setText(LIST_PCM16X_EI);
    }
    // Update frame trim indicators.
    ui->spbPCM16x0OddTopCut->setValue(frame_asm_pcm16x0.odd_top_data);
    ui->spbPCM16x0EvenTopCut->setValue(frame_asm_pcm16x0.even_top_data);
    // Update line count indicators.
    odd_number_str = QString::number(frame_asm_pcm16x0.odd_data_lines, 10);
    even_number_str = QString::number(frame_asm_pcm16x0.even_data_lines, 10);
    odd_number_str += "/"+QString::number(frame_asm_pcm16x0.odd_std_lines, 10);
    even_number_str += "/"+QString::number(frame_asm_pcm16x0.even_std_lines, 10);
    ui->edtPCM16x0OddCount->setText(odd_number_str);
    ui->edtPCM16x0EvenCount->setText(even_number_str);
    // Update frame padding indicators.
    ui->spbPCM16x0OddTopPad->setValue(frame_asm_pcm16x0.odd_top_padding);
    ui->spbPCM16x0EvenTopPad->setValue(frame_asm_pcm16x0.even_top_padding);
    ui->spbPCM16x0OddBottomPad->setValue(frame_asm_pcm16x0.odd_bottom_padding);
    ui->spbPCM16x0EvenBottomPad->setValue(frame_asm_pcm16x0.even_bottom_padding);
    if(frame_asm_pcm16x0.padding_ok==false)
    {
        ui->spbPCM16x0OddTopPad->setEnabled(false);
        ui->spbPCM16x0EvenTopPad->setEnabled(false);
        ui->spbPCM16x0OddBottomPad->setEnabled(false);
        ui->spbPCM16x0EvenBottomPad->setEnabled(false);
    }
    else
    {
        ui->spbPCM16x0OddTopPad->setEnabled(true);
        ui->spbPCM16x0EvenTopPad->setEnabled(true);
        ui->spbPCM16x0OddBottomPad->setEnabled(true);
        ui->spbPCM16x0EvenBottomPad->setEnabled(true);
    }
    // Prevent updating fields in the window until [frame_asm_pcm16x0] is updated/rewritten.
    frame_asm_pcm16x0.drawn = true;
}

//------------------------ Update STC-007 frame assembling data.
void MainWindow::updateSTC007FrameData()
{
    uint16_t field_imbalance;
    QString frame_num, odd_number_str, even_number_str;
    FrameBinDescriptor bin_data;
    // Update frame number.
    frame_num = QString::number(frame_asm_stc007.frame_number, 10);
    if(frame_asm_stc007.isAddressSet()!=false)
    {
        // Assemble index and time code string.
        frame_num.sprintf("%u (Index: %02d, %02d:%02d:%02d.%02d)",
                          frame_asm_stc007.frame_number,
                          frame_asm_stc007.ctrl_index,
                          frame_asm_stc007.ctrl_hour,
                          frame_asm_stc007.ctrl_minute,
                          frame_asm_stc007.ctrl_second,
                          frame_asm_stc007.ctrl_field/2);
    }
    ui->edtSTC007FrameNo->setText(frame_num);
    // Update field order.
    if(frame_asm_stc007.isOrderSet()==false)
    {
        ui->edtSTC007SetOrder->setText(LIST_ORDER_UNK);
        ui->edtSTC007SetOrder->setEnabled(false);
    }
    else if(frame_asm_stc007.isOrderBFF()!=false)
    {
        if(frame_asm_stc007.isOrderPreset()==false)
        {
            ui->edtSTC007SetOrder->setText(LIST_ORDER_BFF);
        }
        else
        {
            ui->edtSTC007SetOrder->setText(LIST_ORDER_BFF+LIST_ORDER_FORCE);
        }
        if((frame_asm_stc007.isOrderGuessed()==false)&&(frame_asm_stc007.isOrderPreset()==false))
        {
            ui->edtSTC007SetOrder->setEnabled(true);
        }
        else
        {
            ui->edtSTC007SetOrder->setEnabled(false);
        }
    }
    else
    {
        if(frame_asm_stc007.isOrderPreset()==false)
        {
            ui->edtSTC007SetOrder->setText(LIST_ORDER_TFF);
        }
        else
        {
            ui->edtSTC007SetOrder->setText(LIST_ORDER_TFF+LIST_ORDER_FORCE);
        }
        if((frame_asm_stc007.isOrderGuessed()==false)&&(frame_asm_stc007.isOrderPreset()==false))
        {
            ui->edtSTC007SetOrder->setEnabled(true);
        }
        else
        {
            ui->edtSTC007SetOrder->setEnabled(false);
        }
    }
    // Update data coordinates.
    bin_data = getBinDescriptorByFrameNo(frame_asm_stc007.frame_number);
    if(bin_data.data_coord.isSourceDoubleWidth()!=false)
    {
        bin_data.line_length = bin_data.line_length/2;
    }
    ui->spbSTC007LineLen->setValue(bin_data.line_length);
    if(bin_data.data_coord.areValid()==false)
    {
        ui->spbSTC007DataCoordStart->setValue(0);
        ui->spbSTC007DataCoordStart->setEnabled(false);
        ui->spbSTC007DataCoordStop->setValue(0);
        ui->spbSTC007DataCoordStop->setEnabled(false);
    }
    else
    {
        if(bin_data.data_coord.isSourceDoubleWidth()!=false)
        {
            bin_data.data_coord.data_start = bin_data.data_coord.data_start/2;
            bin_data.data_coord.data_stop = bin_data.data_coord.data_stop/2;
        }
        ui->spbSTC007DataCoordStart->setValue(bin_data.data_coord.data_start);
        ui->spbSTC007DataCoordStop->setValue((int16_t)bin_data.line_length-bin_data.data_coord.data_stop);
        if(bin_data.data_coord.not_sure!=false)
        {
            ui->spbSTC007DataCoordStart->setEnabled(false);
            ui->spbSTC007DataCoordStop->setEnabled(false);
        }
        else
        {
            ui->spbSTC007DataCoordStart->setEnabled(true);
            ui->spbSTC007DataCoordStop->setEnabled(true);
        }
    }
    // Update field imbalance indicator (beta, TODO).
    if(frame_asm_stc007.even_valid_lines>frame_asm_stc007.odd_valid_lines)
    {
        field_imbalance = frame_asm_stc007.even_valid_lines - frame_asm_stc007.odd_valid_lines;
        field_imbalance = (frame_asm_stc007.even_std_lines + field_imbalance/2)/field_imbalance;
        odd_number_str = QString::number(field_imbalance);
        if(field_imbalance>7)
        {
            ui->lblDBGOdd->setPalette(plt_greenlabel);
        }
        else if(field_imbalance>3)
        {
            ui->lblDBGOdd->setPalette(plt_yellowlabel);
        }
        else
        {
            ui->lblDBGOdd->setPalette(plt_redlabel);
        }
        ui->lblDBGOdd->setText(odd_number_str);
        ui->lblDBGEven->setText("");
        ui->lblDBGEven->setPalette(plt_greenlabel);
    }
    else if(frame_asm_stc007.even_valid_lines<frame_asm_stc007.odd_valid_lines)
    {
        field_imbalance = frame_asm_stc007.odd_valid_lines - frame_asm_stc007.even_valid_lines;
        field_imbalance = (frame_asm_stc007.odd_std_lines + field_imbalance/2)/field_imbalance;
        even_number_str = QString::number(field_imbalance);
        if(field_imbalance>7)
        {
            ui->lblDBGEven->setPalette(plt_greenlabel);
        }
        else if(field_imbalance>3)
        {
            ui->lblDBGEven->setPalette(plt_yellowlabel);
        }
        else
        {
            ui->lblDBGEven->setPalette(plt_redlabel);
        }
        ui->lblDBGOdd->setText("");
        ui->lblDBGEven->setText(even_number_str);
        ui->lblDBGOdd->setPalette(plt_greenlabel);
    }
    else
    {
        ui->lblDBGOdd->setPalette(plt_greenlabel);
        ui->lblDBGEven->setPalette(plt_greenlabel);
        ui->lblDBGOdd->setText("");
        ui->lblDBGEven->setText("");
    }

    // Update video standard.
    if(frame_asm_stc007.video_standard==FrameAsmDescriptor::VID_NTSC)
    {
        if(frame_asm_stc007.vid_std_preset==false)
        {
            ui->edtSTC007LineCnt->setText(LIST_VIDSTD_NTSC);
        }
        else
        {
            ui->edtSTC007LineCnt->setText(LIST_VIDSTD_NTSC+LIST_VIDSTD_FORCE);
        }
    }
    else if(frame_asm_stc007.video_standard==FrameAsmDescriptor::VID_PAL)
    {
        if(frame_asm_stc007.vid_std_preset==false)
        {
            ui->edtSTC007LineCnt->setText(LIST_VIDSTD_PAL);
        }
        else
        {
            ui->edtSTC007LineCnt->setText(LIST_VIDSTD_PAL+LIST_VIDSTD_FORCE);
        }
    }
    else
    {
        ui->edtSTC007LineCnt->setText(LIST_VIDSTD_UNK);
    }
    if(frame_asm_stc007.vid_std_guessed==false)
    {
        ui->edtSTC007LineCnt->setEnabled(true);
    }
    else
    {
        ui->edtSTC007LineCnt->setEnabled(false);
    }
    // Update frame trim indicators.
    ui->spbSTC007OddTopCut->setValue(frame_asm_stc007.odd_top_data);
    ui->spbSTC007EvenTopCut->setValue(frame_asm_stc007.even_top_data);
    // Update line count indicators.
    odd_number_str = QString::number(frame_asm_stc007.odd_data_lines, 10);
    even_number_str = QString::number(frame_asm_stc007.even_data_lines, 10);
    odd_number_str += "/"+QString::number(frame_asm_stc007.odd_std_lines);
    even_number_str += "/"+QString::number(frame_asm_stc007.even_std_lines);
    ui->edtSTC007OddCount->setText(odd_number_str);
    ui->edtSTC007EvenCount->setText(even_number_str);
    // Update audio resolution.
    if(frame_asm_stc007.odd_resolution==STC007Deinterleaver::RES_MODE_14BIT)
    {
        ui->edtSTC007OddAudRes->setText(LIST_14BIT_AUD);
    }
    else if(frame_asm_stc007.odd_resolution==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
    {
        ui->edtSTC007OddAudRes->setText(LIST_14AUTO_AUD);
    }
    else if(frame_asm_stc007.odd_resolution==STC007Deinterleaver::RES_MODE_16BIT)
    {
        ui->edtSTC007OddAudRes->setText(LIST_16BIT_AUD);
    }
    else
    {
        ui->edtSTC007OddAudRes->setText(LIST_16AUTO_AUD);
    }

    if(frame_asm_stc007.even_resolution==STC007Deinterleaver::RES_MODE_14BIT)
    {
        ui->edtSTC007EvenAudRes->setText(LIST_14BIT_AUD);
    }
    else if(frame_asm_stc007.even_resolution==STC007Deinterleaver::RES_MODE_14BIT_AUTO)
    {
        ui->edtSTC007EvenAudRes->setText(LIST_14AUTO_AUD);
    }
    else if(frame_asm_stc007.even_resolution==STC007Deinterleaver::RES_MODE_16BIT)
    {
        ui->edtSTC007EvenAudRes->setText(LIST_16BIT_AUD);
    }
    else
    {
        ui->edtSTC007EvenAudRes->setText(LIST_16AUTO_AUD);
    }
    // Update frame padding indicators.
    ui->spbSTC007InnerPadding->setValue(frame_asm_stc007.inner_padding);
    ui->spbSTC007OuterPadding->setValue(frame_asm_stc007.outer_padding);
    ui->spbSTC007InnerPadding->setEnabled(frame_asm_stc007.inner_padding_ok);
    ui->spbSTC007OuterPadding->setEnabled(frame_asm_stc007.outer_padding_ok);

    // Prevent updating fields in the window until [frame_asm_stc007] is updated/rewritten.
    frame_asm_stc007.drawn = true;
}

//------------------------ Update M2 frame assembling data.
void MainWindow::updateM2FrameData()
{
    QString frame_num, odd_number_str, even_number_str;
    FrameBinDescriptor bin_data;
    // Update frame number.
    frame_num = QString::number(frame_asm_m2.frame_number, 10);
    if(frame_asm_m2.isAddressSet()!=false)
    {
        // Assemble index and time code string.
        frame_num.sprintf("%u (Index: %02d, %02d:%02d:%02d.%02d)",
                          frame_asm_m2.frame_number,
                          frame_asm_m2.ctrl_index,
                          frame_asm_m2.ctrl_hour,
                          frame_asm_m2.ctrl_minute,
                          frame_asm_m2.ctrl_second,
                          frame_asm_m2.ctrl_field/2);
    }
    ui->edtM2FrameNo->setText(frame_num);
    // Update field order.
    if(frame_asm_m2.isOrderSet()==false)
    {
        ui->edtM2SetOrder->setText(LIST_ORDER_UNK);
        ui->edtM2SetOrder->setEnabled(false);
    }
    else if(frame_asm_m2.isOrderBFF()!=false)
    {
        if(frame_asm_m2.isOrderPreset()==false)
        {
            ui->edtM2SetOrder->setText(LIST_ORDER_BFF);
        }
        else
        {
            ui->edtM2SetOrder->setText(LIST_ORDER_BFF+LIST_ORDER_FORCE);
        }
        if((frame_asm_m2.isOrderGuessed()==false)&&(frame_asm_m2.isOrderPreset()==false))
        {
            ui->edtM2SetOrder->setEnabled(true);
        }
        else
        {
            ui->edtM2SetOrder->setEnabled(false);
        }
    }
    else
    {
        if(frame_asm_m2.isOrderPreset()==false)
        {
            ui->edtM2SetOrder->setText(LIST_ORDER_TFF);
        }
        else
        {
            ui->edtM2SetOrder->setText(LIST_ORDER_TFF+LIST_ORDER_FORCE);
        }
        if((frame_asm_m2.isOrderGuessed()==false)&&(frame_asm_m2.isOrderPreset()==false))
        {
            ui->edtM2SetOrder->setEnabled(true);
        }
        else
        {
            ui->edtM2SetOrder->setEnabled(false);
        }
    }
    // Update data coordinates.
    bin_data = getBinDescriptorByFrameNo(frame_asm_m2.frame_number);
    if(bin_data.data_coord.isSourceDoubleWidth()!=false)
    {
        bin_data.line_length = bin_data.line_length/2;
    }
    ui->spbM2LineLen->setValue(bin_data.line_length);
    if(bin_data.data_coord.areValid()==false)
    {
        ui->spbM2DataCoordStart->setValue(0);
        ui->spbM2DataCoordStart->setEnabled(false);
        ui->spbM2DataCoordStop->setValue(0);
        ui->spbM2DataCoordStop->setEnabled(false);
    }
    else
    {
        if(bin_data.data_coord.isSourceDoubleWidth()!=false)
        {
            bin_data.data_coord.data_start = bin_data.data_coord.data_start/2;
            bin_data.data_coord.data_stop = bin_data.data_coord.data_stop/2;
        }
        ui->spbM2DataCoordStart->setValue(bin_data.data_coord.data_start);
        ui->spbM2DataCoordStop->setValue((int16_t)bin_data.line_length-bin_data.data_coord.data_stop);
        if(bin_data.data_coord.not_sure!=false)
        {
            ui->spbM2DataCoordStart->setEnabled(false);
            ui->spbM2DataCoordStop->setEnabled(false);
        }
        else
        {
            ui->spbM2DataCoordStart->setEnabled(true);
            ui->spbM2DataCoordStop->setEnabled(true);
        }
    }

    // Update video standard.
    if(frame_asm_m2.video_standard==FrameAsmDescriptor::VID_NTSC)
    {
        if(frame_asm_m2.vid_std_preset==false)
        {
            ui->edtM2LineCnt->setText(LIST_VIDSTD_NTSC);
        }
        else
        {
            ui->edtM2LineCnt->setText(LIST_VIDSTD_NTSC+LIST_VIDSTD_FORCE);
        }
    }
    else if(frame_asm_m2.video_standard==FrameAsmDescriptor::VID_PAL)
    {
        if(frame_asm_m2.vid_std_preset==false)
        {
            ui->edtM2LineCnt->setText(LIST_VIDSTD_PAL);
        }
        else
        {
            ui->edtM2LineCnt->setText(LIST_VIDSTD_PAL+LIST_VIDSTD_FORCE);
        }
    }
    else
    {
        ui->edtM2LineCnt->setText(LIST_VIDSTD_UNK);
    }
    if(frame_asm_m2.vid_std_guessed==false)
    {
        ui->edtM2LineCnt->setEnabled(true);
    }
    else
    {
        ui->edtM2LineCnt->setEnabled(false);
    }
    // Update frame trim indicators.
    ui->spbM2OddTopCut->setValue(frame_asm_m2.odd_top_data);
    ui->spbM2EvenTopCut->setValue(frame_asm_m2.even_top_data);
    // Update line count indicators.
    odd_number_str = QString::number(frame_asm_m2.odd_data_lines, 10);
    even_number_str = QString::number(frame_asm_m2.even_data_lines, 10);
    odd_number_str += "/"+QString::number(frame_asm_m2.odd_std_lines);
    even_number_str += "/"+QString::number(frame_asm_m2.even_std_lines);
    ui->edtM2OddCount->setText(odd_number_str);
    ui->edtM2EvenCount->setText(even_number_str);
    // Update frame padding indicators.
    ui->spbM2InnerPadding->setValue(frame_asm_m2.inner_padding);
    ui->spbM2OuterPadding->setValue(frame_asm_m2.outer_padding);
    ui->spbM2InnerPadding->setEnabled(frame_asm_m2.inner_padding_ok);
    ui->spbM2OuterPadding->setEnabled(frame_asm_m2.outer_padding_ok);

    // Prevent updating fields in the window until [frame_asm_m2] is updated/rewritten.
    frame_asm_m2.drawn = true;
}

//------------------------ Receive and retransmit binarized PCM-1 line.
void MainWindow::receiveBinLine(PCM1Line in_line)
{
    emit retransmitBinLine(in_line);
}

//------------------------ Receive and retransmit binarized PCM-16x0 sub-line.
void MainWindow::receiveBinLine(PCM16X0SubLine in_line)
{
    emit retransmitBinLine(in_line);
}

//------------------------ Receive and retransmit binarized STC-007 line.
void MainWindow::receiveBinLine(STC007Line in_line)
{
    emit retransmitBinLine(in_line);
}

//------------------------ Receive and retransmit PCM-1 sub-line from re-assembled frame.
void MainWindow::receiveAsmLine(PCM1SubLine in_line)
{
    emit retransmitAsmLine(in_line);
}

//------------------------ Receive and retransmit PCM-16x0 sub-line from re-assembled frame.
void MainWindow::receiveAsmLine(PCM16X0SubLine in_line)
{
    emit retransmitAsmLine(in_line);
}

//------------------------ Receive and retransmit STC-007 line from re-assembled frame.
void MainWindow::receiveAsmLine(STC007Line in_line)
{
    emit retransmitAsmLine(in_line);
}

//------------------------ Receive and retransmit PCM-1 data block after deinterleave.
void MainWindow::receivePCMDataBlock(PCM1DataBlock in_block)
{
    emit retransmitPCMDataBlock(in_block);
}

//------------------------ Receive and retransmit PCM-16x0 data block after deinterleave.
void MainWindow::receivePCMDataBlock(PCM16X0DataBlock in_block)
{
    emit retransmitPCMDataBlock(in_block);
}

//------------------------ Receive and retransmit STC-007 data block after deinterleave.
void MainWindow::receivePCMDataBlock(STC007DataBlock in_block)
{
    emit retransmitPCMDataBlock(in_block);
}

//------------------------ Receive VU levels for displaying.
void MainWindow::receiveVUMeters(uint8_t in_l, uint8_t in_r)
{
    vu_left = in_l;
    vu_right = in_r;
}

//------------------------ Update stats for debug bar.
void MainWindow::updateDebugBar(quint64 in_stat)
{
    uint64_t stat_summ;
    stat_debug_avg[stat_dbg_index] = in_stat;
    stat_dbg_index++;
    if(stat_dbg_index>=DBG_AVG_LEN)
    {
        stat_dbg_index = 0;
    }

    in_stat = in_stat&0xFFFFFFFF;
    if(in_stat>=(quint64)ui->pgrDebug->maximum())
    {
        ui->pgrDebug->setMaximum(in_stat);
        ui->pgrDebug->setToolTip(QString::number(in_stat));
    }

    stat_summ = 0;
    for(uint8_t i=0;i<DBG_AVG_LEN;i++)
    {
        stat_summ += stat_debug_avg[i];
    }
    stat_summ = stat_summ/DBG_AVG_LEN;
    ui->pgrDebug->setValue(stat_summ);
}

//------------------------ Update stats after VIP has read a frame.
void MainWindow::updateStatsVIPFrame(uint32_t frame_no)
{
    stat_read_frame_cnt = frame_no;
    if((frame_no==0)||(frame_no==1))
    {
        clearStat();
    }
    // Check if source didn't report correct total frame count.
    if(stat_read_frame_cnt>stat_total_frame_cnt)
    {
        // Correct total frame counter.
        stat_total_frame_cnt = stat_read_frame_cnt;
    }
}

//------------------------ Update stats after VIP has spliced a frame.
void MainWindow::updateStatsVideoTracking(FrameBinDescriptor in_tracking)
{
    // Update oldest entry.
    stat_video_tracking = in_tracking;
    stat_tracking_arr.push(in_tracking);

    stat_dup_err_cnt += in_tracking.totalDuplicated();

    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[M] Frame"<<in_tracking.frame_id<<"binarized by"<<in_tracking.time_odd<<"us";
    }

    stat_no_pcm_cnt += (in_tracking.lines_odd-in_tracking.lines_pcm_odd);
    stat_no_pcm_cnt += (in_tracking.lines_even-in_tracking.lines_pcm_even);
    stat_crc_err_cnt += in_tracking.lines_bad_odd;
    stat_crc_err_cnt += in_tracking.lines_bad_even;

    // Report the number of the binarized frame.
    emit newFrameBinarized(in_tracking.frame_id);
}

//------------------------ Update stats with new value for dropped frames.
void MainWindow::updateStatsDroppedFrame()
{
    // Update counter.
    stat_drop_frame_cnt++;
}

//------------------------ Reset help output for work log for PCM-1.
void MainWindow::resetWorkLogHelpPCM1()
{
    wl_pcm1_help_idx = 0;
}

//------------------------ Reset help output for work log for PCM-16x0.
void MainWindow::resetWorkLogHelpPCM16X0()
{
    wl_pcm16x0_help_idx = 0;
}

//------------------------ Reset help output for work log for STC-007.
void MainWindow::resetWorkLogHelpSTC007()
{
    wl_stc007_help_idx = 0;
}

//------------------------ Output next help string for work log for PCM-1.
QString MainWindow::logHelpNextPCM1()
{
    QString text_out;
    if(wl_pcm1_help_idx<WL_PCM1_HELP_SIZE)
    {
        text_out = QString::fromStdString(WL_PCM1_HELP[wl_pcm1_help_idx]);
        wl_pcm1_help_idx++;
    }
    else
    {
        text_out.clear();
    }
    return text_out;
}

//------------------------ Output next help string for work log for PCM-16x0.
QString MainWindow::logHelpNextPCM16X0()
{
    QString text_out;
    if(wl_pcm16x0_help_idx<WL_PCM16X0_HELP_SIZE)
    {
        text_out = QString::fromStdString(WL_PCM16x0_HELP[wl_pcm16x0_help_idx]);
        wl_pcm16x0_help_idx++;
    }
    else
    {
        text_out.clear();
    }
    return text_out;
}

//------------------------ Output next help string for work log for STC-007.
QString MainWindow::logHelpNextSTC007()
{
    QString text_out;
    if(wl_stc007_help_idx<WL_STC007_HELP_SIZE)
    {
        text_out = QString::fromStdString(WL_STC007_HELP[wl_stc007_help_idx]);
        wl_stc007_help_idx++;
    }
    else
    {
        text_out.clear();
    }
    return text_out;
}

//------------------------ Make a user-log string for the frame assembly data.
QString MainWindow::logStatsFrameAsm(FrameAsmPCM1 asm_data)
{
    QString frame_log, tmp_log;
    FrameBinDescriptor bin_data;
    // Log frame number.
    frame_log.sprintf("PCM-1 F[%06u] ", asm_data.frame_number);
    // Get binarization stats for the frame.
    bin_data = getBinDescriptorByFrameNo(asm_data.frame_number);
    if(bin_data.data_coord.isSourceDoubleWidth()!=false)
    {
        bin_data.line_length = bin_data.line_length/2;
        if(bin_data.data_coord.areValid()!=false)
        {
            bin_data.data_coord.data_start = bin_data.data_coord.data_start/2;
            bin_data.data_coord.data_stop = bin_data.data_coord.data_stop/2;
        }
    }
    // Log video standard.
    if(asm_data.odd_std_lines==LINES_PER_NTSC_FIELD)
    {
        frame_log += "NTSC ";
    }
    else if(asm_data.odd_std_lines==LINES_PER_PAL_FIELD)
    {
        frame_log += "PAL  ";
    }
    else
    {
        frame_log += "     ";
    }
    // Log number of lines and vertical data coordinates.
    tmp_log.sprintf("Lo[%03u/%03u/%03u] Le[%03u/%03u/%03u] Vo[%03u/%03u] Ve[%03u/%03u] ",
                      asm_data.odd_std_lines, asm_data.odd_data_lines, asm_data.odd_valid_lines,
                      asm_data.even_std_lines, asm_data.even_data_lines, asm_data.even_valid_lines,
                      asm_data.odd_top_data, asm_data.odd_bottom_data,
                      asm_data.even_top_data, asm_data.even_bottom_data);
    frame_log += tmp_log;
    // Log horizontal data coordinates.
    tmp_log.sprintf("H[0:%03u]", bin_data.line_length);
    frame_log += tmp_log;
    if(bin_data.data_coord.areValid()==false)
    {
        frame_log += "[???:???][G] ";
    }
    else
    {
        tmp_log.sprintf("[%03d:%03d]",
                        bin_data.data_coord.data_start,
                        bin_data.data_coord.data_stop);
        frame_log += tmp_log;
        if(bin_data.data_coord.not_sure==false)
        {
            frame_log += "[A] ";
        }
        else
        {
            frame_log += "[G] ";
        }
    }
    // Log field order.
    if(asm_data.field_order==FrameAsmDescriptor::ORDER_TFF)
    {
        frame_log += "O[TFF]";
    }
    else if(asm_data.field_order==FrameAsmDescriptor::ORDER_BFF)
    {
        frame_log += "O[BFF]";
    }
    else
    {
        frame_log += "O[UNK]";
    }
    if(asm_data.isOrderGuessed()!=false)
    {
        frame_log += "[G] ";
    }
    else if(asm_data.isOrderPreset()!=false)
    {
        frame_log += "[F] ";
    }
    else
    {
        frame_log += "[A] ";
    }
    // Log reference level.
    tmp_log.sprintf("R[%03u/%03u] ", asm_data.odd_ref, asm_data.even_ref);
    frame_log += tmp_log;
    // Log padding status.
    tmp_log.sprintf("Po[%03u/%03u] Pe[%03u/%03u] ",
                    asm_data.odd_top_padding,
                    asm_data.odd_bottom_padding,
                    asm_data.even_top_padding,
                    asm_data.even_bottom_padding);
    frame_log += tmp_log;
    // Log data block counts.
    tmp_log.sprintf("B[%02u/%02u/%02u] ",
                    asm_data.blocks_total, asm_data.blocks_fix_bp, asm_data.blocks_drop);
    frame_log += tmp_log;
    // Log sample rate.
    tmp_log.sprintf("S[%05u/%05u] ", asm_data.odd_sample_rate, asm_data.even_sample_rate);
    frame_log += tmp_log;
    // Log emphasis status.
    if(asm_data.odd_emphasis==false)
    {
        frame_log += "E[N/";
    }
    else
    {
        frame_log += "E[Y/";
    }
    if(asm_data.even_emphasis==false)
    {
        frame_log += "N]";
    }
    else
    {
        frame_log += "Y]";
    }
    return frame_log;
}

//------------------------ Make a user-log string for the frame assembly data.
QString MainWindow::logStatsFrameAsm(FrameAsmPCM16x0 asm_data)
{
    QString frame_log, tmp_log;
    FrameBinDescriptor bin_data;
    // Log frame number.
    frame_log.sprintf("PCM-16x0 F[%06u] ", asm_data.frame_number);
    // Get binarization stats for the frame.
    bin_data = getBinDescriptorByFrameNo(asm_data.frame_number);
    if(bin_data.data_coord.isSourceDoubleWidth()!=false)
    {
        bin_data.line_length = bin_data.line_length/2;
        if(bin_data.data_coord.areValid()!=false)
        {
            bin_data.data_coord.data_start = bin_data.data_coord.data_start/2;
            bin_data.data_coord.data_stop = bin_data.data_coord.data_stop/2;
        }
    }
    // Log video standard.
    if(asm_data.odd_std_lines==LINES_PER_NTSC_FIELD)
    {
        frame_log += "NTSC ";
    }
    else if(asm_data.odd_std_lines==LINES_PER_PAL_FIELD)
    {
        frame_log += "PAL  ";
    }
    else
    {
        frame_log += "     ";
    }
    // Log number of lines and vertical data coordinates.
    tmp_log.sprintf("Lo[%03u/%03u/%03u] Le[%03u/%03u/%03u] Vo[%03u/%03u] Ve[%03u/%03u] ",
                      asm_data.odd_std_lines, asm_data.odd_data_lines, asm_data.odd_valid_lines,
                      asm_data.even_std_lines, asm_data.even_data_lines, asm_data.even_valid_lines,
                      asm_data.odd_top_data, asm_data.odd_bottom_data,
                      asm_data.even_top_data, asm_data.even_bottom_data);
    frame_log += tmp_log;
    // Log horizontal data coordinates.
    tmp_log.sprintf("H[0:%03u]", bin_data.line_length);
    frame_log += tmp_log;
    if(bin_data.data_coord.areValid()==false)
    {
        frame_log += "[???:???][G] ";
    }
    else
    {
        tmp_log.sprintf("[%03d:%03d]",
                        bin_data.data_coord.data_start,
                        bin_data.data_coord.data_stop);
        frame_log += tmp_log;
        if(bin_data.data_coord.not_sure==false)
        {
            frame_log += "[A] ";
        }
        else
        {
            frame_log += "[G] ";
        }
    }
    // Log field order.
    if(asm_data.field_order==FrameAsmDescriptor::ORDER_TFF)
    {
        frame_log += "O[TFF]";
    }
    else if(asm_data.field_order==FrameAsmDescriptor::ORDER_BFF)
    {
        frame_log += "O[BFF]";
    }
    else
    {
        frame_log += "O[UNK]";
    }
    if(asm_data.isOrderGuessed()!=false)
    {
        frame_log += "[G] ";
    }
    else if(asm_data.isOrderPreset()!=false)
    {
        frame_log += "[F] ";
    }
    else
    {
        frame_log += "[A] ";
    }
    // Log reference level.
    tmp_log.sprintf("R[%03u/%03u] ", asm_data.odd_ref, asm_data.even_ref);
    frame_log += tmp_log;
    // Log padding status.
    if(asm_data.padding_ok!=false)
    {
        frame_log += "P[OK]";
    }
    else if(asm_data.silence==false)
    {
        frame_log += "P[BD]";
    }
    else
    {
        frame_log += "P[SL]";
    }
    tmp_log.sprintf("[%03u/%03u][%03u/%03u] ",
                    asm_data.odd_top_padding,
                    asm_data.odd_bottom_padding,
                    asm_data.even_top_padding,
                    asm_data.even_bottom_padding);
    frame_log += tmp_log;
    // Log data block counts.
    tmp_log.sprintf("B[%04u/%04u/%04u/%04u/%04u/%04u] ",
                    asm_data.blocks_total, asm_data.blocks_fix_bp, asm_data.blocks_fix_p, asm_data.blocks_fix_cwd,
                    asm_data.blocks_broken, asm_data.blocks_drop);
    frame_log += tmp_log;
    // Log SI/EI format.
    if(asm_data.ei_format==false)
    {
        frame_log += "D[SI] ";
    }
    else
    {
        frame_log += "D[EI] ";
    }
    // Log sample rate.
    tmp_log.sprintf("S[%05u/%05u] ", asm_data.odd_sample_rate, asm_data.even_sample_rate);
    frame_log += tmp_log;
    // Log emphasis status.
    if(asm_data.odd_emphasis==false)
    {
        frame_log += "E[N/";
    }
    else
    {
        frame_log += "E[Y/";
    }
    if(asm_data.even_emphasis==false)
    {
        frame_log += "N]";
    }
    else
    {
        frame_log += "Y]";
    }
    return frame_log;
}

//------------------------ Make a user-log string for the frame assembly data.
QString MainWindow::logStatsFrameAsm(FrameAsmSTC007 asm_data)
{
    QString frame_log, tmp_log;
    FrameBinDescriptor bin_data;
    // Log frame number.
    frame_log.sprintf("STC-007 F[%06u] ", asm_data.frame_number);
    // Get binarization stats for the frame.
    bin_data = getBinDescriptorByFrameNo(asm_data.frame_number);
    if(bin_data.data_coord.isSourceDoubleWidth()!=false)
    {
        bin_data.line_length = bin_data.line_length/2;
        if(bin_data.data_coord.areValid()!=false)
        {
            bin_data.data_coord.data_start = bin_data.data_coord.data_start/2;
            bin_data.data_coord.data_stop = bin_data.data_coord.data_stop/2;
        }
    }
    // Log video standard.
    if(asm_data.odd_std_lines==LINES_PER_NTSC_FIELD)
    {
        frame_log += "NTSC ";
    }
    else if(asm_data.odd_std_lines==LINES_PER_PAL_FIELD)
    {
        frame_log += " PAL ";
    }
    else
    {
        frame_log += "     ";
    }
    // Log number of lines and vertical data coordinates.
    tmp_log.sprintf("Lo[%03u/%03u/%03u] Le[%03u/%03u/%03u] Vo[%03u/%03u] Ve[%03u/%03u] ",
                      asm_data.odd_std_lines, asm_data.odd_data_lines, asm_data.odd_valid_lines,
                      asm_data.even_std_lines, asm_data.even_data_lines, asm_data.even_valid_lines,
                      asm_data.odd_top_data, asm_data.odd_bottom_data,
                      asm_data.even_top_data, asm_data.even_bottom_data);
    frame_log += tmp_log;
    // Log horizontal data coordinates.
    tmp_log.sprintf("H[0:%03u]", bin_data.line_length);
    frame_log += tmp_log;
    if(bin_data.data_coord.areValid()==false)
    {
        frame_log += "[???:???][G] ";
    }
    else
    {
        tmp_log.sprintf("[%03d:%03d]",
                        bin_data.data_coord.data_start,
                        bin_data.data_coord.data_stop);
        frame_log += tmp_log;
        if(bin_data.data_coord.not_sure==false)
        {
            frame_log += "[A] ";
        }
        else
        {
            frame_log += "[G] ";
        }
    }
    // Log field order.
    if(asm_data.field_order==FrameAsmDescriptor::ORDER_TFF)
    {
        frame_log += "O[TFF]";
    }
    else if(asm_data.field_order==FrameAsmDescriptor::ORDER_BFF)
    {
        frame_log += "O[BFF]";
    }
    else
    {
        frame_log += "O[UNK]";
    }
    if(asm_data.isOrderGuessed()!=false)
    {
        frame_log += "[G] ";
    }
    else if(asm_data.isOrderPreset()!=false)
    {
        frame_log += "[F] ";
    }
    else
    {
        frame_log += "[A] ";
    }
    // Log reference level.
    tmp_log.sprintf("R[%03u/%03u] ", asm_data.odd_ref, asm_data.even_ref);
    frame_log += tmp_log;
    // Log padding status.
    if(asm_data.inner_padding_ok!=false)
    {
        frame_log += "P[OK]";
    }
    else if(asm_data.inner_silence==false)
    {
        frame_log += "P[BD]";
    }
    else
    {
        frame_log += "P[SL]";
    }
    tmp_log.sprintf("[%03u]", asm_data.inner_padding);
    frame_log += tmp_log;
    if(asm_data.outer_padding_ok!=false)
    {
        frame_log += "[OK]";
    }
    else if(asm_data.outer_silence==false)
    {
        frame_log += "[BD]";
    }
    else
    {
        frame_log += "[SL]";
    }
    tmp_log.sprintf("[%03u] ", asm_data.outer_padding);
    frame_log += tmp_log;
    // Log data block counts.
    tmp_log.sprintf("B[%03u/%03u/%03u/%03u/%03u/%03u] ",
                    asm_data.blocks_total, asm_data.blocks_fix_p, asm_data.blocks_fix_q, asm_data.blocks_fix_cwd,
                    asm_data.blocks_broken_field, asm_data.blocks_drop);
    frame_log += tmp_log;
    // Log audio resolution.
    if((asm_data.odd_resolution==STC007Deinterleaver::RES_MODE_14BIT)||(asm_data.odd_resolution==STC007Deinterleaver::RES_MODE_14BIT_AUTO))
    {
        frame_log += "A[14bit/";
    }
    else
    {
        frame_log += "A[16bit/";
    }
    if((asm_data.even_resolution==STC007Deinterleaver::RES_MODE_14BIT)||(asm_data.even_resolution==STC007Deinterleaver::RES_MODE_14BIT_AUTO))
    {
        frame_log += "14bit] ";
    }
    else
    {
        frame_log += "16bit] ";
    }
    // Log index/address information.
    if(asm_data.isAddressSet()==false)
    {
        frame_log += "I[--][--:--:--.--] ";
    }
    else
    {
        tmp_log.sprintf("I[%02d][%02d:%02d:%02d.%02d] ",
                        asm_data.ctrl_index,
                        asm_data.ctrl_hour,
                        asm_data.ctrl_minute,
                        asm_data.ctrl_second,
                        asm_data.ctrl_field);
        frame_log += tmp_log;
    }
    // Log sample rate.
    tmp_log.sprintf("S[%05u/%05u] ", asm_data.odd_sample_rate, asm_data.even_sample_rate);
    frame_log += tmp_log;
    // Log emphasis status.
    if(asm_data.odd_emphasis==false)
    {
        frame_log += "E[N/";
    }
    else
    {
        frame_log += "E[Y/";
    }
    if(asm_data.even_emphasis==false)
    {
        frame_log += "N]";
    }
    else
    {
        frame_log += "Y]";
    }
    return frame_log;
}

//------------------------ Update stats and video processor with new frame assembling settings.
void MainWindow::updateStatsFrameAsm(FrameAsmPCM1 new_trim)
{
    if(new_trim.hasServiceTag()!=false)
    {
        QString log_help;
        if(new_trim.isServNewFile()!=false)
        {
            // Output log header.
            qDebug()<<WL_HEADER;
            log_help = WL_SOURCE+QString::fromStdString(new_trim.getServFileName());
            qDebug()<<log_help;
            // Reset help output.
            resetWorkLogHelpPCM1();
            // Dump help into the work log.
            do
            {
                log_help = logHelpNextPCM1();
                qDebug()<<log_help;
            }
            while(log_help.isEmpty()==false);
        }
        else if(new_trim.isServEndFile()!=false)
        {
            qDebug()<<"Source EOF";
        }
        // Skip updating counters if service tag detected.
        return;
    }
    //if(ui->cbxWaveSave->isChecked()!=false)
    {
        qDebug()<<logStatsFrameAsm(new_trim);
    }

    // Update GUI and video processor with new parameters.
    frame_asm_pcm1 = new_trim;
    frame_asm_pcm1.drawn = false;

    // Update reference level.
    stat_ref_level = frame_asm_pcm1.getAvgRef();

    // Count drops.
    if(frame_asm_pcm1.blocks_drop>0)
    {
        stat_drop_block_cnt += frame_asm_pcm1.blocks_drop;
        flag_dropout_cnt = TIM_FLAG_RED_CNT;
    }
    stat_drop_sample_cnt += frame_asm_pcm1.samples_drop;

    // Report the number of the assembled frame.
    emit newFrameAssembled(frame_asm_pcm1.frame_number);
    // Report video standard information for PCM-1.
    emit newVideoStandard(FrameAsmDescriptor::VID_NTSC);
}

//------------------------ Update stats and video processor with new frame assembling settings.
void MainWindow::updateStatsFrameAsm(FrameAsmPCM16x0 new_trim)
{
    if(new_trim.hasServiceTag()!=false)
    {
        QString log_help;
        if(new_trim.isServNewFile()!=false)
        {
            // Output log header.
            qDebug()<<WL_HEADER;
            log_help = WL_SOURCE+QString::fromStdString(new_trim.getServFileName());
            qDebug()<<log_help;
            // Reset help output.
            resetWorkLogHelpPCM16X0();
            // Dump help into the work log.
            do
            {
                log_help = logHelpNextPCM16X0();
                qDebug()<<log_help;
            }
            while(log_help.isEmpty()==false);
        }
        else if(new_trim.isServEndFile()!=false)
        {
            qDebug()<<"Source EOF";
        }
        // Skip updating counters if service tag detected.
        return;
    }
    //if(ui->cbxWaveSave->isChecked()!=false)
    {
        qDebug()<<logStatsFrameAsm(new_trim);
    }

    // Update GUI and video processor with new parameters.
    frame_asm_pcm16x0 = new_trim;
    frame_asm_pcm16x0.drawn = false;

    // Update reference level.
    stat_ref_level = frame_asm_pcm16x0.getAvgRef();

    // Count drops and fixes.
    if(frame_asm_pcm16x0.blocks_fix_p>0)
    {
        stat_p_fix_cnt += frame_asm_pcm16x0.blocks_fix_p;
        flag_p_corr_cnt = TIM_FLAG_GREEN_CNT;
    }
    if(frame_asm_pcm16x0.blocks_drop>0)
    {
        stat_drop_block_cnt += frame_asm_pcm16x0.blocks_drop;
        flag_dropout_cnt = TIM_FLAG_RED_CNT;
    }
    stat_drop_sample_cnt += frame_asm_pcm16x0.samples_drop;
    if(frame_asm_pcm16x0.blocks_broken>0)
    {
        stat_broken_block_cnt += frame_asm_pcm16x0.blocks_broken;
        flag_broken_cnt = TIM_FLAG_RED_CNT;
    }

    // Report video standard information for PCM-16x0.
    emit newVideoStandard(FrameAsmDescriptor::VID_NTSC);
    // Report the number of the assembled frame.
    emit newFrameAssembled(frame_asm_pcm16x0.frame_number);
}

//------------------------ Update stats and video processor with new frame assembling settings.
void MainWindow::updateStatsFrameAsm(FrameAsmSTC007 new_trim)
{
    if(new_trim.hasServiceTag()!=false)
    {
        QString log_help;
        if(new_trim.isServNewFile()!=false)
        {
            // Output log header.
            qDebug()<<WL_HEADER;
            log_help = WL_SOURCE+QString::fromStdString(new_trim.getServFileName());
            qDebug()<<log_help;
            // Reset help output.
            resetWorkLogHelpSTC007();
            do
            {
                log_help = logHelpNextSTC007();
                qDebug()<<log_help;
            }
            while(log_help.isEmpty()==false);
        }
        else if(new_trim.isServEndFile()!=false)
        {
            qDebug()<<"Source EOF";
        }
        // Skip updating counters if service tag detected.
        return;
    }
    //if(ui->cbxWaveSave->isChecked()!=false)
    {
        qDebug()<<logStatsFrameAsm(new_trim);
    }

    // Update GUI and video processor with new parameters.
    if(set_pcm_type==LIST_TYPE_STC007)
    {
        frame_asm_stc007 = new_trim;
        frame_asm_stc007.drawn = false;
    }
    else
    {
        frame_asm_m2 = new_trim;
        frame_asm_m2.drawn = false;
    }

    // Update reference level.
    stat_ref_level = new_trim.getAvgRef();

    // Count bad stitches.
    if(new_trim.inner_padding_ok==false)
    {
        if(new_trim.inner_silence==false)
        {
            stat_bad_stitch_cnt++;
            flag_bad_stitch_cnt = TIM_FLAG_RED_CNT;
        }
    }
    if(new_trim.outer_padding_ok==false)
    {
        if(new_trim.outer_silence==false)
        {
            stat_bad_stitch_cnt++;
            flag_bad_stitch_cnt = TIM_FLAG_RED_CNT;
        }
    }
    // Count drops and fixes.
    if(new_trim.blocks_fix_p>0)
    {
        stat_p_fix_cnt += new_trim.blocks_fix_p;
        flag_p_corr_cnt = TIM_FLAG_GREEN_CNT;
    }
    if(new_trim.blocks_fix_q>0)
    {
        stat_q_fix_cnt += new_trim.blocks_fix_q;
        flag_q_corr_cnt = TIM_FLAG_GREEN_CNT;
    }
    if(new_trim.blocks_fix_cwd>0)
    {
        stat_cwd_fix_cnt += new_trim.blocks_fix_cwd;
        flag_cwd_corr_cnt = TIM_FLAG_GREEN_CNT;
    }
    if(new_trim.blocks_drop>0)
    {
        stat_drop_block_cnt += new_trim.blocks_drop;
        flag_dropout_cnt = TIM_FLAG_RED_CNT;
    }
    stat_drop_sample_cnt += new_trim.samples_drop;
    if(new_trim.blocks_broken_field>0)
    {
        stat_broken_block_cnt += new_trim.blocks_broken_field;
        flag_broken_cnt = TIM_FLAG_RED_CNT;
    }

    // Report video standard information for STC-007/PCM-F1.
    emit newVideoStandard(new_trim.video_standard);
    // Report the number of the assembled frame.
    emit newFrameAssembled(new_trim.frame_number);
}

//------------------------ Update stats with new value for masked samples.
void MainWindow::updateStatsMaskes(uint16_t in_cnt)
{
    // Update counter.
    stat_mask_cnt += in_cnt;
}

//------------------------ Update stats after DI has finished a data block and provided spent time count.
void MainWindow::updateStatsBlockTime(STC007DataBlock in_block)
{
    // Count time spent on a frame.
    stat_blocks_time_per_frame += in_block.process_time;
    stat_blocks_per_frame++;
    if(stat_min_di_time>in_block.process_time)
    {
        stat_min_di_time = in_block.process_time;
    }
    if(stat_max_di_time<in_block.process_time)
    {
        stat_max_di_time = in_block.process_time;
    }
}

//------------------------ Update stats after DI has finished a frame.
void MainWindow::updateStatsDIFrame(uint32_t frame_no)
{
    // Update GUI.
    //stat_processed_frame_cnt++;
    stat_processed_frame_cnt = frame_no;

    if(stat_blocks_per_frame==0) stat_blocks_per_frame = 1;

    // Output logs for spent time.
    QString log_line;
    log_line = "Deinterleaved "+QString::number(stat_blocks_per_frame)+" blocks in frame "+QString::number(frame_no)+
            " by "+QString::number(stat_blocks_time_per_frame)+" us ("+QString::number(stat_blocks_time_per_frame/stat_blocks_per_frame)+" us per line)";
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[M]"<<log_line;
    }
    log_line.clear();
    log_line = "Deinterleave min/max per block: "+QString::number(stat_min_di_time)+"/"+QString::number(stat_max_di_time)+" us";
    if((log_level&LOG_PROCESS)!=0)
    {
        qInfo()<<"[M]"<<log_line;
    }

    // Reset stats.
    stat_blocks_time_per_frame = 0;
    stat_blocks_per_frame = 0;
    stat_min_di_time = 0xFFFFFFFF;
    stat_max_di_time = 0;
}


//------------------------ Perform internal test of CRCC within PCM line.
void MainWindow::testStartCRCC()
{
    // Disable action.
    ui->actTestCRCC->setEnabled(false);
    // Create new test worker in its own thread.
    QThread *ecc_tester = NULL;
    ecc_tester = new QThread;
    PCMTester *test_worker = new PCMTester();
    test_worker->moveToThread(ecc_tester);
    connect(ecc_tester, SIGNAL(started()), test_worker, SLOT(testCRCC()));
    connect(ecc_tester, SIGNAL(finished()), this, SLOT(testCRCCCleanup()));
    connect(ecc_tester, SIGNAL(finished()), ecc_tester, SLOT(deleteLater()));
    connect(test_worker, SIGNAL(testOk()), this, SLOT(testCRCCPassed()));
    connect(test_worker, SIGNAL(testFailed()), this, SLOT(testCRCCFailed()));
    connect(test_worker, SIGNAL(finished()), ecc_tester, SLOT(quit()));
    connect(test_worker, SIGNAL(finished()), test_worker, SLOT(deleteLater()));
    // Start new thread with tester.
    ecc_tester->start();
}

//------------------------ Perform internal test of ECC within PCM data block.
void MainWindow::testStartECC()
{
    // Disable action.
    ui->actTestECC->setEnabled(false);
    // Create new test worker in its own thread.
    QThread *ecc_tester = NULL;
    ecc_tester = new QThread;
    PCMTester *test_worker = new PCMTester();
    test_worker->moveToThread(ecc_tester);
    connect(ecc_tester, SIGNAL(started()), test_worker, SLOT(testDataBlock()));
    connect(ecc_tester, SIGNAL(finished()), this, SLOT(testECCCleanup()));
    connect(ecc_tester, SIGNAL(finished()), ecc_tester, SLOT(deleteLater()));
    connect(test_worker, SIGNAL(testOk()), this, SLOT(testECCPassed()));
    connect(test_worker, SIGNAL(testFailed()), this, SLOT(testECCFailed()));
    connect(test_worker, SIGNAL(finished()), ecc_tester, SLOT(quit()));
    connect(test_worker, SIGNAL(finished()), test_worker, SLOT(deleteLater()));
    // Start new thread with tester.
    ecc_tester->start();
}

//------------------------ CRCC test finished with PASSED result.
void MainWindow::testCRCCPassed()
{
    QMessageBox::information(this, tr("Результат"), tr("Тест CRCC пройден успешно."));
}

//------------------------ CRCC test finished with FAILED result.
void MainWindow::testCRCCFailed()
{
    QMessageBox::critical(this, tr("Ошибка"), tr("Тест CRCC не пройден! Подробности в отладочной консоли."));
}

//------------------------ ECC test finished with PASSED result.
void MainWindow::testECCPassed()
{
    QMessageBox::information(this, tr("Результат"), tr("Тест ECC пройден успешно."));
}

//------------------------ ECC test finished with FAILED result.
void MainWindow::testECCFailed()
{
    QMessageBox::critical(this, tr("Ошибка"), tr("Тест ECC не пройден! Подробности в отладочной консоли."));
}

//------------------------ CRCC test has finished, perform housekeeping.
void MainWindow::testCRCCCleanup()
{
    // Re-enable action to allow repeat of the test.
    ui->actTestCRCC->setEnabled(true);
}

//------------------------ ECC test has finished, perform housekeeping.
void MainWindow::testECCCleanup()
{
    // Re-enable action to allow repeat of the test.
    ui->actTestECC->setEnabled(true);
}
