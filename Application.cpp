// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "Application.hpp"
#include "ImageProvider.hpp"
#ifdef USE_COLORD
# include "Colord.hpp"
#endif

Application::Application(int &argc, char **argv)
    : APPLICATION_BASE_CLASS(argc, argv)
    , m_isWayland(platformName().startsWith(u"wayland"_sv))
    , m_isX11(platformName() == u"xcb"_sv)
{
    setApplicationName(u"qdre-viewer"_s);
    setApplicationDisplayName(u"QDRE Viewer"_s);
    setApplicationVersion(QLatin1StringView(PROJECT_VERSION));

    setWindowIcon(QIcon::fromTheme(applicationName()));
}
Application::~Application()
{
    if (Q_LIKELY(m_imageProvider))
    {
        m_imageProvider->requestWindowColorSpaceAborted();
    }
}

void Application::initialize()
{
    const QCommandLineOption softwareOption(u"software"_s, u"Force software rendering."_s);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Simple image viewer with wide-gamut and 10-bit support"_s);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(softwareOption);
    parser.addPositionalArgument(u"file"_s, u"The image file to open."_s, u"[file...]"_s);
    parser.process(*this);

    QImageReader::setAllocationLimit(0);

    if (const auto configPath = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation).value(0); !configPath.isEmpty())
    {
        m_configPath = QUrl::fromLocalFile(configPath + u"/"_sv + applicationName() + u".ini"_sv);
    }

    if (!m_isWayland && !m_isX11)
    {
        qWarning().nospace() << "Unsupported platform: " << platformName() << ", expect issues";
    }

#ifdef USE_WIDGETS
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE") && qEnvironmentVariableIntValue("KDE_SESSION_VERSION") == 6)
    {
        QQuickStyle::setStyle(u"org.kde.breeze"_s);
    }
#endif

    if (parser.isSet(softwareOption))
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }
    else if (const auto rhiBackend = QSettings(m_configPath.toLocalFile(), QSettings::Format::IniFormat).value("rhiBackend").toString(); !rhiBackend.isEmpty())
    {
        if (rhiBackend == u"software"_sv)
        {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        }
        else if (rhiBackend == u"vulkan"_sv)
        {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
        }
        else if (rhiBackend == u"opengl"_sv)
        {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        }
    }

    const bool software = (QQuickWindow::graphicsApi() == QSGRendererInterface::Software || QQuickWindow::sceneGraphBackend() == u"software"_sv);

    if (software)
    {
        qWarning() << "Running in software renderer mode, 10-bit output will not work";
    }

#ifndef USE_COLORD
    if (m_isX11)
    {
        m_displayColorManagementSupported = false;
        m_displayColorManagementEnabled = false;
    }
