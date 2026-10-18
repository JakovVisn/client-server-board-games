#pragma once

#include <QString>

class AuthManager
{
public:
    enum class Result {
        Authorized,
        WrongPassword,
        NotFound,
        Error,
    };

    explicit AuthManager(QString authFilePath, QString statsFilePath);

    // A plain-text password in auth.txt is replaced by its hash on the first successful login
    [[nodiscard]] Result verify(const QString& login, const QString& password) const;
    [[nodiscard]] Result registerUser(const QString& login, const QString& password) const;

    void recordWin(const QString& login) const;
    void recordLoss(const QString& login) const;

    [[nodiscard]] bool isBot(const QString& login) const;
    [[nodiscard]] int autoConnectionLimit(const QString& login) const;

private:
    QString filePath_;
    QString statsFilePath_;

    void updateStats(const QString& login, int winDelta, int lossDelta) const;
    void storeHash(const QString& login, const QString& password) const;
};
