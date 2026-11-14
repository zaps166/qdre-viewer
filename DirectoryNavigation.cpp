// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "DirectoryNavigation.hpp"

DirectoryNavigation::DirectoryNavigation()
    : m_supportedMimeTypes(QImageReader::supportedMimeTypes())
{
    m_dirRefreshTimer.setInterval(750ms);
    m_dirRefreshTimer.setSingleShot(true);
    m_dirRefreshTimer.callOnTimeout(this, &DirectoryNavigation::requestUpdateFileList);
}
DirectoryNavigation::~DirectoryNavigation()
{
}

void DirectoryNavigation::setDirAndCurrentFile(const QDir &dir, const QString &currentFile)
{
    bool dirPathChanged {};
    bool currentFileChanged {};

    if (m_dir != dir)
    {
        m_dir = dir;

        m_dirWatcher = make_unique<QFileSystemWatcher>(QStringList{m_dir->path()});
        connect(m_dirWatcher.get(), &QFileSystemWatcher::directoryChanged,
                this, [this](const QString &path) {
            Q_UNUSED(path)
            scheduleDirRefresh();
        });

        dirPathChanged = true;
    }

    if (m_currentFile != currentFile)
    {
        m_currentFile = currentFile;
        currentFileChanged = true;
    }

    if (dirPathChanged || m_dirRefreshTimer.isActive())
    {
        requestUpdateFileList();
    }
    else if (currentFileChanged)
    {
        updateCurrentFilePos(false);
    }
}

void DirectoryNavigation::removeFromFileList(const QString &filePath)
{
    if (filePath.isEmpty())
    {
        return;
    }

    const auto nRemoved = m_files.removeIf([filePath](const QFileInfo &fi) {
        return (fi.filePath() == filePath);
    });
    if (nRemoved > 0)
    {
        scheduleDirRefresh();
        updateCurrentFilePos(true);
    }
}

QString DirectoryNavigation::navigate(Navigate where)
{
    if (m_dirRefreshTimer.isActive())
    {
        requestUpdateFileList();
    }

    syncDirLister();

    if (m_currentFile.isEmpty() || m_files.empty() || m_currentFilePos >= m_files.size())
    {
        return QString();
    }

    switch (where)
    {
        using enum Navigate;
        case Begin:
        {
            if (m_currentFilePos == 0)
            {
                return QString();
            }
            return m_files.constFirst().filePath();
        }
        case End:
        {
            if (m_currentFilePos == m_files.size() - 1)
            {
                return QString();
            }
            return m_files.constLast().filePath();
        }
        case Prev:
        {
            if (m_currentFilePos <= 0)
            {
                return QString();
            }
            return m_files.at(m_currentFilePos - 1).filePath();
        }
        case Next:
        {
            if (m_currentFilePos < 0 || m_currentFilePos == m_files.size() - 1)
            {
                return QString();
            }
            return m_files.at(m_currentFilePos + 1).filePath();
        }
    }

    return QString();
}

pair<qsizetype, qsizetype> DirectoryNavigation::getInfo() const
{
    return {
        m_currentFilePos,
        m_numFiles,
    };
}

void DirectoryNavigation::scheduleDirRefresh()
{
    m_dirRefreshTimer.start();
    m_dirListerFuture.cancel();
}

void DirectoryNavigation::requestUpdateFileList()
{
    m_dirRefreshTimer.stop();
    m_dirListerFuture.cancel();

    if (!m_dir.has_value())
    {
        m_files.clear();
        updateInfoValues(-1);
        return;
    }

    auto getFiles = [dir = *m_dir, supportedMimeTypes = m_supportedMimeTypes](DirListerPromise &promise) {
        QFileInfoList files = dir.entryInfoList(QDir::Filter::Files);
        if (promise.isCanceled())
        {
            return;
        }

        // Filter unsupported files
        QMimeDatabase mimeDatabase;
        auto [filterItFirst, filterItLast] = ranges::remove_if(files, [&promise, &mimeDatabase, supportedMimeTypes](const QFileInfo &fi) {
            if (!promise.isCanceled() && fi.size() > 0 && fi.isReadable())
            {
                const auto mimeType = mimeDatabase.mimeTypeForFile(fi).name();
                return (mimeType == s_iccProfileMimeType || !supportedMimeTypes.contains(mimeType.toLatin1()));
            }
            return false;
        });
        if (promise.isCanceled())
        {
            return;
        }
        files.erase(filterItFirst, filterItLast);

        // Sort by name
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        ranges::sort(files, [&collator](const QFileInfo &a, const QFileInfo &b) {
            int cmp = collator.compare(a.baseName(), b.baseName());
            if (cmp == 0)
            {
                cmp = collator.compare(a.fileName(), b.fileName());
            }
            return (cmp < 0);
        });

        if (promise.isCanceled())
        {
            return;
        }
        promise.addResult(files);
    };

    m_dirListerFuture = QtConcurrent::run(getFiles);
    m_dirListerFuture.then(this, [this](const QFileInfoList &files) {
        Q_UNUSED(files)
        dirListerFinished(m_dirListerFuture.takeResult());
    });
}
void DirectoryNavigation::updateCurrentFilePos(bool ignoreRefreshTimer)
{
    if (!ignoreRefreshTimer && m_dirRefreshTimer.isActive())
    {
        requestUpdateFileList();
        return;
    }

    if (m_dirListerFuture.isRunning() && !m_dirListerFuture.isCanceled())
    {
        return;
    }

    if (m_currentFile.isEmpty())
    {
        updateInfoValues(-1);
        return;
    }

    auto it = ranges::find_if(as_const(m_files), [this](const QFileInfo &fi) {
        return (m_currentFile == fi.fileName());
    });

    qsizetype currentFilePos;
    if (it != m_files.cend())
    {
        currentFilePos = distance(m_files.cbegin(), it);
    }
    else
    {
        currentFilePos = -1;
    }
    updateInfoValues(currentFilePos);
}

void DirectoryNavigation::dirListerFinished(const QFileInfoList &files)
{
    Q_ASSERT(!m_dirListerFuture.isRunning());
    m_files = std::move(files);
    updateCurrentFilePos(false);
}

void DirectoryNavigation::syncDirLister()
{
    if (!m_dirListerFuture.isCanceled())
    {
        const bool overrideCursor = m_dirListerFuture.isRunning();
        if (overrideCursor)
        {
            QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        }
        m_dirListerFuture.waitForFinished();
        if (overrideCursor)
        {
            QGuiApplication::restoreOverrideCursor();
        }
        if (m_dirListerFuture.isValid())
        {
            dirListerFinished(m_dirListerFuture.takeResult());
        }
        m_dirListerFuture.cancel(); // Don't allow "then()" lambda to be called
    }
}

void DirectoryNavigation::updateInfoValues(qsizetype currentFilePos)
{
    if (m_currentFilePos != currentFilePos || m_numFiles != m_files.size())
    {
        m_currentFilePos = currentFilePos;
        m_numFiles = m_files.size();
        emit infoChanged();
    }
}
