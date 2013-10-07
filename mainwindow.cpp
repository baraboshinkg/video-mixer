/*
 * Copyright (C) 2013 Irkutsk Diagnostic Center.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mainwindow.h"
#include "product.h"
#include "aboutdialog.h"
#include "archivewindow.h"
#include "qwaitcursor.h"
#include "settings.h"
#include "videosettings.h"
#include "patientdialog.h"

#ifdef WITH_DICOM
#include "dicom/worklist.h"
#include "dicom/dcmclient.h"
#include "dicom/transcyrillic.h"
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#endif

#ifdef WITH_TOUCH
#include "touch/slidingstackedwidget.h"
#endif

#include <QApplication>
#include <QBoxLayout>
#include <QDesktopServices>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#include <QGlib/Connect>
#include <QGlib/Type>
#include <QGst/Bus>
#include <QGst/Clock>
#include <QGst/ElementFactory>
#include <QGst/Event>
#include <QGst/Parse>
#include <gst/gstdebugutils.h>
#include <cairo/cairo-gobject.h>

namespace QGlib
{
    template <>
    struct GetTypeImpl<cairo_t*>
    {
        inline operator Type() { return CAIRO_GOBJECT_TYPE_CONTEXT; };
    };
}

static inline QBoxLayout::Direction bestDirection(const QSize &s)
{
    return s.width() >= s.height()? QBoxLayout::LeftToRight: QBoxLayout::TopToBottom;
}

#ifdef QT_DEBUG

static void Dump(QGst::ElementPtr elm)
{
    if (!elm)
    {
        qDebug() << " (null) ";
        return;
    }

    foreach (auto prop, elm->listProperties())
    {
        const QString n = prop->name();
        const QGlib::Value v = elm->property(n.toUtf8());
        switch (v.type().fundamental())
        {
        case QGlib::Type::Boolean:
            qDebug() << n << " = " << v.get<bool>();
            break;
        case QGlib::Type::Float:
        case QGlib::Type::Double:
            qDebug() << n << " = " << v.get<double>();
            break;
        case QGlib::Type::Enum:
        case QGlib::Type::Flags:
        case QGlib::Type::Int:
        case QGlib::Type::Uint:
            qDebug() << n << " = " << v.get<int>();
            break;
        case QGlib::Type::Long:
        case QGlib::Type::Ulong:
            qDebug() << n << " = " << v.get<long>();
            break;
        case QGlib::Type::Int64:
        case QGlib::Type::Uint64:
            qDebug() << n << " = " << v.get<qint64>();
            break;
        default:
            qDebug() << n << " = " << v.get<QString>();
            break;
        }
    }

    QGst::ChildProxyPtr childProxy =  elm.dynamicCast<QGst::ChildProxy>();
    if (childProxy)
    {
        auto cnt = childProxy->childrenCount();
        for (uint i = 0; i < cnt; ++i)
        {
            qDebug() << "==== CHILD ==== " << i;
            Dump(childProxy->childByIndex(i).dynamicCast<QGst::Element>());
        }
    }
}

#endif

MainWindow::MainWindow(QWidget *parent) :
    QWidget(parent),
    archiveWindow(nullptr),
#ifdef WITH_DICOM
    pendingPatient(nullptr),
    worklist(nullptr),
#endif
    imageNo(0),
    clipNo(0),
    studyNo(0),
    overlayWidth(0),
    overlayHeight(0),
    running(false),
    recording(false)
{
    QSettings settings;
    updateWindowTitle();

    // This magic required for updating widgets from worker threads on Microsoft (R) Windows (TM)
    //
    connect(this, SIGNAL(enableWidget(QWidget*, bool)), this, SLOT(onEnableWidget(QWidget*, bool)), Qt::QueuedConnection);

    auto layoutMain = new QVBoxLayout();
#ifndef WITH_TOUCH
    layoutMain->addWidget(createToolBar());
#endif
    listImagesAndClips = new QListWidget();
    listImagesAndClips->setViewMode(QListView::IconMode);
    listImagesAndClips->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    listImagesAndClips->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listImagesAndClips->setMinimumHeight(144); // 576/4
    listImagesAndClips->setMaximumHeight(160);
    listImagesAndClips->setIconSize(QSize(144,144));
    listImagesAndClips->setMovement(QListView::Static);
    listImagesAndClips->setWrapping(false);

    displayWidget = new QGst::Ui::VideoWidget();
    displayWidget->setMinimumSize(712, 576);
    displayWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
#ifdef WITH_TOUCH
    mainStack = new SlidingStackedWidget();
    layoutMain->addWidget(mainStack);

#ifdef WITH_DICOM
    worklist = new Worklist();
    connect(worklist, SIGNAL(startStudy(DcmDataset*)), this, SLOT(onStartStudy(DcmDataset*)));
    mainStack->addWidget(worklist);
#endif
    auto studyLayout = new QVBoxLayout;
    studyLayout->addWidget(displayWidget);
    studyLayout->addWidget(listImagesAndClips);
    studyLayout->addWidget(createToolBar());
    auto studyWidget = new QWidget;
    studyWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    studyWidget->setLayout(studyLayout);
    mainStack->addWidget(studyWidget);
    mainStack->setCurrentWidget(studyWidget);
    mainStack->setProperty("MainWidget", mainStack->indexOf(studyWidget));

    archiveWindow = new ArchiveWindow();
    archiveWindow->updateRoot();
    mainStack->addWidget(archiveWindow);
#else
    layoutMain->addWidget(displayWidget);
    layoutMain->addWidget(listImagesAndClips);
#endif

    if (settings.value("enable-menu", false).toBool())
    {
        layoutMain->setMenuBar(createMenuBar());
    }
    setLayout(layoutMain);

    restoreGeometry(settings.value("mainwindow-geometry").toByteArray());
    setWindowState((Qt::WindowState)settings.value("mainwindow-state").toInt());

    updateStartButton();
    updateRecordButton();

    updatePipeline();

    if (settings.value("first-run", true).toBool())
    {
        settings.setValue("first-run", false);
        QTimer::singleShot(0, this, SLOT(onShowSettingsClick()));
    }
}

MainWindow::~MainWindow()
{
    if (pipeline)
    {
        releasePipeline();
    }

    delete archiveWindow;
    archiveWindow = nullptr;

#ifdef WITH_DICOM
    delete worklist;
    worklist = nullptr;
#endif
}

void MainWindow::closeEvent(QCloseEvent *evt)
{
    if (archiveWindow)
    {
        archiveWindow->close();
    }
#ifdef WITH_DICOM
    if (worklist)
    {
        worklist->close();
    }
#endif

    QSettings settings;
    settings.setValue("mainwindow-geometry", saveGeometry());
    settings.setValue("mainwindow-state", (int)windowState() & ~Qt::WindowMinimized);
    QWidget::closeEvent(evt);
}

QMenuBar* MainWindow::createMenuBar()
{
    auto mnuBar = new QMenuBar();
    auto mnu    = new QMenu(tr("&Menu"));

    mnu->addAction(actionAbout);
    actionAbout->setMenuRole(QAction::AboutRole);

    actionArchive->setShortcut(Qt::Key_F2);
    mnu->addAction(actionArchive);

#ifdef WITH_DICOM
    actionWorklist->setShortcut(Qt::Key_F3);
    mnu->addAction(actionWorklist);
#endif
    mnu->addSeparator();
    auto actionRtp = mnu->addAction(tr("&Enable RTP streaming"), this, SLOT(toggleSetting()));
    actionRtp->setCheckable(true);
    actionRtp->setData("enable-rtp");

    auto actionFullVideo = mnu->addAction(tr("&Record entire study"), this, SLOT(toggleSetting()));
    actionFullVideo->setCheckable(true);
    actionFullVideo->setData("enable-video");

    actionSettings->setShortcut(Qt::Key_F9);
    actionSettings->setMenuRole(QAction::PreferencesRole);
    mnu->addAction(actionSettings);
    mnu->addSeparator();
    auto actionExit = mnu->addAction(tr("E&xit"), qApp, SLOT(quit()), Qt::ALT | Qt::Key_F4);
    actionExit->setMenuRole(QAction::QuitRole);

    connect(mnu, SIGNAL(aboutToShow()), this, SLOT(prepareSettingsMenu()));
    mnuBar->addMenu(mnu);

    mnuBar->show();
    return mnuBar;
}

QToolBar* MainWindow::createToolBar()
{
    QToolBar* bar = new QToolBar(tr("Main"));

    btnStart = new QToolButton();
    btnStart->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btnStart->setFocusPolicy(Qt::NoFocus);
    btnStart->setMinimumWidth(175);
    connect(btnStart, SIGNAL(clicked()), this, SLOT(onStartClick()));
    bar->addWidget(btnStart);

    btnSnapshot = new QToolButton();
    btnSnapshot->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btnSnapshot->setFocusPolicy(Qt::NoFocus);
    btnSnapshot->setIcon(QIcon(":/buttons/camera"));
    btnSnapshot->setText(tr("&Take snapshot"));
    btnSnapshot->setMinimumWidth(175);
    connect(btnSnapshot, SIGNAL(clicked()), this, SLOT(onSnapshotClick()));
    bar->addWidget(btnSnapshot);

    btnRecord = new QToolButton();
    btnRecord->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btnRecord->setFocusPolicy(Qt::NoFocus);
    btnRecord->setMinimumWidth(175);
    connect(btnRecord, SIGNAL(clicked()), this, SLOT(onRecordClick()));
    bar->addWidget(btnRecord);

    QWidget* spacer = new QWidget;
    spacer->setMinimumWidth(1);
    spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    bar->addWidget(spacer);

#ifdef WITH_DICOM
    actionWorklist = bar->addAction(QIcon(":/buttons/show_worklist"), tr("&Worlkist"), this, SLOT(onShowWorkListClick()));
#endif

    actionArchive = bar->addAction(QIcon(":/buttons/database"), tr("&Archive"), this, SLOT(onShowArchiveClick()));
    actionArchive->setToolTip(tr("Show studies archive"));

    actionSettings = bar->addAction(QIcon(":/buttons/settings"), tr("&Preferences").append(0x2026), this, SLOT(onShowSettingsClick()));
    actionSettings->setToolTip(tr("Edit settings"));

    actionAbout = bar->addAction(QIcon(":/buttons/about"), tr("A&bout %1").arg(PRODUCT_FULL_NAME).append(0x2026), this, SLOT(onShowAboutClick()));
    actionAbout->setToolTip(tr("About %1").arg(PRODUCT_FULL_NAME));

    return bar;
}

/*

  The pipeline is:

                 [video src]
                     |
                     V
         +----[main splitter]-----+
         |            |           |
  [image valve]       |       [video valve]
         |            |           |
         V            V           V
 [image encoder]  [display]  [video encoder]
        |                         |
        V                         V
  [image writer]      +----[video splitter]----+
                      |           |            |
                      V           V            V
           [movie writer]  [clip valve]  [rtp sender]
                                  |
                                  V
                          [clip writer]


Sample:
    v4l2src ! autoconvert ! tee name=splitter
        ! autovideosink name=displaysink async=0 splitter.
        ! valve name=encvalve drop=1 ! queue max-size-bytes=0 ! x264enc name=videoencoder ! tee name=videosplitter
                ! identity  name=videoinspect drop-probability=1.0 ! queue ! valve name=videovalve drop=1 ! [mpegpsmux name=videomux ! filesink name=videosink] videosplitter.
                ! queue ! rtph264pay ! udpsink name=rtpsink clients=127.0.0.1:5000 sync=0 videosplitter.
                ! identity  name=clipinspect drop-probability=1.0 ! queue ! valve name=clipvalve ! [ mpegpsmux name=clipmux ! filesink name=clipsink] videosplitter.
        splitter.
        ! identity name=imagevalve drop-probability=1.0 ! jpegenc ! multifilesink name=imagesink post-messages=1 async=0 sync=0 location=/video/image splitter.
*/

