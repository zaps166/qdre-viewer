// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "Headers.hpp"
#include "ImageProvider.hpp"
#include "DirectoryNavigation.hpp"

class Application : public APPLICATION_BASE_CLASS
{
    Q_OBJECT

    QML_NAMED_ELEMENT(Singleton)
    QML_SINGLETON

    Q_PROPERTY(bool isWayland MEMBER m_isWayland CONSTANT FINAL)

    Q_PROPERTY(QUrl configPath MEMBER m_configPath CONSTANT FINAL)

    Q_PROPERTY(bool displayColorManagementSupported MEMBER m_displayColorManagementSupported CONSTANT FINAL)
    Q_PROPERTY(bool useGammaForSRgbUsable MEMBER m_useGammaForSRgbUsable NOTIFY useGammaForSRgbUsableChanged FINAL)
    Q_PROPERTY(bool hasImageColorSpaceOverride MEMBER m_hasImageColorSpaceOverride NOTIFY hasImageColorSpaceOverrideChanged FINAL)

    Q_PROPERTY(qsizetype directoryPosition MEMBER m_directoryPosition NOTIFY directoryPositionOrCountChanged FINAL)
    Q_PROPERTY(qsizetype directoryCount MEMBER m_directoryCount NOTIFY directoryPositionOrCountChanged FINAL)

public:
    Application(int &argc, char **argv);
    ~Application();

    void initialize();

public:
    Q_INVOKABLE void setMainWindow(QWindow *win);

    Q_INVOKABLE void setupViewerWindow(QWindow *win);

    Q_INVOKABLE void setDisplayColorManagementEnabled(bool displayColorManagementEnabled);
    Q_INVOKABLE void setUseGammaForSRgb(bool useGammaForSRgb);

    Q_INVOKABLE void updateWindowColor();

    Q_INVOKABLE void setViewerCursorPos(const QPoint &pos);

    Q_INVOKABLE void openFile(const QString &filePath);

    Q_INVOKABLE void navigateBegin();
    Q_INVOKABLE void navigateEnd();
    Q_INVOKABLE void navigatePrev();
    Q_INVOKABLE void navigateNext();

    Q_INVOKABLE bool moveToTrash(const QString &filePath);
    Q_INVOKABLE void removeFromFileList(const QString &filePath);

    Q_INVOKABLE void resetImageColorSpaceOverride();

    Q_INVOKABLE bool isRecreatingViewerWindow() const;

    Q_INVOKABLE static bool hasThemeIcon(const QString &name);

    Q_INVOKABLE static QString getImageSourceTriggerSeq(bool sourceTrigger, bool animation);

    Q_INVOKABLE static bool verifyDropUrls(const QList<QUrl> &urls);

    Q_INVOKABLE static QString urlToLocalFile(const QUrl &url);
    Q_INVOKABLE static QString getFileName(const QString &filePath);
    Q_INVOKABLE static QUrl getDirectoryPathUrl(const QString &filePath);

private:
    void requestViewerWindowColorSpace(const QColorSpace &cs);

    bool setupViewerWindowColorSpace(const QColorSpace &cs);

#ifdef USE_COLORD
    bool applyColordScreenColorSpace(QScreen *screen);
#endif

    void processImageMetadata(
        const QString &fileName,
        const QSize &size,
        const QColorSpace &colorSpace,
        QImage::Format format,
        const shared_ptr<Exiv2::Image> &exiv2,
        bool imageTransformed
    );

Q_SIGNALS:
    void loadFile(const QString &filePath);
    void reload();

    void metadataChanged(bool hflip, bool vflip, double rotation, const QString &text);
    void animationFrameDelayChanged(int nextAnimationFrameDelay);

    void useGammaForSRgbUsableChanged();
    void hasImageColorSpaceOverrideChanged();

    void directoryPositionOrCountChanged();

private:
    const bool m_isWayland;
    const bool m_isX11;

    QUrl m_configPath;

    ImageProvider *m_imageProvider {};
    QQmlApplicationEngine m_engine;

    QPointer<QWindow> m_mainWin;
    QPointer<QWindow> m_viewerWin;
    QList<QColorSpace> m_brokenColorSpaces;

    bool m_displayColorManagementEnabled {true}; // Default value must be in-sync with QML's default value

    bool m_displayColorManagementSupported {true};
    bool m_useGammaForSRgbUsable {true};
    bool m_useGammaForSRgb {};
    bool m_hasImageColorSpaceOverride {};

#ifdef USE_COLORD
    QColorSpace m_colordColorSpace;
    bool m_colordCanReload {};
#endif

    bool m_recreatingViewerWindow {};

    DirectoryNavigation m_dirNavigation;
    qsizetype m_directoryPosition {};
    qsizetype m_directoryCount {};
};
