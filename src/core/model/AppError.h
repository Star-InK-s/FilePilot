#pragma once

#include <QString>

namespace FilePilot {

enum class ErrorCode {
    None,
    InvalidPath,
    NotFound,
    AccessDenied,
    FileInUse,
    Io,
    Database,
    Configuration,
    Cancelled,
    Unknown
};

QString errorCodeName(ErrorCode code);

class AppError
{
public:
    AppError() = default;
    AppError(ErrorCode code, QString message, QString context = QString());

    bool isValid() const;
    ErrorCode code() const;
    const QString &message() const;
    const QString &context() const;

private:
    ErrorCode code_ = ErrorCode::None;
    QString message_;
    QString context_;
};

} // namespace FilePilot
