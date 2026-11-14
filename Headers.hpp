// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#ifndef Q_MOC_RUN

# include <condition_variable>
# include <memory>
# include <iosfwd>
# include <mutex>

# include <exiv2/image.hpp>

# include <QtConcurrent/QtConcurrentRun>
# include <QCommandLineParser>
# include <QRegularExpression>
# include <QFileSystemWatcher>
# include <QStandardPaths>
# include <QMimeDatabase>
# include <QCollator>
# include <QFileInfo>
# include <QSettings>
# include <QPromise>
# include <QFuture>
# include <QThread>
# include <QTimer>
# include <QDir>

# include <QGuiApplication>
# include <QSurfaceFormat>
# include <QImageReader>
# include <QColorSpace>
# include <QSGTexture>

# ifdef USE_WIDGETS
#  include <QApplication>
# endif

# include <private/qrhi_p.h>

# include <QQmlApplicationEngine>
# include <QQuickImageProvider>
# include <QQuickWindow>
# include <QQuickStyle>

# ifdef USE_COLORD
#  include <QDBusPendingReply>
#  include <QDBusConnection>
#  include <QDBusInterface>
#  include <QDBusArgument>
# endif

using namespace Qt::StringLiterals;
using namespace std;

#else

# include <QtQml/qqmlregistration.h>

#endif