#endif

    m_imageProvider = new ImageProvider(software);
    connect(m_imageProvider, &ImageProvider::requestWindowColorSpace,
            this, &Application::requestViewerWindowColorSpace);
    connect(m_imageProvider, &ImageProvider::imageMetadataChanged,
            this, &Application::processImageMetadata);
    connect(m_imageProvider, &ImageProvider::animationFrameDelayChanged,
            this, &Application::animationFrameDelayChanged);

    connect(&m_dirNavigation, &DirectoryNavigation::infoChanged,
            this, [this] {
        tie(m_directoryPosition, m_directoryCount) = m_dirNavigation.getInfo();
        emit directoryPositionOrCountChanged();
    });

    m_engine.addImageProvider(u"qdre"_s, m_imageProvider);
    qmlRegisterSingletonInstance("QDREViewer", 1, 0, "Singleton", this);
    QObject::connect(&m_engine, &QQmlApplicationEngine::objectCreationFailed, this, [] {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    m_engine.loadFromModule("QDREViewer", "Main");

    openFile(parser.positionalArguments().value(0));
}

void Application::setMainWindow(QWindow *win)
{
    if (m_mainWin == win)
    {
        return;
    }

    m_mainWin = win;

#ifdef USE_COLORD
    if (m_isX11 && win)
    {
        connect(win, &QWindow::screenChanged,
                this, &Application::applyColordScreenColorSpace);
        connect(this, &QGuiApplication::applicationStateChanged,
                this, [this](Qt::ApplicationState state) {
            if (auto *mainWin = m_mainWin.get(); mainWin && state == Qt::ApplicationActive)
            {
                applyColordScreenColorSpace(mainWin->screen());
            }
        });
    }
#endif
}

void Application::setupViewerWindow(QWindow *win)
{
    if (m_viewerWin == win)
    {
        return;
    }

    m_brokenColorSpaces.clear();

    if (!win)
    {
        m_viewerWin.clear(); // Explicitly clear the pointer
        return;
    }

    m_viewerWin = win;

    const bool hasWinHandle = static_cast<bool>(win->handle());

    setupViewerWindowColorSpace(QColorSpace::NamedColorSpace::SRgb);

    if (!hasWinHandle)
    {
        win->destroy();
    }
}

void Application::setDisplayColorManagementEnabled(bool displayColorManagementEnabled)
{
    if (!m_displayColorManagementSupported)
    {
        return;
    }

    m_displayColorManagementEnabled = displayColorManagementEnabled;

    bool mustReload = !m_isWayland && m_useGammaForSRgb;

#ifdef USE_COLORD
    if (auto *mainWin = m_mainWin.get())
    {
        if (applyColordScreenColorSpace(mainWin->screen()))
        {
            mustReload = false;
        }
    }
#endif

    if (mustReload)
    {
        emit reload();
    }
}
void Application::setUseGammaForSRgb(bool useGammaForSRgb)
{
    m_useGammaForSRgb = useGammaForSRgb;
}

void Application::updateWindowColor()
{
    auto *mainWin = m_mainWin.get();
    auto *win = m_viewerWin.get();
    if (mainWin && win)
    {
        auto mainWinColor = QColor(mainWin->property("color").toString());
        if (!mainWinColor.isValid())
        {
            mainWinColor = Qt::black;
        }

        auto winColorSpace = win->format().colorSpace();
        if (!winColorSpace.isValid())
        {
            winColorSpace = QColorSpace::NamedColorSpace::SRgb;
        }

        win->setProperty("color", QColorSpace(QColorSpace::NamedColorSpace::SRgb).transformationToColorSpace(winColorSpace).map(mainWinColor));
    }
}

void Application::setViewerCursorPos(const QPoint &pos)
{
    auto *mainWin = m_mainWin.get();
    if (!mainWin)
    {
        return;
    }

    QCursor::setPos(
        mainWin->screen(),
        (m_isWayland && !m_viewerWin.isNull())
            ? mainWin->mapFromGlobal(pos)
            : pos
    );
}

void Application::openFile(const QString &filePath)
{
    if (filePath.isEmpty())
    {
        return;
    }

    if (QMimeDatabase().mimeTypeForFile(filePath).name() == DirectoryNavigation::s_iccProfileMimeType)
    {
        if (QFile f(filePath); f.size() <= static_cast<qint64>(1 << 24) && f.open(QFile::ReadOnly))
        {
            if (auto colorSpace = QColorSpace::fromIccProfile(f.readAll()); colorSpace.isValid())
            {
                m_imageProvider->setImageColorSpaceOverride(colorSpace);
                m_hasImageColorSpaceOverride = true;
                emit hasImageColorSpaceOverrideChanged();
                emit reload();
            }
        }
    }
    else if (const QFileInfo fi(filePath); fi.isFile())
    {
        m_dirNavigation.setDirAndCurrentFile(fi.dir(), fi.fileName());
        emit loadFile(filePath);
    }
}

void Application::navigateBegin()
{
    openFile(m_dirNavigation.navigate(DirectoryNavigation::Navigate::Begin));
}
void Application::navigateEnd()
{
    openFile(m_dirNavigation.navigate(DirectoryNavigation::Navigate::End));
}
void Application::navigatePrev()
{
    openFile(m_dirNavigation.navigate(DirectoryNavigation::Navigate::Prev));
}
void Application::navigateNext()
{
    openFile(m_dirNavigation.navigate(DirectoryNavigation::Navigate::Next));
}

bool Application::moveToTrash(const QString &filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }
    return QFile::moveToTrash(filePath);
}
void Application::removeFromFileList(const QString &filePath)
{
    m_dirNavigation.removeFromFileList(filePath);
}

