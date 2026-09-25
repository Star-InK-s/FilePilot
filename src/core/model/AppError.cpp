#include "core/model/AppError.h"

#include <utility>

namespace FilePilot {

QString errorCodeName(const ErrorCode code)
{
    switch (code) {
    case ErrorCode::None:
        return QStringLiteral("None");
    case ErrorCode::InvalidPath:
        return QStringLiteral("Invalid path");
    case ErrorCode::NotFound:
        return QStringLiteral("Not found");
    case ErrorCode::AccessDenied:
        return QStringLiteral("Access denied");
    case ErrorCode::FileInUse:
        return QStringLiteral("File in use");
    case ErrorCode::Io:
        return QStringLiteral("I/O error");
    case ErrorCode::Database:
        return QStringLiteral("Database error");
    case ErrorCode::Configuration:
        return QStringLiteral("Configuration error");
    case ErrorCode::Cancelled:
        return QStringLiteral("Cancelled");
    case ErrorCode::Unknown:
        return QStringLiteral("Unknown error");
    }

    return QStringLiteral("Unknown error");
}

AppError::AppError(const ErrorCode code, QString message, QString context)
    : code_(code)
    , message_(std::move(message))
    , context_(std::move(context))
{
}

bool AppError::isValid() const
{
    return code_ != ErrorCode::None;
}

ErrorCode AppError::code() const
{
    return code_;
}

const QString &AppError::message() const
{
    return message_;
}

const QString &AppError::context() const
{
    return context_;
}

} // namespace FilePilot