QString MainWindow::buildPipeline()
{
    QSettings settings;

    // v4l2src device=/dev/video1 ! video/x-raw-yuv,format=(fourcc)YUY2,width=(int)720,height=(int)576 ! ffmpegcolorspace
    //
    QString pipe;

    auto deviceDef = settings.value("device").toString();
    auto formatDef = settings.value("format").toString();
    auto sizeDef   = settings.value("size").toSize();
    auto srcDef   = settings.value("src").toString();
    auto srcFixColor = settings.value("src-colorspace", false).toBool()? "! ffmpegcolorspace ": "";
    auto srcDeinterlace = settings.value("video-deinterlace").toBool();
    auto srcParams = settings.value("src-parameters").toString();

    if (!srcDef.isEmpty())
    {
        pipe.append(srcDef);
    }
    else
    {
        pipe.append(PLATFORM_SPECIFIC_SOURCE);
        if (!deviceDef.isEmpty())
        {
            pipe.append(" " PLATFORM_SPECIFIC_PROPERTY "=\"").append(deviceDef).append("\"");
        }
        if (!srcParams.isEmpty())
        {
            pipe.append(" ").append(srcParams);
        }
        if (srcDeinterlace)
        {
            pipe.append(" ! deinterlace");
        }
    }

    if (!formatDef.isEmpty())   
    {
        pipe.append(" ! ").append(formatDef);
        if (!sizeDef.isEmpty())
        {
            pipe = pipe.append(",width=(int)%1,height=(int)%2").arg(sizeDef.width()).arg(sizeDef.height());
        }
    }

    pipe.append(srcFixColor);

    // v4l2src ... ! tee name=splitter ! ffmpegcolorspace ! ximagesink splitter.");
    //
    auto displaySinkDef  = settings.value("display-sink",  "autovideosink name=displaysink async=0").toString();
    auto displayFixColor = settings.value(displaySinkDef + "-colorspace", false).toBool();
    auto displayParams   = settings.value(displaySinkDef + "-parameters").toString();
    auto enableVideo     = settings.value("enable-video").toBool();
    pipe.append(" ! tee name=splitter");
    if (!displaySinkDef.isEmpty())
    {
        if (enableVideo)
        {
            pipe.append(" ! ffmpegcolorspace ! cairooverlay name=displayoverlay");
        }
        if (enableVideo || displayFixColor)
        {
            pipe.append(" ! ffmpegcolorspace");
        }

        pipe.append(" ! " ).append(displaySinkDef).append(" ").append(displayParams).append(" splitter.");
    }

    // ... ! tee name=splitter ! ximagesink splitter. ! valve name=encvalve ! ffmpegcolorspace ! x264enc
    //           ! tee name=videosplitter
    //                ! queue ! mpegpsmux ! filesink videosplitter.  ! queue ! x264 videosplitter.
    //                ! queue ! rtph264pay ! udpsink videosplitter.
    //                ! identity name=clipinspect ! queue ! mpegpsmux ! filesink videosplitter.
    //           splitter.
    //
    auto outputPathDef      = settings.value("output-path",   "/video").toString();
    auto videoEncoderDef    = settings.value("video-encoder", "x264enc").toString();
    auto videoFixColor      = settings.value(videoEncoderDef + "-colorspace", false).toBool()? "ffmpegcolorspace ! ": "";
    auto videoEncoderParams = settings.value(videoEncoderDef + "-parameters").toString();
    auto rtpPayDef          = settings.value("rtp-payloader", "rtph264pay").toString();
    auto rtpPayParams       = settings.value(rtpPayDef + "-parameters").toString();
    auto rtpSinkDef         = settings.value("rtp-sink",      "udpsink clients=127.0.0.1:5000 sync=0").toString();
    auto rtpSinkParams      = settings.value(rtpSinkDef + "-parameters").toString();
    auto enableRtp          = !rtpSinkDef.isEmpty() && settings.value("enable-rtp").toBool();

    pipe.append(" ! valve name=encvalve drop=1 ! queue max-size-bytes=0 ! ").append(videoFixColor)
            .append(videoEncoderDef).append(" name=videoencoder ").append(videoEncoderParams);
    if (enableRtp || enableVideo)
    {
        pipe.append(" ! tee name=videosplitter");
        if (enableVideo)
        {
            pipe.append(" ! identity name=videoinspect drop-probability=1.0 ! queue ! valve name=videovalve videosplitter.");
        }
        if (enableRtp)
        {
            pipe.append(" ! queue ! ").append(rtpPayDef).append(" ").append(rtpPayParams)
                .append(" ! ").append(rtpSinkDef).append(" name=rtpsink videosplitter.").append(" ").append(rtpSinkParams);
        }
    }

    pipe.append(" ! identity name=clipinspect drop-probability=1.0 ! queue ! valve name=clipvalve drop=1 ");
    if (enableRtp || enableVideo)
        pipe.append(" videosplitter.");
    pipe.append(" splitter.");

    // ... ! tee name=splitter ... splitter. ! identity name=imagevalve ! jpegenc ! multifilesink splitter.
    //
    auto imageEncoderDef = settings.value("image-encoder", "jpegenc").toString();
    auto imageEncoderFixColor = settings.value(imageEncoderDef + "-colorspace", false).toBool()?
                "ffmpegcolorspace ! ": "";
    auto imageEncoderParams = settings.value(imageEncoderDef + "-parameters").toString();
    auto imageSinkDef    = settings.value("image-sink", "multifilesink name=imagesink post-messages=1 async=0 sync=0").toString();
    if (!imageSinkDef.isEmpty())
    {
        pipe.append(" ! identity name=imagevalve drop-probability=1.0 ! ")
            .append(imageEncoderFixColor).append(imageEncoderDef).append(" ").append(imageEncoderParams).append(" ! ")
            .append(imageSinkDef).append(" location=").append(outputPathDef).append("/image splitter.");
    }

    return pipe;
}