void Application::resetImageColorSpaceOverride()
{
    m_imageProvider->setImageColorSpaceOverride();
    m_hasImageColorSpaceOverride = false;
    emit hasImageColorSpaceOverrideChanged();
    emit reload();
}

bool Application::isRecreatingViewerWindow() const
{
    return m_recreatingViewerWindow;
}

bool Application::hasThemeIcon(const QString &name)
{
    return QIcon::hasThemeIcon(name);
}

QString Application::getImageSourceTriggerSeq(bool sourceTrigger, bool animation)
{
    return QString::number(
        sourceTrigger
            ? animation
              ? to_underlying(ImageProvider::ImageSourceTriggerSeq::Ani1)
              : to_underlying(ImageProvider::ImageSourceTriggerSeq::Still1)
            : animation
              ? to_underlying(ImageProvider::ImageSourceTriggerSeq::Ani0)
              : to_underlying(ImageProvider::ImageSourceTriggerSeq::Still0)
    );
}

bool Application::verifyDropUrls(const QList<QUrl> &urls)
{
    if (urls.size() != 1)
    {
        return false;
    }

    const auto url = urls[0];
    if (!url.isLocalFile())
    {
        return false;
    }

    return QFileInfo(url.toLocalFile()).isFile();
}

QString Application::urlToLocalFile(const QUrl &url)
{
    return url.toLocalFile();
}
QString Application::getFileName(const QString &filePath)
{
    return QFileInfo(filePath).fileName();
}
QUrl Application::getDirectoryPathUrl(const QString &filePath)
{
    return QUrl::fromLocalFile(QFileInfo(filePath).path());
}

void Application::requestViewerWindowColorSpace(const QColorSpace &cs)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    QColorSpace targetColorSpace;
    bool deepColorNotAvailable {};

    auto *win = m_viewerWin.get();

    if (win && m_isWayland && Q_LIKELY(m_displayColorManagementEnabled) && !m_brokenColorSpaces.contains(QColorSpace::NamedColorSpace::SRgb))
    {
        const bool hasWinHandle = static_cast<bool>(win->handle());

        QColorSpace wantedCs(QColorSpace::NamedColorSpace::SRgb);
        switch (cs.primaries())
        {
            using enum QColorSpace::Primaries;
            case SRgb:
                break;
            case AdobeRgb:
            case DciP3D65:
                wantedCs.setPrimaries(cs.primaries());
                break;
            default:
                if (cs.colorModel() != QColorSpace::ColorModel::Gray)
                {
                    wantedCs.setPrimaries(Bt2020);
                }
                break;
        }
        switch (cs.transferFunction())
        {
            using enum QColorSpace::TransferFunction;
            case St2084:
            case Hlg:
                wantedCs.setTransferFunction(cs.transferFunction());
                break;
            default:
                break;
        }

        if (win->format().colorSpace() != wantedCs && !setupViewerWindowColorSpace(wantedCs))
        {
            if (wantedCs.transferFunction() != QColorSpace::TransferFunction::SRgb)
            {
                wantedCs.setTransferFunction(QColorSpace::TransferFunction::SRgb);
                setupViewerWindowColorSpace(wantedCs);
            }
        }
        targetColorSpace = win->format().colorSpace();

        if (!hasWinHandle)
        {
            win->destroy();
        }
    }
#ifdef USE_COLORD
    else
    {
        if (m_displayColorManagementEnabled && m_colordColorSpace.isValid())
        {
            targetColorSpace = m_colordColorSpace;
        }
        m_colordCanReload = true;
    }
