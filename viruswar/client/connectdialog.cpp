#include "connectdialog.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ConnectDialog::ConnectDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Connect to Server");
    setMinimumWidth(320);

    hostEdit_     = new QLineEdit("127.0.0.1");
    portEdit_     = new QLineEdit("1234");
    portEdit_->setValidator(new QIntValidator(1, 65535, this));
    loginEdit_    = new QLineEdit;
    passwordEdit_ = new QLineEdit;
    passwordEdit_->setEchoMode(QLineEdit::Password);
    prefEdit_     = new QLineEdit;
    prefEdit_->setPlaceholderText("optional");

    auto* form = new QFormLayout;
    form->addRow("Host:",     hostEdit_);
    form->addRow("Port:",     portEdit_);
    form->addRow("Login:",    loginEdit_);
    form->addRow("Password:", passwordEdit_);
    form->addRow("Opponent:", prefEdit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Connect");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString ConnectDialog::host() const     { return hostEdit_->text().trimmed(); }
quint16 ConnectDialog::port() const     { return portEdit_->text().toUShort(); }
QString ConnectDialog::login() const    { return loginEdit_->text().trimmed(); }
QString ConnectDialog::password() const { return passwordEdit_->text(); }
QString ConnectDialog::preferredOpponent() const { return prefEdit_->text().trimmed(); }

void ConnectDialog::setHost(const QString& host)   { hostEdit_->setText(host); }
void ConnectDialog::setPort(quint16 port)          { portEdit_->setText(QString::number(port)); }
void ConnectDialog::setLogin(const QString& login) { loginEdit_->setText(login); }
void ConnectDialog::setPassword(const QString& pw) { passwordEdit_->setText(pw); }
void ConnectDialog::setPreferredOpponent(const QString& pref) { prefEdit_->setText(pref); }