QGst::PipelinePtr MainWindow::createPipeline()
{
    qCritical() << pipelineDef;

    QGst::PipelinePtr pl;

    // Default values for all profiles
    //

    try
    {
        pl = QGst::Parse::launch(pipelineDef).dynamicCast<QGst::Pipeline>();
    }
    catch (QGlib::Error ex)
    {
        errorGlib(pl, ex);
    }

    if (pl)
    {
        QGlib::connect(pl->bus(), "message", this, &MainWindow::onBusMessage);
        pl->bus()->addSignalWatch();
        displayWidget->watchPipeline(pl);

        displaySink = pl->getElementByName("displaysink");
        if (!displaySink)
        {
            qCritical() << "Element displaysink not found";
        }

        auto displayOverlay = pl->getElementByName("displayoverlay");
        displayOverlay && QGlib::connect(displayOverlay, "caps-changed", this, &MainWindow::onCapsOverlay)
             && QGlib::connect(displayOverlay, "draw", this, &MainWindow::onDrawOverlay);

        auto clipValve = pl->getElementByName("clipinspect");
        clipValve && QGlib::connect(clipValve, "handoff", this, &MainWindow::onClipFrame);

        auto videoValve = pl->getElementByName("videoinspect");
        videoValve && QGlib::connect(videoValve, "handoff", this, &MainWindow::onVideoFrame);

        videoEncoderValve  = pl->getElementByName("encvalve");
        if (!videoEncoderValve)
        {
            qCritical() << "Element encvalve not found";
        }

        imageValve = pl->getElementByName("imagevalve");
        imageValve && QGlib::connect(imageValve, "handoff", this, &MainWindow::onImageReady);

        imageSink  = pl->getElementByName("imagesink");
        if (!imageSink)
        {
            qCritical() << "Element imagesink not found";
        }

        // To set correct bitrate we must examine default bitrate first
        //
        videoEncoder = pl->getElementByName("videoencoder");
        if (!videoEncoder)
        {
            qCritical() << "Element videoencoder not found";
        }

        auto details = GstDebugGraphDetails(GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE | GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS | GST_DEBUG_GRAPH_SHOW_STATES);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(pl.staticCast<QGst::Bin>(), details, qApp->applicationName().toUtf8());

        // The pipeline will start once it reaches paused state without an error
        //
        pl->setState(QGst::StatePaused);
    }

    return pl;
}

