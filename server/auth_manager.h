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

    explicit AuthManager(QString filePath);

    [[nodiscard]] Result verify(const QString& login, const QString& password);
    [[nodiscard]] Result registerUser(const QString& login, const QString& password) const;

private:
    QString filePath_;
};
