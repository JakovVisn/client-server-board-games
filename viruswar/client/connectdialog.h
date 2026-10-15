#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;
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

    void setHost(const QString& host);
    void setPort(quint16 port);
    void setLogin(const QString& login);
    void setPassword(const QString& password);
    void setPreferredOpponent(const QString& pref);
    void setBoardSize(int width, int height);
    void setTimeControl(const QString& timeControl);

private:
    QLineEdit* hostEdit_;
    QLineEdit* portEdit_;
    QLineEdit* loginEdit_;
    QLineEdit* passwordEdit_;
    QLineEdit* prefEdit_;
    QSpinBox* widthSpin_;
    QSpinBox* heightSpin_;
    QCheckBox* customTimeCheck_;
    QComboBox* timeCombo_;
    QFormLayout* form_;
};
