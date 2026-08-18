#include "password_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

PasswordEnableDialog::PasswordEnableDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Enable password protection"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Choose a master password for Agent Redactor. It protects your stored "
           "API keys on this machine and is unrelated to your login password."), this));
    auto* form = new QFormLayout;
    pw1_ = new QLineEdit(this);
    pw1_->setEchoMode(QLineEdit::Password);
    pw2_ = new QLineEdit(this);
    pw2_->setEchoMode(QLineEdit::Password);
    form->addRow(tr("New password:"), pw1_);
    form->addRow(tr("Confirm password:"), pw2_);
    layout->addLayout(form);

    error_ = new QLabel(this);
    error_->setStyleSheet(QStringLiteral("color: red"));
    error_->setVisible(false);
    layout->addWidget(error_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PasswordEnableDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString PasswordEnableDialog::password() const { return pw1_->text(); }

void PasswordEnableDialog::onAccept() {
    if (pw1_->text().isEmpty()) {
        error_->setText(tr("Password must not be empty."));
        error_->setVisible(true);
        return;
    }
    if (pw1_->text() != pw2_->text()) {
        error_->setText(tr("Passwords do not match."));
        error_->setVisible(true);
        return;
    }
    accept();
}

PasswordUnlockDialog::PasswordUnlockDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Unlock Agent Redactor"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Enter your master password to unlock."), this));
    pw_ = new QLineEdit(this);
    pw_->setEchoMode(QLineEdit::Password);
    layout->addWidget(pw_);

    error_ = new QLabel(this);
    error_->setStyleSheet(QStringLiteral("color: red"));
    error_->setVisible(false);
    layout->addWidget(error_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString PasswordUnlockDialog::password() const { return pw_->text(); }

void PasswordUnlockDialog::setError(const QString& message) {
    error_->setText(message);
    error_->setVisible(true);
}
