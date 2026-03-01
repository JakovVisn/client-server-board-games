#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;

class ConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectDialog(QWidget* parent = nullptr);

    [[nodiscard]] QString host() const;
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] QString login() const;
    [[nodiscard]] QString password() const;
    [[nodiscard]] QString preferredOpponent() const;

    void setHost(const QString& host);
    void setPort(quint16 port);
    void setLogin(const QString& login);
    void setPassword(const QString& password);
    void setPreferredOpponent(const QString& pref);

private:
    QLineEdit* hostEdit_;
    QLineEdit* portEdit_;
    QLineEdit* loginEdit_;
    QLineEdit* passwordEdit_;
    QLineEdit* prefEdit_;
};
