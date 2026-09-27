#pragma once

#include "core/model/AppError.h"

#include <QByteArray>
#include <QString>

namespace FilePilot {

class FileHasher
{
public:
    bool hashFile(const QString &path,
                  QByteArray &sha256,
                  AppError &error) const;
};

} // namespace FilePilot
