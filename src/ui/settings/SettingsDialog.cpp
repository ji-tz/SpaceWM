#include "SettingsDialog.h"

#include "core/settings/AppSettings.h"
#include "hotkeys/HotkeyManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
struct HotkeyRowDef {
    int action;
    const char *name;
};
const HotkeyRowDef kRows[] = {
    {HotkeyManager::SwitchPrevSpace, "Previous space"},
    {HotkeyManager::SwitchNextSpace, "Next space"},
    {HotkeyManager::ToggleOverview, "Overview"},
    {HotkeyManager::JumpSpace1, "Jump to space 1"},
    {HotkeyManager::JumpSpace2, "Jump to space 2"},
    {HotkeyManager::JumpSpace3, "Jump to space 3"},
    {HotkeyManager::JumpSpace4, "Jump to space 4"},
};
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("SpaceWM Settings"));
    setModal(true);
    resize(560, 480);

    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    // --- Hotkeys ---
    auto *hkPage = new QWidget;
    auto *hkLayout = new QVBoxLayout(hkPage);

    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(tr("Preset:")));
    m_preset = new QComboBox(hkPage);
    m_preset->addItem(tr("Default (Ctrl+Alt + …)"), QStringLiteral("default"));
    m_preset->addItem(tr("System shortcuts (Win+Tab, Ctrl+Win+←/→)"), QStringLiteral("system"));
    m_preset->addItem(tr("Custom"), QStringLiteral("custom"));
    presetRow->addWidget(m_preset, 1);
    hkLayout->addLayout(presetRow);

    m_captureHint = new QLabel(hkPage);
    m_captureHint->setStyleSheet(QStringLiteral("color: #7aa2ff;"));
    m_captureHint->setVisible(false);
    hkLayout->addWidget(m_captureHint);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    for (const auto &def : kRows) {
        HotkeyRow row;
        row.action = def.action;
        row.name = new QLabel(tr(def.name), hkPage);
        row.button = new QPushButton(hkPage);
        row.button->setMinimumWidth(160);
        form->addRow(row.name, row.button);
        m_rows.push_back(row);
    }
    hkLayout->addLayout(form);
    hkLayout->addStretch(1);
    tabs->addTab(hkPage, tr("Hotkeys"));

    for (int i = 0; i < m_rows.size(); ++i) {
        auto *btn = m_rows[i].button;
        const int action = m_rows[i].action;
        connect(btn, &QPushButton::clicked, this, [this, action, btn]() {
            startCapture(action, btn);
        });
    }
    connect(m_preset, &QComboBox::currentIndexChanged, this,
            &SettingsDialog::onPresetChanged);

    // --- General ---
    auto *genPage = new QWidget;
    auto *genLayout = new QVBoxLayout(genPage);
    m_autoStart = new QCheckBox(tr("Start when Windows starts"), genPage);
    genLayout->addWidget(m_autoStart);
    m_exclusiveSpaces = new QCheckBox(tr("Exclusive spaces (one window per space)"), genPage);
    genLayout->addWidget(m_exclusiveSpaces);
    genLayout->addStretch(1);
    tabs->addTab(genPage, tr("General"));

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        applyAndSave(true);
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this]() { applyAndSave(false); });

    reload();
}

void SettingsDialog::reload()
{
    m_captureAction = -1;
    m_captureButton = nullptr;
    refreshCaptureLabel();

    AppSettings s;
    const QString preset = s.hotkeyPreset();
    int idx = m_preset->findData(preset);
    m_preset->setCurrentIndex(idx >= 0 ? idx : 0);
    loadHotkeyRows();
    m_autoStart->setChecked(s.autoStart());
    m_exclusiveSpaces->setChecked(s.exclusiveSpaces());
}

void SettingsDialog::loadHotkeyRows()
{
    AppSettings s;
    for (auto &row : m_rows) {
        row.sequence = s.hotkeyOrDefault(row.action);
        if (row.button)
            row.button->setText(row.sequence.isEmpty() ? tr("(none)") : row.sequence);
    }
}

void SettingsDialog::onPresetChanged(int index)
{
    const QString preset = m_preset->itemData(index).toString();
    if (preset == QStringLiteral("default") || preset == QStringLiteral("system")) {
        for (auto &row : m_rows) {
            row.sequence = (preset == QStringLiteral("system"))
                               ? HotkeyManager::systemSequence(row.action)
                               : HotkeyManager::defaultSequence(row.action);
            if (row.button)
                row.button->setText(row.sequence);
        }
    }
}

void SettingsDialog::startCapture(int action, QPushButton *button)
{
    m_captureAction = action;
    m_captureButton = button;
    if (button)
        button->setText(tr("Press keys…"));
    refreshCaptureLabel();
    if (button)
        button->setFocus(Qt::OtherFocusReason);
}

void SettingsDialog::refreshCaptureLabel()
{
    if (!m_captureHint)
        return;
    if (m_captureAction >= 0) {
        m_captureHint->setText(
            tr("Press a shortcut (Esc to cancel). Win-combos occupy system shortcuts."));
        m_captureHint->setVisible(true);
    } else {
        m_captureHint->clear();
        m_captureHint->setVisible(false);
    }
}

void SettingsDialog::keyPressEvent(QKeyEvent *event)
{
    if (m_captureAction < 0) {
        QDialog::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        m_captureAction = -1;
        m_captureButton = nullptr;
        refreshCaptureLabel();
        loadHotkeyRows();
        event->accept();
        return;
    }
    // Ignore pure modifier presses.
    const int key = event->key();
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
        || key == Qt::Key_Meta || key == Qt::Key_unknown) {
        event->accept();
        return;
    }
    const QKeySequence seq(event->modifiers() | key);
    const QString text = seq.toString(QKeySequence::PortableText);
    if (text.isEmpty()) {
        event->accept();
        return;
    }
    for (auto &row : m_rows) {
        if (row.action == m_captureAction) {
            row.sequence = text;
            if (row.button)
                row.button->setText(text);
            break;
        }
    }
    // Manual edit → custom preset.
    const int customIdx = m_preset->findData(QStringLiteral("custom"));
    if (customIdx >= 0 && m_preset->currentIndex() != customIdx)
        m_preset->setCurrentIndex(customIdx);

    m_captureAction = -1;
    m_captureButton = nullptr;
    refreshCaptureLabel();
    event->accept();
}

void SettingsDialog::applyAndSave(bool close)
{
    Q_UNUSED(close)
    AppSettings s;

    // Persist current preset + all row sequences.
    s.setHotkeyPreset(m_preset->currentData().toString());
    QVector<QString> pairs;
    for (const auto &row : m_rows) {
        s.setHotkey(row.action, row.sequence);
        pairs << QString::number(row.action) << row.sequence;
    }
    // setHotkeyPreset("default"/"system") rewrites sequences — re-apply rows after
    // so a custom row edit under a non-custom preset still wins when preset is custom.
    if (m_preset->currentData().toString() == QStringLiteral("custom")) {
        for (const auto &row : m_rows)
            s.setHotkey(row.action, row.sequence);
    } else {
        // Align UI with what the preset wrote.
        loadHotkeyRows();
    }

    s.setAutoStart(m_autoStart->isChecked());
    s.setExclusiveSpaces(m_exclusiveSpaces->isChecked());

    emit settingsApplied();
}
