#include <utility>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include "auth_manager.h"

AuthManager::AuthManager(QString authFilePath, QString statsFilePath)
    : filePath_(std::move(authFilePath))
    , statsFilePath_(std::move(statsFilePath))
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

void AuthManager::recordWin(const QString& login) const
{
    updateStats(login, 1, 0);
}

void AuthManager::recordLoss(const QString& login) const
{
    updateStats(login, 0, 1);
}

bool AuthManager::isBot(const QString& login)
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QStringList parts = line.split(':');
        if (parts.size() >= 3 && parts.at(0).trimmed() == login)
            return parts.at(2).trimmed() == "bot";
    }
    return false;
}

int AuthManager::autoConnectionLimit(const QString& login)
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QStringList parts = line.split(':');
        if (parts.size() >= 4 && parts.at(0).trimmed() == login
                && parts.at(2).trimmed() == "bot") {
            bool ok = false;
            const int limit = parts.at(3).trimmed().toInt(&ok);
            return (ok && limit > 0) ? limit : 0;
        }
    }
    return 0;
}

void AuthManager::updateStats(const QString& login, int winDelta, int lossDelta) const
{
    QFile file(statsFilePath_);
    QStringList lines;
    bool found = false;

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QStringList parts = line.split(':');
            if (parts.size() >= 3 && parts.at(0) == login) {
                const int wins   = parts.at(1).toInt() + winDelta;
                const int losses = parts.at(2).toInt() + lossDelta;
                lines << QString("%1:%2:%3").arg(login).arg(wins).arg(losses);
                found = true;
            } else {
                lines << line;
            }
        }
        file.close();
    }

    if (!found)
        lines << QString("%1:%2:%3").arg(login).arg(winDelta).arg(lossDelta);

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&file);
        for (const QString& l : lines)
            out << l << "\n";
    } else {
        qWarning() << "Cannot write stats file:" << statsFilePath_;
    }
}
