#include <utility>
#include <QCryptographicHash>
#include <QFile>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>
#include "auth_manager.h"
#include "logging.h"

namespace {

// Stored as pbkdf2_sha256$ITERATIONS$SALT$KEY (hex). The server is single-threaded, so
// every login blocks it for one derivation: keep the iteration count modest
constexpr auto kScheme = "pbkdf2_sha256";
constexpr int kIterations = 10000;
constexpr int kSaltBytes = 16;
constexpr int kKeyBytes = 32;

QByteArray derive(const QString& password, const QByteArray& salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(),
                                              salt, iterations, kKeyBytes);
}

QString hashPassword(const QString& password)
{
    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(salt.data()), kSaltBytes / 4);
    return QStringList{QString::fromLatin1(kScheme), QString::number(kIterations),
                       QString::fromLatin1(salt.toHex()),
                       QString::fromLatin1(derive(password, salt, kIterations).toHex())}.join('$');
}

bool isHash(const QString& stored)
{
    return stored.startsWith(QString::fromLatin1(kScheme) + '$');
}

// Constant time, so a wrong guess doesn't reveal how many bytes matched
bool sameBytes(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

bool matches(const QString& stored, const QString& password)
{
    if (!isHash(stored))
        return stored == password;  // a plain-text line, written by hand or by an older server
    const QStringList f = stored.split('$');
    bool ok = false;
    const int iterations = f.size() == 4 ? f[1].toInt(&ok) : 0;
    if (!ok || iterations <= 0) return false;
    return sameBytes(derive(password, QByteArray::fromHex(f[2].toLatin1()), iterations),
                     QByteArray::fromHex(f[3].toLatin1()));
}

}  // namespace

AuthManager::AuthManager(QString authFilePath, QString statsFilePath)
    : filePath_(std::move(authFilePath))
    , statsFilePath_(std::move(statsFilePath))
{
}

AuthManager::Result AuthManager::verify(const QString& login, const QString& password) const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (QFile::exists(filePath_)) {
            Log::warn(QString("auth_file_error op=read path=%1").arg(Log::quoted(filePath_)));
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
            const QString stored = parts.at(1).trimmed();
            if (!matches(stored, password))
                return Result::WrongPassword;
            if (!isHash(stored)) {
                file.close();
                storeHash(login, password);
            }
            return Result::Authorized;
        }
    }

    return Result::NotFound;
}

void AuthManager::storeHash(const QString& login, const QString& password) const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList parts = line.split(':');
        if (parts.size() >= 2 && parts.at(0).trimmed() == login) {
            parts[1] = hashPassword(password);
            line = parts.join(':');
        }
        lines << line;
    }
    file.close();

    QSaveFile out(filePath_);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&out);
        for (const QString& l : lines)
            stream << l << "\n";
        stream.flush();
        if (out.commit()) {
            Log::info(QString("password_hashed login=%1").arg(Log::quoted(login)));
            return;
        }
    }
    Log::warn(QString("auth_file_error op=write path=%1").arg(Log::quoted(filePath_)));
}

AuthManager::Result AuthManager::registerUser(const QString& login, const QString& password) const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        Log::warn(QString("auth_file_error op=append path=%1").arg(Log::quoted(filePath_)));
        return Result::Error;
    }

    QTextStream out(&file);
    out << login << ":" << hashPassword(password) << "\n";
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

bool AuthManager::isBot(const QString& login) const
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

int AuthManager::autoConnectionLimit(const QString& login) const
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

    // Written to a temporary file and renamed: a crash mid-write keeps the old stats
    QSaveFile out(statsFilePath_);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&out);
        for (const QString& l : lines)
            stream << l << "\n";
        stream.flush();
        if (out.commit())
            return;
    }
    Log::warn(QString("stats_file_error op=write path=%1").arg(Log::quoted(statsFilePath_)));
}
