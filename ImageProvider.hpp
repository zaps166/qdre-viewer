// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "Headers.hpp"

class ImageProvider : public QQuickImageProvider
{
    Q_OBJECT

public:
    enum class ImageSourceTriggerSeq
    {
        Still0 = 0,
        Still1 = 1,
        Ani0 = 2,
        Ani1 = 3,
    };
    Q_ENUM(ImageSourceTriggerSeq)

public:
    ImageProvider(bool software);
    ~ImageProvider();

    void requestWindowColorSpaceDone(const QColorSpace &cs, bool deepColorNotAvailable, bool useGammaForSRgb);
    void requestWindowColorSpaceAborted();

    void setImageColorSpaceOverride(const QColorSpace &cs = {});

protected:
    QImage requestImage(const QString &id, QSize *size, const QSize& requestedSize) override;
    QQuickTextureFactory *requestTexture(const QString &id, QSize *size, const QSize &requestedSize) override;

Q_SIGNALS:
    void requestWindowColorSpace(const QColorSpace &cs);

    void imageMetadataChanged(
        const QString &fileName = {},
        const QSize &size = {},
        const QColorSpace &colorSpace = {},
        QImage::Format format = {},
        const shared_ptr<Exiv2::Image> &exiv2 = {},
        bool imageTransformed = {}
    );
    void animationFrameDelayChanged(int nextAnimationFrameDelay = {});

private:
    struct ImageHandler
    {
        QFile file;
        QImageReader reader;
        QByteArray fileFormat;
        int loops {};
    };

    atomic_bool m_requestingImageTexture {};

    bool m_requestWindowColorSpaceDone {};
    bool m_requestWindowColorSpaceAborted {};
    condition_variable m_waitCond;
    mutex m_waitCondMutex;

    shared_ptr<ImageHandler> m_animationHandler;

    QColorSpace m_displayColorSpace;
    QColorSpace m_imageColorSpaceOverride;
    mutex m_imageColorSpaceOverrideMutex;

    bool m_deepColorNotAvailable {};
    bool m_useGammaForSRgb {};
};
