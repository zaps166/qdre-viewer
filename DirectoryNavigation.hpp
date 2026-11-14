// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "Headers.hpp"

class DirectoryNavigation : public QObject
{
    Q_OBJECT

    using DirListerFuture = QFuture<QFileInfoList>;
    using DirListerPromise = QPromise<QFileInfoList>;

public:
    enum class Navigate
    {
        Begin,
        End,
        Prev,
        Next,
    };

    static constexpr auto s_iccProfileMimeType = u"application/vnd.iccprofile"_sv;

public:
    DirectoryNavigation();
    ~DirectoryNavigation();

    void setDirAndCurrentFile(const QDir &dir, const QString &currentFile);

    void removeFromFileList(const QString &filePath);

    QString navigate(Navigate where);

    pair<qsizetype, qsizetype> getInfo() const;

private:
    void scheduleDirRefresh();

    void requestUpdateFileList();
    void updateCurrentFilePos(bool ignoreRefreshTimer);

    void dirListerFinished(const QFileInfoList &files);

    void syncDirLister();

    void updateInfoValues(qsizetype currentFilePos);

Q_SIGNALS:
    void infoChanged();

private:
    const QList<QByteArray> m_supportedMimeTypes;

    optional<QDir> m_dir;
    QFileInfoList m_files;

    QString m_currentFile;

    qsizetype m_currentFilePos {-1};
    qsizetype m_numFiles {};

    unique_ptr<QFileSystemWatcher> m_dirWatcher;
    QTimer m_dirRefreshTimer;

    DirListerFuture m_dirListerFuture;
};