void MainWindow::setElementProperty(const char* elmName, const char* prop, const QGlib::Value& value, QGst::State minimumState)
{
    auto elm = pipeline? pipeline->getElementByName(elmName): QGst::ElementPtr();
    if (!elm)
    {
        qDebug() << "Element " << elmName << " not found";
    }
    else
    {
        setElementProperty(elm, prop, value, minimumState);
    }
}

void MainWindow::setElementProperty(QGst::ElementPtr& elm, const char* prop, const QGlib::Value& value, QGst::State minimumState)
{
    if (elm)
    {
        QGst::State currentState = QGst::StateVoidPending;
        elm->getState(&currentState, nullptr, 1000000000L); // 1 sec
        if (currentState > minimumState)
        {
            elm->setState(minimumState);
            elm->getState(nullptr, nullptr, 1000000000L);
        }
        if (prop)
        {
            elm->setProperty(prop, value);
        }
        elm->setState(currentState);
        elm->getState(nullptr, nullptr, 1000000000L);
    }
}

// mpegpsmux => mpg, jpegenc => jpg, pngenc => png, oggmux => ogg, avimux => avi, matrosskamux => mat
//
static QString getExt(QString str)
{
    if (str.startsWith("ffmux_"))
    {
        str = str.mid(6);
    }
    return QString(".").append(str.remove('e').left(3));
}

static QString fixFileName(QString str)
{
    if (!str.isNull())
    {
        for (int i = 0; i < str.length(); ++i)
        {
            switch (str[i].unicode())
            {
            case '<':
            case '>':
            case ':':
            case '\"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                str[i] = '_';
                break;
            }
        }
    }
    return str;
}

QString MainWindow::replace(QString str, int seqNo)
{
    auto nn = seqNo >= 10? QString::number(seqNo): QString("0").append('0' + seqNo);
    auto ts = QDateTime::currentDateTime();

    return str
        .replace("%name%",      patientName,         Qt::CaseInsensitive)
        .replace("%id%",        patientId,           Qt::CaseInsensitive)
        .replace("%physician%", physician,           Qt::CaseInsensitive)
        .replace("%study%",     studyName,           Qt::CaseInsensitive)
        .replace("%yyyy%",      ts.toString("yyyy"), Qt::CaseInsensitive)
        .replace("%yy%",        ts.toString("yy"),   Qt::CaseInsensitive)
        .replace("%mm%",        ts.toString("MM"),   Qt::CaseInsensitive)
        .replace("%mmm%",       ts.toString("MMM"),  Qt::CaseInsensitive)
        .replace("%mmmm%",      ts.toString("MMMM"), Qt::CaseInsensitive)
        .replace("%dd%",        ts.toString("dd"),   Qt::CaseInsensitive)
        .replace("%ddd%",       ts.toString("ddd"),  Qt::CaseInsensitive)
        .replace("%dddd%",      ts.toString("dddd"), Qt::CaseInsensitive)
        .replace("%hh%",        ts.toString("hh"),   Qt::CaseInsensitive)
        .replace("%min%",       ts.toString("mm"),   Qt::CaseInsensitive)
        .replace("%ss%",        ts.toString("ss"),   Qt::CaseInsensitive)
        .replace("%zzz%",       ts.toString("zzz"),  Qt::CaseInsensitive)
        .replace("%ap%",        ts.toString("ap"),   Qt::CaseInsensitive)
        .replace("%nn%",        nn,                  Qt::CaseInsensitive)
        ;
}

void MainWindow::updatePipeline()
{
    QWaitCursor wait(this);
    QSettings settings;

    auto newPipelineDef = buildPipeline();
    if (newPipelineDef != pipelineDef)
    {
        qDebug() << "The pipeline has been changed, restarting";
        if (pipeline)
        {
            releasePipeline();
        }
        pipelineDef = newPipelineDef;
        pipeline = createPipeline();
    }

    setElementProperty("rtpsink", "clients", settings.value("rtp-clients").toString(), QGst::StateReady);

    if (videoEncoder)
    {
        auto videoEncBitrate = settings.value("bitrate", DEFAULT_VIDEOBITRATE).toInt();
        // To set correct bitrate we must examine default bitrate first
        //
        auto currentBitrate = videoEncoder->property("bitrate").toInt();
        if (currentBitrate > 200000)
        {
            // The codec uses bits per second instead of kbits per second
            //
            videoEncBitrate *= 1024;
        }
        setElementProperty(videoEncoder, "bitrate", videoEncBitrate);
    }

    if (archiveWindow != nullptr)
    {
        archiveWindow->updateRoot();
    }

#ifdef WITH_DICOM
    actionWorklist->setEnabled(!settings.value("mwl-server").toString().isEmpty());
#endif
}

