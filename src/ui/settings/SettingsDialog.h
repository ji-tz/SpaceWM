#pragma once

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;
class QKeyEvent;

// Settings: Hotkeys tab + General tab. Opened from the tray.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void reload();

signals:
    // Emitted on Apply/OK after persisting — re-register hotkeys, etc.
    void settingsApplied();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void loadHotkeyRows();
    void applyAndSave(bool close);
    void onPresetChanged(int index);
    void startCapture(int action, QPushButton *button);
    void refreshCaptureLabel();

    struct HotkeyRow {
        int action = 0;
        QLabel *name = nullptr;
        QPushButton *button = nullptr;
        QString sequence;
    };
    QVector<HotkeyRow> m_rows;
    QComboBox *m_preset = nullptr;
    QLabel *m_captureHint = nullptr;
    int m_captureAction = -1;
    QPushButton *m_captureButton = nullptr;

    QCheckBox *m_autoStart = nullptr;
};
