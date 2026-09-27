#include "core/filesystem/FileHasher.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace FilePilot {

bool FileHasher::hashFile(const QString &path,
                          QByteArray &sha256,
                          AppError &error) const
{
    sha256.clear();
    error = AppError{};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = AppError{
            QFileInfo::exists(path) ? ErrorCode::Io : ErrorCode::NotFound,
            QStringLiteral("无法读取文件内容生成 SHA-256"),
            path,
        };
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr int bufferSize = 1024 * 1024;
    QByteArray buffer(bufferSize, Qt::Uninitialized);

    while (!file.atEnd()) {
        const qint64 bytesRead = file.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            error = AppError{
                ErrorCode::Io,
                QStringLiteral("读取文件内容生成 SHA-256 失败"),
                path,
            };
            return false;
        }
        if (bytesRead == 0) {
            break;
        }
        hash.addData(QByteArrayView(buffer.constData(), bytesRead));
    }

    if (file.error() != QFileDevice::NoError) {
        error = AppError{
            ErrorCode::Io,
            QStringLiteral("读取文件内容生成 SHA-256 失败"),
            path,
        };
        return false;
    }

    sha256 = hash.result();
    return true;
}

} // namespace FilePilot