void MainWindow::updateWindowTitle()
{
    QString windowTitle(PRODUCT_FULL_NAME);
    if (running)
    {
        if (!patientId.isEmpty())
        {
            windowTitle.append(tr(" - ")).append(patientId);
        }

        if (!patientName.isEmpty())
        {
            windowTitle.append(tr(" - ")).append(patientName);
        }

        if (!physician.isEmpty())
        {
            windowTitle.append(tr(" - ")).append(physician);
        }

        if (!studyName.isEmpty())
        {
            windowTitle.append(tr(" - ")).append(studyName);
        }
    }

    setWindowTitle(windowTitle);
}

void MainWindow::updateOutputPath()
{
    QSettings settings;
    auto tpl = settings.value("output-path", "/video").toString();
    if (!patientId.isEmpty())
    {
        tpl.append(settings.value("folder-template", "/%yyyy%-%MM%/%dd%/%name%/").toString());
    }

    outputPath.setPath(replace(tpl, ++studyNo));

    if (!outputPath.mkpath("."))
    {
        QString msg = tr("Failed to create '%1'").arg(outputPath.absolutePath());
        qCritical() << msg;
        QMessageBox::critical(this, windowTitle(), msg, QMessageBox::Ok);
    }
}

void MainWindow::releasePipeline()
{
    pipeline->setState(QGst::StateNull);
    pipeline->getState(nullptr, nullptr, 10000000000L); // 10 sec

    displaySink.clear();
    imageValve.clear();
    imageSink.clear();
    videoEncoderValve.clear();
    videoEncoder.clear();

    pipeline.clear();
    displayWidget->stopPipelineWatch();
}

void MainWindow::onClipFrame(const QGst::BufferPtr& buf)
{
    if (0 != (buf->flags() & GST_BUFFER_FLAG_DELTA_UNIT))
    {
        return;
    }

    enableWidget(btnRecord, true);

    // Once we got an I-Frame, open second valve
    //
    setElementProperty("clipvalve", "drop", false);

    if (!clipPreviewFileName.isEmpty())
    {
        // Once an image will be ready, the valve will be turned off again.
        //
        enableWidget(btnSnapshot, false);

        // Take a picture for thumbnail
        //
        setElementProperty(imageSink, "location", clipPreviewFileName, QGst::StateReady);

        // Turn the valve on for a while.
        //
        imageValve->setProperty("drop-probability", 0.0);
    }
}

void MainWindow::onVideoFrame(const QGst::BufferPtr& buf)
{
    if (0 != (buf->flags() & GST_BUFFER_FLAG_DELTA_UNIT))
    {
        return;
    }

    enableWidget(btnStart, true);

    // Once we got an I-Frame, open second valve
    //
    setElementProperty("videovalve", "drop", false);
}

void MainWindow::onCapsOverlay(const QGst::CapsPtr& caps)
{
    QGst::StructurePtr s = caps->internalStructure(0);
    if (s)
    {
        overlayWidth = s->value("width").toInt();
        overlayHeight = s->value("height").toInt();
    }
    else
    {
        overlayWidth = overlayHeight = 0;
    }
}

void MainWindow::onDrawOverlay(cairo_t* cr, quint64 timestamp, quint64 /*duration*/)
{
    if (!running || !overlayWidth || !overlayHeight)
    {
        return;
    }

    if (1 & (timestamp / 750000000))
    {
        // skip every odd second
        //
        return;
    }

    int size = overlayHeight / 30;
    cairo_translate (cr, overlayWidth - size, size);

    cairo_set_line_width(cr, 2);
    cairo_set_source_rgb(cr, recording? 0.0: 0.25, recording? 0.25: 0.0, 0);

    cairo_arc(cr, 0, 0, size / 2, 0, 2 * M_PI);
    cairo_stroke_preserve(cr);

    cairo_set_source_rgb(cr, recording? 0.0: 0.8, recording? 0.8: 0.0, 0.0);
    cairo_fill(cr);
}

void MainWindow::onImageReady(const QGst::BufferPtr& buf)
{
    qDebug() << "imageValve handoff " << buf->size() << " " << buf->timeStamp() << " " << buf->flags();
    imageValve->setProperty("drop-probability", 1.0);
}

void MainWindow::errorGlib(const QGlib::ObjectPtr& obj, const QGlib::Error& ex)
{
    const QString msg = obj?
        QString().append(obj->property("name").toString()).append(" ").append(ex.message()):
        ex.message();
    qCritical() << msg;
    QMessageBox::critical(this, windowTitle(), msg, QMessageBox::Ok);
}

void MainWindow::onBusMessage(const QGst::MessagePtr& message)
{
    //qDebug() << message->typeName() << " " << message->source()->property("name").toString();

    switch (message->type())
    {
    case QGst::MessageStateChanged:
        onStateChangedMessage(message.staticCast<QGst::StateChangedMessage>());
        break;
    case QGst::MessageElement:
        onElementMessage(message.staticCast<QGst::ElementMessage>());
        break;
    case QGst::MessageError:
        errorGlib(message->source(), message.staticCast<QGst::ErrorMessage>()->error());
        break;
#ifdef QT_DEBUG
    case QGst::MessageInfo:
        qDebug() << message->source()->property("name").toString() << " " << message.staticCast<QGst::InfoMessage>()->error();
        break;
    case QGst::MessageWarning:
        qDebug() << message->source()->property("name").toString() << " " << message.staticCast<QGst::WarningMessage>()->error();
        break;
    case QGst::MessageEos:
    case QGst::MessageNewClock:
    case QGst::MessageStreamStatus:
    case QGst::MessageQos:
    case QGst::MessageAsyncDone:
        break;
    default:
        qDebug() << message->type();
        break;
#else
    default: // Make the compiler happy
        break;
#endif
    }
}

