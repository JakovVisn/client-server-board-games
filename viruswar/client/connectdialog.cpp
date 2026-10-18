#include "connectdialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
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

    widthSpin_  = new QSpinBox;
    heightSpin_ = new QSpinBox;
    for (QSpinBox* spin : {widthSpin_, heightSpin_}) {
        spin->setRange(5, 20);
        spin->setValue(10);
    }
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(widthSpin_);
    sizeRow->addWidget(new QLabel("×"));
    sizeRow->addWidget(heightSpin_);
    sizeRow->addStretch();
    const QString sizeTip = "You are matched with players who chose the same size. "
                            "The random bot plays any size; the AZ bot only 10×10.";
    widthSpin_->setToolTip(sizeTip);
    heightSpin_->setToolTip(sizeTip);

    // Hidden unless asked for: without it the server default applies and anyone can be matched.
    // Protocol time controls: see docs/PROTOCOL.md#time-control
    customTimeCheck_ = new QCheckBox("Custom time control");
    customTimeCheck_->setToolTip("Unchecked: the server's default time limit (60 s per turn "
                                 "unless the server is configured otherwise), any opponent.");
    timeCombo_ = new QComboBox;
    timeCombo_->addItem("30 s per turn",          QStringLiteral("turn30"));
    timeCombo_->addItem("60 s per turn",          QStringLiteral("turn60"));
    timeCombo_->addItem("3 min per game",         QStringLiteral("game180"));
    timeCombo_->addItem("5 min + 5 s per turn",   QStringLiteral("game300+5"));
    timeCombo_->addItem("10 min + 10 s per turn", QStringLiteral("game600+10"));
    timeCombo_->addItem("No limit",               QStringLiteral("none"));
    timeCombo_->setCurrentIndex(timeCombo_->findData(QStringLiteral("turn60")));
    timeCombo_->setToolTip("You are only matched with players who chose the same time control "
                           "or none. Running out of time loses the game.");

    form_ = new QFormLayout;
    form_->addRow("Host:",     hostEdit_);
    form_->addRow("Port:",     portEdit_);
    form_->addRow("Login:",    loginEdit_);
    form_->addRow("Password:", passwordEdit_);
    form_->addRow("Opponent:", prefEdit_);
    form_->addRow("Board:",    sizeRow);
    form_->addRow(customTimeCheck_);
    form_->addRow("Time:",     timeCombo_);
    form_->setRowVisible(timeCombo_, false);
    connect(customTimeCheck_, &QCheckBox::toggled, this, [this](bool on) {
        form_->setRowVisible(timeCombo_, on);
        adjustSize();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Connect");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form_);
    layout->addWidget(buttons);
}

QString ConnectDialog::host() const     { return hostEdit_->text().trimmed(); }
quint16 ConnectDialog::port() const     { return portEdit_->text().toUShort(); }
QString ConnectDialog::login() const    { return loginEdit_->text().trimmed(); }
QString ConnectDialog::password() const { return passwordEdit_->text(); }
QString ConnectDialog::preferredOpponent() const { return prefEdit_->text().trimmed(); }
int ConnectDialog::boardWidth() const   { return widthSpin_->value(); }
int ConnectDialog::boardHeight() const  { return heightSpin_->value(); }
QString ConnectDialog::timeControl() const
{
    return customTimeCheck_->isChecked() ? timeCombo_->currentData().toString() : QString();
}

void ConnectDialog::setHost(const QString& host)   { hostEdit_->setText(host); }
void ConnectDialog::setPort(quint16 port)          { portEdit_->setText(QString::number(port)); }
void ConnectDialog::setLogin(const QString& login) { loginEdit_->setText(login); }
void ConnectDialog::setPassword(const QString& pw) { passwordEdit_->setText(pw); }
void ConnectDialog::setPreferredOpponent(const QString& pref) { prefEdit_->setText(pref); }

void ConnectDialog::setBoardSize(int width, int height)
{
    widthSpin_->setValue(width);
    heightSpin_->setValue(height);
}

void ConnectDialog::setTimeControl(const QString& timeControl)
{
    const int index = timeControl.isEmpty() ? -1 : timeCombo_->findData(timeControl);
    if (index >= 0)
        timeCombo_->setCurrentIndex(index);
    customTimeCheck_->setChecked(index >= 0);
}
