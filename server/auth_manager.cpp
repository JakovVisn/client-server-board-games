#include <utility>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include "auth_manager.h"

AuthManager::AuthManager(QString filePath)
    : filePath_(std::move(filePath))
{
}

AuthManager::Result AuthManager::verify(const QString& login, const QString& password)
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (QFile::exists(filePath_)) {
            qWarning() << "Cannot open auth file:" << filePath_;
            return Result::Error;
        }
        return Result::NotFound;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QStringList parts = line.split(':');
        if (parts.size() < 2)
            continue;

        if (parts.at(0).trimmed() == login) {
            return (parts.at(1).trimmed() == password)
                ? Result::Authorized
                : Result::WrongPassword;
        }
    }

    return Result::NotFound;
}

AuthManager::Result AuthManager::registerUser(const QString& login, const QString& password) const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Cannot open auth file for writing:" << filePath_;
        return Result::Error;
    }

    QTextStream out(&file);
    out << login << ":" << password << "\n";
    return Result::Authorized;
}