void MainWindow::onStateChangedMessage(const QGst::StateChangedMessagePtr& message)
{
//  qDebug() << message->oldState() << " => " << message->newState();

    // The pipeline will not start by itself since 2 of 3 renders are in NULL state
    // We need to kick off the display renderer to start the capture
    //
    if (message->oldState() == QGst::StateReady && message->newState() == QGst::StatePaused)
    {
        message->source().staticCast<QGst::Element>()->setState(QGst::StatePlaying);
    }
    else if (message->newState() == QGst::StateNull && message->source() == pipeline)
    {
        // The display area of the main window is filled with some garbage.
        // We need to redraw the contents.
        //
        update();
    }
}

void MainWindow::onElementMessage(const QGst::ElementMessagePtr& message)
{
    const QGst::StructurePtr s = message->internalStructure();
    if (!s)
    {
        qDebug() << "Got empty QGst::MessageElement";
        return;
    }

    if (s->name() == "GstMultiFileSink" && message->source() == imageSink)
    {
        QString fileName = s->value("filename").toString();
        QString toolTip = fileName;
        QPixmap pm;

        auto lastBuffer = message->source()->property("last-buffer").get<QGst::BufferPtr>();
        bool ok = lastBuffer && pm.loadFromData(lastBuffer->data(), lastBuffer->size());

        // If we can not load from the buffer, try to load from the file
        //
        if (!ok && !pm.load(fileName))
        {
            toolTip = tr("Failed to load image %1").arg(fileName);
            pm.load(":/buttons/stop");
        }

        if (clipPreviewFileName == fileName)
        {
            // Got a snapshot for a clip file. Add a fency overlay to it
            //
            QPixmap pmOverlay(":/buttons/film");
            QPainter painter(&pm);
            painter.setOpacity(0.75);
            painter.drawPixmap(pm.rect(), pmOverlay);
            clipPreviewFileName.clear();
        }

        auto existent = listImagesAndClips->findItems(QFileInfo(fileName).baseName(), Qt::MatchExactly);
        auto item = !existent.isEmpty()? existent.at(0):
            new QListWidgetItem(QFileInfo(fileName).baseName(), listImagesAndClips);

        item->setToolTip(toolTip);
        item->setIcon(QIcon(pm));
        listImagesAndClips->setItemSelected(item, true);
        listImagesAndClips->scrollToItem(item);

        btnSnapshot->setEnabled(running);
        return;
    }

    if (s->name() == "prepare-xwindow-id" || s->name() == "prepare-window-handle")
    {
        // At this time the video output finally has a sink, so set it up now
        //
        QGst::ElementPtr sink = displayWidget->videoSink();
        if (sink)
        {
            sink->setProperty("force-aspect-ratio", TRUE);
            displayWidget->update();
        }
        return;
    }

    qDebug() << "Got unknown message " << s->name() << " from " << message->source()->property("name").toString();
}

bool MainWindow::startVideoRecord()
{
    if (QSettings().value("enable-video").toBool())
    {
        auto videoFileName = appendVideoTail("video", ++studyNo);
        if (videoFileName.isEmpty())
        {
            QMessageBox::critical(this, windowTitle(), tr("Failed to start recording.\nCheck the error log for details."), QMessageBox::Ok);
            return false;
        }

        // Until the real clip recording starts, we should disable this button
        //
        btnStart->setEnabled(false);
    }

    return true;
}

void MainWindow::onStartClick()
{
    if (!running)
    {
        onStartStudy();
        return;
    }

    int userChoice;
#ifdef WITH_DICOM
    QSettings settings;

    if (pendingPatient && !settings.value("storage-servers").toStringList().isEmpty())
    {
        userChoice = QMessageBox::question(this, windowTitle(),
           tr("Send study results to the server?"), tr("Continue the study"), tr ("Don't sent"), tr("Send"), 2, 0);

        if (userChoice == 0)
        {
            // Continue the study
            //
            return;
        }
    }
    else
#endif
    {
        userChoice = QMessageBox::question(this, windowTitle(),
           tr("End the study?"), QMessageBox::Yes | QMessageBox::Default, QMessageBox::No);

        if (userChoice == QMessageBox::No)
        {
            // Don't end the study
            //
            return;
        }
    }

    QWaitCursor wait(this);
    if (recording)
    {
        onRecordClick();
    }
    running = recording = false;
    updateRecordButton();
    removeVideoTail("video");
    updateWindowTitle();

#ifdef WITH_DICOM

    if (pendingPatient)
    {
        char seriesUID[100] = {0};
        dcmGenerateUniqueIdentifier(seriesUID, SITE_SERIES_UID_ROOT);

        if (!pendingSOPInstanceUID.isEmpty() && settings.value("complete-with-mpps", true).toBool())
        {
            DcmClient client(UID_ModalityPerformedProcedureStepSOPClass);
            if (!client.nSetRQ(seriesUID, pendingPatient, pendingSOPInstanceUID))
            {
                QMessageBox::critical(this, windowTitle(), client.lastError());
            }
        }

        if (userChoice == 2)
        {
            DcmClient client;
            client.sendToServer(this, pendingPatient, outputPath.entryInfoList(QDir::Files | QDir::Readable), seriesUID);
        }
    }

    delete pendingPatient;
    pendingPatient = nullptr;
    pendingSOPInstanceUID.clear();

#endif

    setElementProperty(videoEncoderValve, "drop", !running);

    updateStartButton();
    displayWidget->update();
}

void MainWindow::onSnapshotClick()
{
    QSettings settings;
    QString imageExt = getExt(settings.value("image-encoder", "jpegenc").toString());
    QString imageFileName = replace(settings.value("image-template", "image-%study%-%nn%").toString(), ++imageNo).append(imageExt);

    setElementProperty(imageSink, "location", outputPath.absoluteFilePath(imageFileName), QGst::StateReady);

    // Turn the valve on for a while.
    //
    imageValve->setProperty("drop-probability", 0.0);
    //
    // Once an image will be ready, the valve will be turned off again.
    btnSnapshot->setEnabled(false);
}