#endif

    auto isFormat8bit = [](const QSurfaceFormat &format) {
        return (format.redBufferSize() <= 8 && format.greenBufferSize() <= 8 && format.blueBufferSize() <= 8);
    };
    if (win)
    {
        deepColorNotAvailable = isFormat8bit(win->format());
    }
    else if (auto *mainWin = m_mainWin.get())
    {
        deepColorNotAvailable = isFormat8bit(mainWin->format());
    }

    const bool useGammaForSRgbUsable = (targetColorSpace.transferFunction() == QColorSpace::TransferFunction::SRgb
                                        && cs.transferFunction() == QColorSpace::TransferFunction::SRgb);
    if (m_useGammaForSRgbUsable != useGammaForSRgbUsable)
    {
        m_useGammaForSRgbUsable = useGammaForSRgbUsable;
        emit useGammaForSRgbUsableChanged();
    }

    m_imageProvider->requestWindowColorSpaceDone(
        targetColorSpace,
        deepColorNotAvailable,
        m_displayColorManagementEnabled && m_useGammaForSRgb && !m_hasImageColorSpaceOverride
    );
}

bool Application::setupViewerWindowColorSpace(const QColorSpace &cs)
{
    auto *win = m_viewerWin.get();
    if (!win)
    {
        return false;
    }

    if (m_brokenColorSpaces.contains(cs))
    {
        // Deep color should be already set when we're here
        return false;
    }

    auto setColorOnRet = qScopeGuard([this] {
        updateWindowColor();
    });

    if (!m_isWayland)
    {
        return false;
    }

    m_recreatingViewerWindow = true;

    win->destroy();

    auto format = win->format();

    // Request deep color for OpenGL RHI
    format.setRedBufferSize(10);
    format.setGreenBufferSize(10);
    format.setBlueBufferSize(10);
    format.setAlphaBufferSize(2);

    // Request wanted color space
    format.setColorSpace(cs);

    win->setFormat(format);

    // Request deep color for Vulkan RHI
    win->setProperty("_qt_sg_hdr_format", "hdr10"_ba);

    win->create();

    m_recreatingViewerWindow = false;

    if (win->format().colorSpace() != cs)
    {
        if (cs == QColorSpace::NamedColorSpace::SRgb)
        {
            qInfo() << "Color management is not supported";
        }
        else
        {
            qWarning() << "Unable to set viewer window color space:" << cs;
        }
        m_brokenColorSpaces.append(cs);
        return false;
    }

    return true;
}

#ifdef USE_COLORD
bool Application::applyColordScreenColorSpace(QScreen *screen)
{
    if (Q_UNLIKELY(!screen) || !m_isX11)
    {
        return false;
    }

    QColorSpace colordColorSpace;
    if (m_displayColorManagementEnabled)
    {
        colordColorSpace = Colord::getScreenColorSpace(screen->name());
    }
    if (m_colordColorSpace != colordColorSpace)
    {
        m_colordColorSpace = std::move(colordColorSpace);
        if (m_colordCanReload)
        {
            emit reload();
            return true;
        }
    }

    return false;
}
#endif

