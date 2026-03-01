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

    [[nodiscard]] Result verify(const QString& login, const QString& password);
    [[nodiscard]] Result registerUser(const QString& login, const QString& password) const;

    void recordWin(const QString& login) const;
    void recordLoss(const QString& login) const;

private:
    QString filePath_;
    QString statsFilePath_;

    void updateStats(const QString& login, int winDelta, int lossDelta) const;
};