QString MainWindow::appendVideoTail(const QString& prefix, int idx)
{
    QSettings settings;
    auto muxDef  = settings.value("video-muxer", "mpegpsmux").toString();
    auto sinkDef = settings.value(prefix + "-sink",   "filesink").toString();

    auto inspect = pipeline->getElementByName((prefix + "inspect").toUtf8());
    if (!inspect)
    {
        qDebug() << "Required element '" << prefix << "inspect'" << " is missing";
        return nullptr;
    }

    auto valve   = pipeline->getElementByName((prefix + "valve").toUtf8());
    if (!valve)
    {
        qDebug() << "Required element '" << prefix << "valve'" << " is missing";
        return nullptr;
    }

    auto mux     = QGst::ElementFactory::make(muxDef, (prefix + "mux").toUtf8());
    if (!mux)
    {
        qDebug() << "Failed to create element '" << prefix << "mux'" << " (" << muxDef << ")";
        return nullptr;
    }

    auto sink    = QGst::ElementFactory::make(sinkDef, (prefix + "sink").toUtf8());
    if (!sink)
    {
        qDebug() << "Failed to create element '" << prefix << "sink'" << " (" << sinkDef << ")";
        return nullptr;
    }

    pipeline->add(mux, sink);
    if (!QGst::Element::linkMany(valve, mux, sink))
    {
        qDebug() << "Failed to link elements altogether";
        return nullptr;
    }

    // Manually increment video/clip file name
    //
    QString videoExt = getExt(muxDef);
    QString clipFileName = replace(settings.value(prefix + "-template", prefix + "-%study%-%nn%").toString(), idx).append(videoExt);
    auto absPath = outputPath.absoluteFilePath(clipFileName);
    sink->setProperty("location", absPath);
    mux->setState(QGst::StatePaused);
    sink->setState(QGst::StatePaused);
    valve->setProperty("drop", true);
    inspect->setProperty("drop-probability", 0.0);
    return absPath;
}

void MainWindow::removeVideoTail(const QString& prefix)
{
    auto inspect = pipeline->getElementByName((prefix + "inspect").toUtf8());
    auto valve   = pipeline->getElementByName((prefix + "valve").toUtf8());
    auto mux     = pipeline->getElementByName((prefix + "mux").toUtf8());
    auto sink    = pipeline->getElementByName((prefix + "sink").toUtf8());

    if (!mux || !sink)
    {
        return;
    }

    inspect->setProperty("drop-probability", 1.0);
    valve->setProperty("drop", true);

    sink->setState(QGst::StateNull);
    sink->getState(nullptr, nullptr, 1000000000L);
    mux->setState(QGst::StateNull);
    mux->getState(nullptr, nullptr, 1000000000L);

    QGst::Element::unlinkMany(valve, mux, sink);

    pipeline->remove(mux);
    pipeline->remove(sink);
}

void MainWindow::onRecordClick()
{
    if (!recording)
    {
        QString imageExt = getExt(QSettings().value("image-encoder", "jpegenc").toString());
        auto clipFileName = appendVideoTail("clip", ++clipNo);
        if (!clipFileName.isEmpty())
        {
            clipPreviewFileName = clipFileName.append(imageExt);

            // Until the real clip recording starts, we should disable this button
            //
            btnRecord->setEnabled(false);
            recording = true;
        }
        else
        {
            QMessageBox::critical(this, windowTitle(), tr("Failed to start recording.\nCheck the error log for details."), QMessageBox::Ok);
        }
    }
    else
    {
        removeVideoTail("clip");
        clipPreviewFileName.clear();
        recording = false;
    }

    updateRecordButton();
}

void MainWindow::updateStartButton()
{
    QIcon icon(running? ":/buttons/stop": ":/buttons/start");
    QString strOnOff(running? tr("End &study"): tr("Start &study"));
    btnStart->setIcon(icon);
    btnStart->setText(strOnOff);

    btnRecord->setEnabled(running);
    btnSnapshot->setEnabled(running);
    actionSettings->setEnabled(!running);
#ifdef WITH_DICOM
    if (worklist)
    {
        worklist->setDisabled(running);
    }
#endif
}

void MainWindow::updateRecordButton()
{
    QIcon icon(recording? ":/buttons/pause": ":/buttons/record");
    QString strOnOff(recording? tr("Paus&e"): tr("R&ecord"));
    btnRecord->setIcon(icon);
    btnRecord->setText(strOnOff);
}

void MainWindow::prepareSettingsMenu()
{
    QSettings settings;

    auto menu = static_cast<QMenu*>(sender());
    foreach (auto action, menu->actions())
    {
        auto propName = action->data().toString();
        if (!propName.isEmpty())
        {
            action->setChecked(settings.value(propName).toBool());
            action->setDisabled(running);
        }
    }
}

void MainWindow::toggleSetting()
{
    QSettings settings;
    auto propName = static_cast<QAction*>(sender())->data().toString();
    bool enable = !settings.value(propName).toBool();
    settings.setValue(propName, enable);
    updatePipeline();
}

void MainWindow::onShowAboutClick()
{
    AboutDialog(this).exec();
}

void MainWindow::onShowArchiveClick()
{
#ifndef WITH_TOUCH
    if (archiveWindow == nullptr)
    {
        archiveWindow = new ArchiveWindow();
        archiveWindow->updateRoot();
    }
#endif

    updateOutputPath();
    archiveWindow->setPath(outputPath.absolutePath());
#ifdef WITH_TOUCH
    mainStack->slideInWidget(archiveWindow);
#else
    archiveWindow->show();
    archiveWindow->activateWindow();
#endif
}

void MainWindow::onShowSettingsClick()
{
    Settings dlg(this);
    connect(&dlg, SIGNAL(apply()), this, SLOT(updatePipeline()));
    if (dlg.exec())
    {
        updatePipeline();

#ifdef WITH_DICOM
        // Recreate worklist just in case the columns/servers were changed
        //
        delete worklist;
        worklist = nullptr;
#endif
    }
}

void MainWindow::onEnableWidget(QWidget* widget, bool enable)
{
    widget->setEnabled(enable);
}

