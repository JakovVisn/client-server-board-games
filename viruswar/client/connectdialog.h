#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;
class QPushButton;
class QSpinBox;

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
    [[nodiscard]] int boardWidth() const;
    [[nodiscard]] int boardHeight() const;
    [[nodiscard]] QString timeControl() const;  // empty: server default, any opponent
    [[nodiscard]] bool rememberPassword() const;

    void setHost(const QString& host);
    void setPort(quint16 port);
    void setLogin(const QString& login);
    void setPassword(const QString& password);
    void setPreferredOpponent(const QString& pref);
    void setBoardSize(int width, int height);
    void setTimeControl(const QString& timeControl);
    void setRememberPassword(bool remember);

private:
    QLineEdit* hostEdit_ = nullptr;
    QLineEdit* portEdit_ = nullptr;
    QLineEdit* loginEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLineEdit* prefEdit_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;
    QSpinBox* heightSpin_ = nullptr;
    QCheckBox* customTimeCheck_ = nullptr;
    QComboBox* timeCombo_ = nullptr;
    QFormLayout* form_ = nullptr;
    QCheckBox* rememberCheck_ = nullptr;
    QPushButton* connectButton_ = nullptr;

    void updateConnectButton();
};
