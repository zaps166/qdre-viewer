// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "ImageProvider.hpp"

class ImageTexture : public QSGTexture
{
public:
    ImageTexture(QImage &&img)
        : m_img(std::move(img))
        , m_size(m_img.size())
        , m_hasAlpha(m_img.hasAlphaChannel())
    {
    }
    ~ImageTexture()
    {
        delete m_tex;
    }

private:
    qint64 comparisonKey() const override
    {
        return m_tex ? reinterpret_cast<qintptr>(m_tex) : reinterpret_cast<qintptr>(this);
    }
    QRhiTexture *rhiTexture() const override
    {
        return m_tex;
    }
    QSize textureSize() const override
    {
        return m_size;
    }
    bool hasAlphaChannel() const override
    {
        return m_hasAlpha;
    }
    bool hasMipmaps() const override
    {
        return m_hasMipmaps;
    }

    void commitTextureOperations(QRhi *rhi, QRhiResourceUpdateBatch *resourceUpdates) override
    {
        if (!rhi || !resourceUpdates || m_tex || m_img.isNull())
        {
            return;
        }

        QRhiTexture::Format format {};
        switch (m_img.format())
        {
            case QImage::Format_RGB32:
            case QImage::Format_ARGB32_Premultiplied:
                format = QRhiTexture::BGRA8;
                break;
            case QImage::Format_BGR30:
            case QImage::Format_A2BGR30_Premultiplied:
                format = QRhiTexture::RGB10A2;
                break;
            case QImage::Format_RGBA16FPx4_Premultiplied:
                format = QRhiTexture::RGBA16F;
                break;
            default:
                qCritical() << "Unsupported image format:" << m_img.format();
                return;
        }

        m_hasMipmaps = (mipmapFiltering() != QSGTexture::None);

        const auto flags = m_hasMipmaps
            ? (QRhiTexture::Flag::UsedWithGenerateMips | QRhiTexture::Flag::MipMapped)
            : QRhiTexture::Flags()
        ;

        if (format != QRhiTexture::BGRA8 && !rhi->isTextureFormatSupported(format, flags))
        {
            qWarning() << "Texture format not supported:" << format;
            format = QRhiTexture::BGRA8;
            m_img.convertTo(m_img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
        }

        const int textureSizeLimit = rhi->resourceLimit(QRhi::ResourceLimit::TextureSizeMax);
        if (m_img.width() > textureSizeLimit || m_img.height() > textureSizeLimit)
        {
            qWarning() << "Texture limit exceeded, image will be downscaled to the longer edge:" << textureSizeLimit;
            m_img = m_img.scaled(textureSizeLimit, textureSizeLimit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        m_tex = rhi->newTexture(format, m_img.size(), 1, flags);
        if (!m_tex)
        {
            return;
        }
        if (!m_tex->create())
        {
            qCritical() << "Unable to create texture:" << format;
            delete m_tex;
            m_tex = nullptr;
            return;
        }

        resourceUpdates->uploadTexture(m_tex, m_img);
        m_img = QImage();
        if (m_hasMipmaps)
        {
            resourceUpdates->generateMips(m_tex);
        }
    }

private:
    QImage m_img;
    const QSize m_size;
    const bool m_hasAlpha;
    bool m_hasMipmaps {};
    QRhiTexture *m_tex {};
};

/**/

class ImageTextureFactory : public QQuickTextureFactory
{
public:
    ImageTextureFactory(QImage &&img)
        : m_img(std::move(img))
        , m_size(m_img.size())
    {
    }
    ~ImageTextureFactory()
    {
    }

private:
    QSGTexture *createTexture(QQuickWindow *window) const override
    {
        if (!window || m_img.isNull())
        {
            return nullptr;
        }
        return new ImageTexture(std::move(m_img));
    }

    int textureByteCount() const override
    {
        return 0;
    }

    QSize textureSize() const override
    {
        return m_size;
    }

private:
    mutable QImage m_img;
    const QSize m_size;
};

/**/

ImageProvider::ImageProvider(bool software)
    : QQuickImageProvider(software ? ImageType::Image : ImageType::Texture)
{
}
ImageProvider::~ImageProvider()
{
}

void ImageProvider::requestWindowColorSpaceDone(const QColorSpace &cs, bool deepColorNotAvailable, bool useGammaForSRgb)
{
    if (cs.isValid())
    {
        m_displayColorSpace = cs;
    }
    else
    {
        m_displayColorSpace = QColorSpace::NamedColorSpace::SRgb;
    }

    m_deepColorNotAvailable = (imageType() == ImageType::Image)
        ? true // QPainter can output 10-bit on X11, but QML's "ImageType::Image" converts image to 8-bit
        : deepColorNotAvailable
    ;

    m_useGammaForSRgb = useGammaForSRgb;

    {
        scoped_lock locker(m_waitCondMutex);
        m_requestWindowColorSpaceDone = true;
        m_waitCond.notify_one();
    }
}
void ImageProvider::requestWindowColorSpaceAborted()
{
    scoped_lock locker(m_waitCondMutex);
    m_requestWindowColorSpaceAborted = true;
    m_waitCond.notify_one();
}

void ImageProvider::setImageColorSpaceOverride(const QColorSpace &cs)
{
    m_imageColorSpaceOverride = cs;
}

QImage ImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    constexpr auto imagePrefix = u"image/"_sv;
    if (id.startsWith(imagePrefix))
    {
        if (m_requestingImageTexture)
        {
            qCritical() << "Image is already loading";
            return QImage();
        }
        m_requestingImageTexture = true;
        auto onExit = qScopeGuard([this] {
            m_requestingImageTexture = false;
        });

        auto onLoadFailed = qScopeGuard([this] {
            m_animationHandler.reset();
            emit imageMetadataChanged();
            emit animationFrameDelayChanged();
        });

        if (requestedSize.isValid())
        {
            qCritical() << "Requested size is not supported";
            return QImage();
        }

        const auto filePath = id.mid(imagePrefix.size() + 2);
        if (filePath.isEmpty())
        {
            return QImage();
        }

        const bool isAnimationSequence = [&] {
            bool idxObtained = false;
            const underlying_type_t<ImageSourceTriggerSeq> imageSourceTriggerSeq = QStringView(id).mid(imagePrefix.size(), 1).toInt(&idxObtained);
            if (Q_LIKELY(idxObtained)) switch (imageSourceTriggerSeq)
            {
                using enum ImageSourceTriggerSeq;
                case to_underlying(Ani0):
                case to_underlying(Ani1):
                    return true;
            }
            return false;
        }();

        shared_ptr<ImageHandler> imageHandler;
        if (isAnimationSequence && m_animationHandler)
        {
            imageHandler = m_animationHandler;
        }
        else
        {
            m_animationHandler.reset();

            imageHandler = make_shared<ImageHandler>();

            imageHandler->file.setFileName(filePath);
            if (!imageHandler->file.open(QFile::ReadOnly))
            {
                qCritical() << "Unable to open:" << filePath;
                return QImage();
            }

            imageHandler->reader.setDevice(&imageHandler->file);
            if (!imageHandler->reader.canRead())
            {
                qCritical() << "Unable to read:" << filePath;
                return QImage();
            }

            imageHandler->fileFormat = imageHandler->reader.format();

            if (imageHandler->reader.supportsAnimation() && imageHandler->reader.imageCount() > 1)
            {
                imageHandler->loops = imageHandler->reader.loopCount();
                m_animationHandler = imageHandler;
            }
        }

        auto img = imageHandler->reader.read();
        if (img.isNull())
        {
            qCritical() << "Unable to load:" << filePath;
            return QImage();
        }

        int animationFrameDelay = 0;
        if (m_animationHandler)
        {
            animationFrameDelay = imageHandler->reader.nextImageDelay();
        }

        QImage::Format realImageFormat {};
        Exiv2::Image::UniquePtr exiv2;

        if (!isAnimationSequence)
        {
            if (imageHandler->fileFormat == "gif")
            {
                realImageFormat = QImage::Format_Indexed8;
            }
            else
            {
                // Read the real image format before conversion
                realImageFormat = img.format();
            }

            try
            {
                exiv2 = Exiv2::ImageFactory::open(filePath.toStdString(), false);
                exiv2->readMetadata();
                if (exiv2->exifData().empty())
                {
                    exiv2.reset();
                }
            }
            catch (...)
            {
                exiv2.reset();
            }
        }

        if (m_imageColorSpaceOverride.isValid())
        {
            img.setColorSpace(m_imageColorSpaceOverride);
        }

        auto imgColorSpace = img.colorSpace();
        if (!imgColorSpace.isValid())
        {
            if (exiv2 && !m_animationHandler)
            {
                // Qt can't determine Adobe RGB color space from camera
                using Exiv2::ExifKey;
                const auto &tags = exiv2->exifData();
                const auto itCs = tags.findKey(ExifKey("Exif.Photo.ColorSpace"s));
                const auto itIop = tags.findKey(ExifKey("Exif.Iop.InteroperabilityIndex"s));
                if (itCs != tags.end() && itCs->count() > 0 && itIop != tags.end())
                {
                    if (itCs->value().toUint32() == 65535 && itIop->value().toString() == "R03"s)
                    {
                        imgColorSpace = QColorSpace::NamedColorSpace::AdobeRgb;
                    }
                }
            }
            if (!imgColorSpace.isValid())
            {
                imgColorSpace = QColorSpace::NamedColorSpace::SRgb;
            }
            img.setColorSpace(imgColorSpace);
        }

        if (!isAnimationSequence)
        {
            unique_lock<mutex> locker;
            if (QThread::currentThread() != QCoreApplication::instance()->thread())
            {
                locker = unique_lock(m_waitCondMutex);
            }
            m_requestWindowColorSpaceDone = false;
            emit requestWindowColorSpace(imgColorSpace);
            if (locker.owns_lock())
            {
                m_waitCond.wait(locker, [this] {
                    return (m_requestWindowColorSpaceDone || m_requestWindowColorSpaceAborted);
                });
            }
            if (!m_requestWindowColorSpaceDone)
            {
                return QImage();
            }
            if (locker.owns_lock())
            {
                locker.unlock();
            }
        }

        if (Q_UNLIKELY(!m_displayColorSpace.isValid()))
        {
            return QImage();
        }

        QImage::Format dstFmt {};
        if (const auto pixelFmt = img.pixelFormat(); m_deepColorNotAvailable || (
                    (m_displayColorSpace.primaries() == imgColorSpace.primaries() || imgColorSpace.colorModel() == QColorSpace::ColorModel::Gray)
                    && pixelFmt.redSize() <= 8 && pixelFmt.greenSize() <= 8 && pixelFmt.blueSize() <= 8))
        {
            if (img.hasAlphaChannel())
            {
                dstFmt = QImage::Format_ARGB32_Premultiplied;
            }
            else
            {
                dstFmt = QImage::Format_RGB32;
            }
        }
        else
        {
            bool hasRealAlpha = false;
            if (img.hasAlphaChannel())
            {
                const auto alpha = img.convertToFormat(QImage::Format_Alpha8);
                const int w = alpha.width();
                const int h = alpha.height();
                for (int y = 0; y < h; ++y)
                {
                    const auto lineData = alpha.constScanLine(y);
                    for (int x = 0; x < w; ++x)
                    {
                        if (lineData[x] != 255 && lineData[x] != 0)
                        {
                            hasRealAlpha = true;
                            break;
                        }
                    }
                    if (hasRealAlpha)
                    {
                        break;
                    }
                }
            }
            if (hasRealAlpha)
            {
                dstFmt = QImage::Format_RGBA16FPx4_Premultiplied;
            }
            else if (img.hasAlphaChannel())
            {
                dstFmt = QImage::Format_A2BGR30_Premultiplied;
            }
            else
            {
                dstFmt = QImage::Format_BGR30;
            }
        }

        auto dstColorSpace = m_displayColorSpace;
        if (dstColorSpace.transferFunction() == QColorSpace::TransferFunction::SRgb
                && (imgColorSpace.transferFunction() == QColorSpace::TransferFunction::Gamma
                    || (m_useGammaForSRgb && imgColorSpace.transferFunction() == QColorSpace::TransferFunction::SRgb)))
        {
            dstColorSpace.setTransferFunction(QColorSpace::TransferFunction::Gamma, 2.2f);
        }
        img.convertToColorSpace(dstColorSpace, dstFmt);

        if (size)
        {
            *size = img.size();
        }

        if (isAnimationSequence && m_animationHandler && imageHandler->reader.currentImageNumber() == imageHandler->reader.imageCount() -1)
        {
            auto resetAnimation = [&, this] {
                m_animationHandler.reset();
                animationFrameDelay = 0;
            };
            if (imageHandler->loops < 0 || exchange(imageHandler->loops, max(0, imageHandler->loops - 1)) > 0)
            {
                if (!imageHandler->reader.jumpToImage(0) && imageHandler->file.seek(0))
                {
                    imageHandler->reader.setDevice(&imageHandler->file);
                }
                if (!imageHandler->reader.canRead())
                {
                    qCritical() << "Unable to rewind the animation:" << filePath;
                    resetAnimation();
                }
            }
            else
            {
                resetAnimation();
            }
        }

        onLoadFailed.dismiss();

        if (!isAnimationSequence) // No need to refresh metadata info every frame
        {
            const bool imageTransformed = (imageHandler->fileFormat == "raw"); // Reader transforms RAW files automatically (3rdparty imageformats bug?)
            emit imageMetadataChanged(
                QFileInfo(filePath).fileName(),
                img.size(),
                imgColorSpace,
                realImageFormat,
                std::move(exiv2),
                imageTransformed
            );
        }
        emit animationFrameDelayChanged(animationFrameDelay);

        return img;
    }
    return QImage();
}
QQuickTextureFactory *ImageProvider::requestTexture(const QString &id, QSize *size, const QSize &requestedSize)
{
    if (auto img = requestImage(id, size, requestedSize); !img.isNull())
    {
        return new ImageTextureFactory(std::move(img));
    }
    return nullptr;
}