#ifdef WITH_DICOM
void MainWindow::onStartStudy(DcmDataset* patient)
#else
void MainWindow::onStartStudy()
#endif
{
    // Switch focus to the main window
    //
    show();
    activateWindow();

    QWaitCursor wait(this);

    QSettings settings;
    listImagesAndClips->clear();
    imageNo = clipNo = 0;

    PatientDialog dlg(this);

#ifdef WITH_DICOM
    if (patient)
    {
        const char *str = nullptr;
        auto trans = settings.value("translate-cyrillic", true).toBool();
        if (patient->findAndGetString(DCM_PatientID, str, true).good())
        {
            dlg.setPatientId(QString::fromUtf8(str));
        }

        if (patient->findAndGetString(DCM_PatientName, str, true).good())
        {
            dlg.setPatientName(trans? translateToCyrillic(QString::fromUtf8(str)): QString::fromUtf8(str));
        }

        if (patient->findAndGetString(DCM_PatientBirthDate, str, true).good())
        {
            dlg.setPatientBirthDate(QDate::fromString(QString::fromUtf8(str), "yyyyMMdd"));
        }

        if (patient->findAndGetString(DCM_PatientSex, str, true).good())
        {
            dlg.setPatientSex(QString::fromUtf8(str));
        }

        if (patient->findAndGetString(DCM_ScheduledPerformingPhysicianName, str, true).good())
        {
            dlg.setPhysician(trans? translateToCyrillic(QString::fromUtf8(str)): QString::fromUtf8(str));
        }

        if (patient->findAndGetString(DCM_ScheduledProcedureStepDescription, str, true).good())
        {
            dlg.setStudyName(QString::fromUtf8(str));
        }

        dlg.setEditable(false);
    }
#endif

    switch (dlg.exec())
    {
#ifdef WITH_DICOM
    case SHOW_WORKLIST_RESULT:
        onShowWorkListClick();
        // passthrouht
#endif
    case QDialog::Rejected:
        return;
    }

    patientId   = fixFileName(dlg.patientId());
    patientName = fixFileName(dlg.patientName());
    physician   = fixFileName(dlg.physician());
    studyName   = fixFileName(dlg.studyName());

    updateOutputPath();

    // After updateOutputPath the outputPath is usable
    //
#ifdef WITH_DICOM
    if (patient)
    {
        pendingPatient = new DcmDataset(*patient);
        OFString cp;
        if (pendingPatient->findAndGetOFString(DCM_SpecificCharacterSet, cp).bad() || cp.length() == 0)
        {
            pendingPatient->putAndInsertString(DCM_SpecificCharacterSet, "ISO_IR 192");
        }
    }
    else
    {
        pendingPatient = new DcmDataset();
        pendingPatient->putAndInsertString(DCM_SpecificCharacterSet, "ISO_IR 192");
        pendingPatient->putAndInsertString(DCM_PatientID, dlg.patientId().toUtf8());
        pendingPatient->putAndInsertString(DCM_PatientName, dlg.patientName().toUtf8());
        pendingPatient->putAndInsertString(DCM_PatientBirthDate, dlg.patientBirthDate().toString("yyyyMMdd").toUtf8());
        pendingPatient->putAndInsertString(DCM_PatientSex, QString().append(dlg.patientSexCode()).toUtf8());

        DcmItem* sps = nullptr;
        pendingPatient->findOrCreateSequenceItem(DCM_ScheduledProcedureStepSequence, sps);
        if (sps)
        {
            sps->putAndInsertString(DCM_ScheduledPerformingPhysicianName, dlg.physician().toUtf8());
            sps->putAndInsertString(DCM_ScheduledProcedureStepDescription, dlg.studyName().toUtf8());
        }
    }

    OFString studyInstanceUID;
    char uuid[100] = {0};
    if (pendingPatient->findAndGetOFString(DCM_StudyInstanceUID, studyInstanceUID).bad() || studyInstanceUID.length() == 0)
    {
        pendingPatient->putAndInsertString(DCM_StudyInstanceUID, dcmGenerateUniqueIdentifier(uuid, SITE_STUDY_UID_ROOT));
    }

#if __BYTE_ORDER == __LITTLE_ENDIAN
    const E_TransferSyntax writeXfer = EXS_LittleEndianImplicit;
#elif __BYTE_ORDER == __BIG_ENDIAN
    const E_TransferSyntax writeXfer = EXS_BigEndianImplicit;
#else
#error "Unsupported byte order"
#endif

    auto cond = pendingPatient->saveFile((const char*)outputPath.absoluteFilePath(".patient.dcm").toLocal8Bit(), writeXfer);
    if (cond.bad())
    {
        QMessageBox::critical(this, windowTitle(), QString::fromLocal8Bit(cond.text()));
        return;
    }

    if (settings.value("start-with-mpps", true).toBool() && !settings.value("mpps-server").toString().isEmpty())
    {
        DcmClient client(UID_ModalityPerformedProcedureStepSOPClass);
        pendingSOPInstanceUID = client.nCreateRQ(pendingPatient);
        if (pendingSOPInstanceUID.isNull())
        {
            QMessageBox::critical(this, windowTitle(), client.lastError());
            return;
        }
        pendingPatient->putAndInsertString(DCM_SOPInstanceUID, pendingSOPInstanceUID.toUtf8());
    }
    else
    {
        pendingPatient->putAndInsertString(DCM_SOPInstanceUID, dcmGenerateUniqueIdentifier(uuid, SITE_INSTANCE_UID_ROOT));
    }
#else
    dlg.savePatientFile(outputPath.absoluteFilePath(".patient"));
#endif

    running = startVideoRecord();

    setElementProperty(videoEncoderValve, "drop", !running);

    updateStartButton();
    updateWindowTitle();
    displayWidget->update();
}

#ifdef WITH_DICOM
void MainWindow::onShowWorkListClick()
{
#ifndef WITH_TOUCH
    if (worklist == nullptr)
    {
        worklist = new Worklist();
        connect(worklist, SIGNAL(startStudy(DcmDataset*)), this, SLOT(onStartStudy(DcmDataset*)));
    }
    worklist->show();
    worklist->activateWindow();
#else
    mainStack->slideInWidget(worklist);
#endif
}
#endif