void Application::processImageMetadata(
        const QString &fileName,
        const QSize &size,
        const QColorSpace &colorSpace,
        QImage::Format format,
        const shared_ptr<Exiv2::Image> &exiv2,
        bool imageTransformed)
{
    bool hflip = false;
    bool vflip = false;
    double rotation = 0.0;

    QString text;
    text.reserve(4096);

    auto getTagStringBuilder = [](const QString &tag, const QString &data) {
        return u"<p><b>"_sv + tag + u"</b><br/>&nbsp;"_sv + data + u"</p>"_sv;
    };
    auto prependTag = [&](const QString &tag, const QString &data) {
        text.prepend(getTagStringBuilder(tag, data));
    };
    auto appendTag = [&](const QString &tag, const QString &data) {
        text.append(getTagStringBuilder(tag, data));
    };

    if (!fileName.isEmpty())
    {
        appendTag(tr("File name"), fileName);
    }

    if (size.isValid())
    {
        if (colorSpace.isValid())
        {
            QString colorSpaceName;
            const auto labelCs = tr("Color space");
            switch (colorSpace.primaries())
            {
                case QColorSpace::Primaries::SRgb:
                    colorSpaceName = u"sRGB"_s;
                    break;
                case QColorSpace::Primaries::AdobeRgb:
                    colorSpaceName = u"Adobe RGB"_s;
                    break;
                case QColorSpace::Primaries::DciP3D65:
                    if (colorSpace.transferFunction() == QColorSpace::TransferFunction::SRgb)
                    {
                        colorSpaceName = u"Display P3"_s;
                    }
                    else
                    {
                        colorSpaceName = u"DCI-P3 D65"_s;
                    }
                    break;
                case QColorSpace::Primaries::ProPhotoRgb:
                    colorSpaceName = u"ProPhoto RGB"_s;
                    break;
                case QColorSpace::Primaries::Bt2020:
                    colorSpaceName = u"BT.2020"_s;
                    break;
                default:
                    if (colorSpace.colorModel() == QColorSpace::ColorModel::Cmyk)
                    {
                        colorSpaceName = u"CMYK"_s;
                    }
                    else if (colorSpace.colorModel() != QColorSpace::ColorModel::Gray)
                    {
                        appendTag(labelCs, tr("Custom"));
                    }
                    break;
            }
            if (!colorSpaceName.isEmpty())
            {
                appendTag(labelCs, colorSpaceName);
            }

            const auto labelTf = tr("Transfer function");
            switch (colorSpace.transferFunction())
            {
                case QColorSpace::TransferFunction::Linear:
                    appendTag(labelTf, tr("Linear"));
                    break;
                case QColorSpace::TransferFunction::Gamma:
                    if (colorSpace.primaries() != QColorSpace::Primaries::AdobeRgb || !qFuzzyCompare(colorSpace.gamma(), 563.0f / 256.0f))
                    {
                        appendTag(labelTf, u"Gamma %1"_s.arg(colorSpace.gamma()));
                    }
                    break;
                case QColorSpace::TransferFunction::SRgb:
                    if (colorSpace.primaries() != QColorSpace::Primaries::SRgb && colorSpace.primaries() != QColorSpace::Primaries::DciP3D65)
                    {
                        appendTag(labelTf, u"sRGB"_s);
                    }
                    break;
                case QColorSpace::TransferFunction::ProPhotoRgb:
                    appendTag(labelTf, u"ProPhoto RGB"_s);
                    break;
                case QColorSpace::TransferFunction::Bt2020:
                    appendTag(labelTf, u"BT.2020"_s);
                    break;
                case QColorSpace::TransferFunction::St2084:
                    appendTag(labelTf, u"PQ"_s);
                    break;
                case QColorSpace::TransferFunction::Hlg:
                    appendTag(labelTf, u"HLG"_s);
                    break;
                default:
                    appendTag(labelTf, tr("Custom"));
                    break;
            }

            if (const auto iccDescription = colorSpace.description().remove(u'\0').trimmed();
                    !iccDescription.isEmpty() && iccDescription != colorSpaceName)
            {
                appendTag(tr("Color profile name"), iccDescription);
            }
        }

        if (format != QImage::Format_Invalid)
        {
            const auto pixelFormat = QImage::toPixelFormat(format);
            switch (pixelFormat.colorModel())
            {
                case QPixelFormat::Grayscale:
                    appendTag(tr("Depth"), u"%1 bit (grayscale)"_s.arg(pixelFormat.bitsPerPixel()));
                    break;
                case QPixelFormat::Indexed:
                    appendTag(tr("Depth"), tr("Indexed", "Depth"));
                    break;
                default:
                    appendTag(tr("Depth"), u"%1 bit"_s.arg(min({pixelFormat.redSize(), pixelFormat.greenSize(), pixelFormat.blueSize()})));
                    break;
            }
            if (pixelFormat.alphaUsage() == QPixelFormat::UsesAlpha)
            {
                appendTag(tr("Alpha"), u"%1 bit"_s.arg(pixelFormat.alphaSize()));
            }
        }

        if (exiv2)
        {
            const auto &tags = exiv2->exifData();

            const initializer_list<pair<QStringView, QString>> exifDataArr {
                {u"Exif.Photo.DateTimeOriginal"_sv, tr("Original date")},
                {u"Exif.Image.Make"_sv,             tr("Camera make")},
                {u"Exif.Image.Model"_sv,            tr("Camera model")},
                {u"Exif.Photo.ExposureMode"_sv,     tr("Exposure mode")},
                {u"Exif.Photo.ExposureTime"_sv,     tr("Exposure time")},
                {u"Exif.Photo.FNumber"_sv,          tr("Aperture")},
                {u"Exif.Photo.ISOSpeedRatings"_sv,  tr("ISO")},
                {u"Exif.*.LensModel"_sv,            tr("Lens model")},
                {u"Exif.*.Lens"_sv,                 tr("Lens")},
                {u"Exif.Photo.FocalLength"_sv,      tr("Focal length")},
                {u"Exif.Photo.Flash"_sv,            tr("Flash")},
                {u"Exif.Photo.MeteringMode"_sv,     tr("Metering mode")},
                {u"Exif.Photo.WhiteBalance"_sv,     tr("White balance")},
                {u"Exif.Photo.DigitalZoomRatio"_sv, tr("Digital zoom ratio")},
                {u"Exif.Photo.SceneCaptureType"_sv, tr("Scene capture type")},
            };
            for (const auto &exifData : exifDataArr)
            {
                QRegularExpression rx(QRegularExpression::wildcardToRegularExpression(exifData.first));
                for (auto it = tags.begin(), itEnd = tags.end(); it != itEnd; ++it)
                {
                    if (rx.match(QString::fromLatin1(it->key())).hasMatch())
                    {
                        ostringstream oss;
                        oss << *it;
                        appendTag(exifData.second, QString::fromLocal8Bit(oss.str()).replace(u'\n', u' '));
                        break;
                    }
                }
            }

            if (auto orientationIt = tags.findKey(Exiv2::ExifKey("Exif.Image.Orientation"s)); orientationIt != tags.end() && orientationIt->count() > 0)
            {
                enum class ImageOrientation : quint32
                {
                    Unspecified = 0,
                    Normal      = 1,
                    Hflip       = 2,
                    Rot180      = 3,
                    Vflip       = 4,
                    HflipRot270 = 5,
                    Rot90       = 6,
                    HflipRot90  = 7,
                    Rot270      = 8,
                };
                switch (static_cast<ImageOrientation>(orientationIt->value().toUint32()))
                {
                    case ImageOrientation::Unspecified:
                    case ImageOrientation::Normal:
                    {
                        break;
                    }
                    case ImageOrientation::Hflip:
                    {
                        hflip = true;
                        break;
                    }
                    case ImageOrientation::Rot180:
                    {
                        rotation = 180.0;
                        break;
                    }
                    case ImageOrientation::Vflip:
                    {
                        vflip = true;
                        break;
                    }
                    case ImageOrientation::HflipRot270:
                    {
                        hflip = true;
                        rotation = 270.0;
                        break;
                    }
                    case ImageOrientation::Rot90:
                    {
                        rotation = 90.0;
                        break;
                    }
                    case ImageOrientation::HflipRot90:
                    {
                        hflip = true;
                        rotation = 90.0;
                        break;
                    }
                    case ImageOrientation::Rot270:
                    {
                        rotation = 270.0;
                        break;
                    }
                    default:
                    {
                        ostringstream oss;
                        oss << *orientationIt;
                        qWarning() << "Unimplemented image orientation:" << QString::fromLocal8Bit(oss.str());
                        break;
                    }
                }
            }
        }

        auto sizeToDisplay = size;
        if (imageTransformed)
        {
            if (rotation == 90.0 || rotation == 270.0)
            {
                sizeToDisplay.transpose();
            }
            hflip = false;
            vflip = false;
            rotation = 0.0;
        }
        QString imageSizeStr = QString::number(sizeToDisplay.width()) + u" x "_sv + QString::number(sizeToDisplay.height());
        const auto mPix = (static_cast<double>(sizeToDisplay.width()) * static_cast<double>(sizeToDisplay.height())) / 1e6;
        if (mPix >= 0.1)
        {
            imageSizeStr += u" ("_sv + QString::number(mPix, 'f', 1) + u" MP)"_sv;
        }
        prependTag(tr("Image size"), imageSizeStr);
    }

    emit metadataChanged(hflip, vflip, rotation, text);
}

/**/

int main(int argc, char *argv[])
{
    // These cause issues
    qunsetenv("QSG_RENDER_LOOP");
    qunsetenv("QSG_NO_VSYNC");

    Application app(argc, argv);
    app.initialize();
    return app.exec();
}
