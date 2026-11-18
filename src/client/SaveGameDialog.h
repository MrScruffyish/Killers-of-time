#pragma once

#include <QDialog>

class QLineEdit;

namespace kot::client {

// The name for a saved game: a roomy little dialog instead of the tiny stock
// QInputDialog, with the day as the suggested name.
class SaveGameDialog : public QDialog {
    Q_OBJECT
public:
    explicit SaveGameDialog(const QString& suggestedName, QWidget* parent = nullptr);
    QString name() const;

private:
    QLineEdit* m_name;
};

}  // namespace kot::client
