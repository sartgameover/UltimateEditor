#include "SettingsDialog.h"
#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMediaDevices>
#include <QSettings>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Настройки");
    QSettings s("UltimateEditor", "UltimateEditor");
    QFormLayout* f = new QFormLayout(this);
    mics = new QComboBox;
    QByteArray cur = s.value("mic_id").toByteArray();
    const QList<QAudioDevice> list = QMediaDevices::audioInputs();
    for (const QAudioDevice& d : list) mics->addItem(d.description(), d.id());
    if (list.isEmpty()) mics->addItem("(микрофоны не найдены)");
    int idx = mics->findData(cur);
    if (idx >= 0) mics->setCurrentIndex(idx);
    else {
        int def = mics->findData(QMediaDevices::defaultAudioInput().id());
        if (def >= 0) mics->setCurrentIndex(def);
    }
    f->addRow("Микрофон для записи голоса", mics);
    autosave = new QSpinBox; autosave->setRange(10, 3600); autosave->setSuffix(" с");
    autosave->setValue(s.value("autosave_sec", 60).toInt());
    f->addRow("Автосохранение каждые", autosave);
    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, [this]() { save(); accept(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    f->addRow(bb);
}

void SettingsDialog::save() {
    QSettings s("UltimateEditor", "UltimateEditor");
    s.setValue("mic_id", mics->currentData().toByteArray());
    s.setValue("autosave_sec", autosave->value());
}

QAudioDevice SettingsDialog::selectedMic() {
    QSettings s("UltimateEditor", "UltimateEditor");
    QByteArray id = s.value("mic_id").toByteArray();
    const QList<QAudioDevice> list = QMediaDevices::audioInputs();
    for (const QAudioDevice& d : list) if (d.id() == id) return d;
    return QMediaDevices::defaultAudioInput();
}

int SettingsDialog::autosaveSeconds() {
    return QSettings("UltimateEditor", "UltimateEditor").value("autosave_sec", 60).toInt();
}
