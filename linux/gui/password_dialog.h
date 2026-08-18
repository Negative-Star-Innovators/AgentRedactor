#pragma once

// Typed-master-password dialogs for the Linux GUI (Linux has no Windows
// Hello; the master password is app-specific and unrelated to the OS login
// password).

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;

// Enable flow: new password + confirmation with mismatch/empty validation.
class PasswordEnableDialog : public QDialog {
    Q_OBJECT
public:
    explicit PasswordEnableDialog(QWidget* parent = nullptr);
    QString password() const;

private:
    void onAccept();

    QLineEdit* pw1_ = nullptr;
    QLineEdit* pw2_ = nullptr;
    QLabel* error_ = nullptr;
};

// Unlock flow: single password field; the caller verifies via POST /unlock
// and re-shows with an error on failure.
class PasswordUnlockDialog : public QDialog {
    Q_OBJECT
public:
    explicit PasswordUnlockDialog(QWidget* parent = nullptr);
    QString password() const;
    void setError(const QString& message);

private:
    QLineEdit* pw_ = nullptr;
    QLabel* error_ = nullptr;
};
