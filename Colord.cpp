// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "Colord.hpp"

namespace Colord {

QColorSpace getScreenColorSpace(const QString &name)
{
    QColorSpace cs;

    const auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
    {
        return cs;
    }

    const auto orgFreedesktopColorManager = u"org.freedesktop.ColorManager"_s;

    QDBusInterface colordIface(orgFreedesktopColorManager, u"/org/freedesktop/ColorManager"_s, orgFreedesktopColorManager, bus);
    const QDBusPendingReply<QList<QDBusObjectPath>> getDevPathsReply = colordIface.call(u"GetDevices"_s);
    if (getDevPathsReply.isError())
    {
        return cs;
    }

    const auto devicePaths = getDevPathsReply.value();
    for (auto &&devPath : devicePaths)
    {
        auto getDevPropsCall = QDBusMessage::createMethodCall(
            orgFreedesktopColorManager,
            devPath.path(),
            u"org.freedesktop.DBus.Properties"_s,
            u"GetAll"_s
        );
        getDevPropsCall.setArguments({u"org.freedesktop.ColorManager.Device"_s});
        const QDBusPendingReply<QVariantMap> devPropsReply = bus.call(getDevPropsCall);
        const auto devProps = devPropsReply.value();
        if (devProps[u"Kind"_s].toString() != u"display"_s)
        {
            continue;
        }

        const auto xrandrName = [&devProps] {
            QMap<QString, QString> metadata;
            devProps[u"Metadata"_s].value<QDBusArgument>()>> metadata;
            return metadata;
        }()[u"XRANDR_name"_s];
        if (xrandrName != name)
        {
            continue;
        }

        const auto profilePath = [&devProps] {
            QList<QDBusObjectPath> profilePaths;
            devProps[u"Profiles"_s].value<QDBusArgument>()>> profilePaths;
            return profilePaths;
        }().value(0).path();
        if (profilePath.isEmpty())
        {
            break;
        }

        auto getFileNameCall = QDBusMessage::createMethodCall(
            orgFreedesktopColorManager,
            profilePath,
            u"org.freedesktop.DBus.Properties"_s,
            u"Get"_s
        );
        getFileNameCall.setArguments({u"org.freedesktop.ColorManager.Profile"_s, u"Filename"_s});
        const QDBusPendingReply<QVariant> fileNameReply = bus.call(getFileNameCall);
        if (fileNameReply.isError())
        {
            break;
        }

        const auto fileName = fileNameReply.value().toString();
        if (fileName.isEmpty())
        {
            break;
        }

        QFile csFile(fileName);
        if (csFile.size() > static_cast<qint64>(1 << 24) || !csFile.open(QFile::ReadOnly))
        {
            break;
        }

        cs = QColorSpace::fromIccProfile(csFile.readAll());
        if (cs == QColorSpace::SRgb)
        {
            cs = QColorSpace();
        }

        break;
    }

    return cs;
}

}
