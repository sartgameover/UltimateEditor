#include "Export.h"
#include "PlayerView.h"
#include "AudioEngine.h"
#include "Water.h"
#include <QCheckBox>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>
#include "Tools.h"
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QDir>
#include <QTemporaryDir>
#include <QApplication>
#include <QFile>

ExportDialog::ExportDialog(Project* p, PlayerView* pl, QWidget* parent) : QDialog(parent), project(p), player(pl) {
    setWindowTitle("Экспорт"); resize(560, 560);
    QSettings s("UltimateEditor", "UltimateEditor");
    QFormLayout* f = new QFormLayout(this);
    // папка: запоминаем последние места экспорта и сразу предлагаем прошлую
    dir = new QComboBox; dir->setEditable(true);
    QStringList dirs = s.value("export_dirs").toStringList();
    if (dirs.isEmpty()) dirs << QDir::homePath() + "/Videos";
    dir->addItems(dirs);
    dir->setCurrentIndex(0);
    QPushButton* br = new QPushButton("Выбрать папку…");
    connect(br, &QPushButton::clicked, this, [this]() {
        QString d = QFileDialog::getExistingDirectory(this, "Куда сохранить", dir->currentText());
        if (!d.isEmpty()) { dir->insertItem(0, d); dir->setCurrentIndex(0); }
    });
    QHBoxLayout* row = new QHBoxLayout; row->addWidget(dir, 1); row->addWidget(br);
    f->addRow("Папка", row);
    name = new QLineEdit(s.value("export_name", "export").toString()); f->addRow("Имя файла", name);
    kind = new QComboBox; kind->addItems({"Видео", "Только аудио"}); f->addRow("Тип", kind);
    cont = new QComboBox; codec = new QComboBox; enc = new QComboBox; fps = new QComboBox;
    codec->addItems({"H.264", "H.265 (HEVC)", "AV1", "ProRes"});
    enc->addItems({"CPU", "NVENC (NVIDIA)", "VAAPI (AMD/Intel)"});
    fps->addItems({"24", "30", "60"});
    f->addRow("Формат", cont); f->addRow("Кодек", codec); f->addRow("Кодировщик", enc); f->addRow("FPS", fps);
    connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExportDialog::kindChanged);
    kindChanged();
    // вспоминаем прошлые настройки
    kind->setCurrentIndex(s.value("export_kind", 0).toInt());
    kindChanged();
    cont->setCurrentText(s.value("export_container", cont->currentText()).toString());
    codec->setCurrentText(s.value("export_codec", "H.264").toString());
    enc->setCurrentText(s.value("export_encoder", "CPU").toString());
    fps->setCurrentText(s.value("export_fps", QString::number(project->fps)).toString());
    openAfter = new QCheckBox("Открыть папку после экспорта");
    openAfter->setChecked(s.value("export_open", true).toBool());
    f->addRow(openAfter);
    water = new WaterProgress; water->setFixedSize(190, 190);
    QHBoxLayout* wr = new QHBoxLayout; wr->addStretch(1); wr->addWidget(water); wr->addStretch(1);
    f->addRow(wr);
    status = new QLabel("Готов к рендеру"); status->setAlignment(Qt::AlignCenter);
    f->addRow(status);
    go = new QPushButton("Рендер"); f->addRow(go);
    connect(go, &QPushButton::clicked, this, &ExportDialog::start);
}

void ExportDialog::kindChanged() {
    bool audio = kind->currentIndex() == 1;
    cont->clear();
    cont->addItems(audio ? QStringList{"mp3", "wav", "flac", "ogg"} : QStringList{"mp4", "mov", "mkv", "avi", "webm"});
    codec->setEnabled(!audio); enc->setEnabled(!audio); fps->setEnabled(!audio);
}

void ExportDialog::reject() {
    if (running) { cancel = true; return; }
    QDialog::reject();
}

void ExportDialog::start() {
    if (running) return;
    const QString outDir = dir->currentText();
    QDir().mkpath(outDir);
    {   // запоминаем место экспорта (первым в списке) и настройки
        QSettings s("UltimateEditor", "UltimateEditor");
        QStringList dirs = s.value("export_dirs").toStringList();
        dirs.removeAll(outDir); dirs.prepend(outDir);
        while (dirs.size() > 8) dirs.removeLast();
        s.setValue("export_dirs", dirs);
        s.setValue("export_name", name->text()); s.setValue("export_kind", kind->currentIndex());
        s.setValue("export_container", cont->currentText()); s.setValue("export_codec", codec->currentText());
        s.setValue("export_encoder", enc->currentText()); s.setValue("export_fps", fps->currentText());
        s.setValue("export_open", openAfter->isChecked());
    }
    bool audioOnly = kind->currentIndex() == 1;
    QString container = cont->currentText();
    QString out = outDir + "/" + name->text() + "." + container;
    QTemporaryDir tmp;
    QString wav = tmp.path() + "/mix.wav";
    status->setText("Микширую звук…"); qApp->processEvents();
    bool hasAudio = renderMixToWav(*project, wav);                       // тот же микшер и эффекты, что в плеере
    running = true; cancel = false; go->setEnabled(false);

    if (audioOnly) {
        if (!hasAudio) { QMessageBox::warning(this, "Экспорт", "Нет аудиоклипов"); running = false; go->setEnabled(true); return; }
        QStringList a = {"-y", "-v", "error", "-i", wav};
        if (container == "mp3") a << "-c:a" << "libmp3lame" << "-q:a" << "2";
        else if (container == "wav") a << "-c:a" << "pcm_s16le";
        else if (container == "flac") a << "-c:a" << "flac";
        else a << "-c:a" << "libvorbis" << "-q:a" << "6";
        a << out;
        QProcess p; p.start(ffmpegPath(), a); p.waitForFinished(-1);
        running = false; go->setEnabled(true); water->setValue(100);
        QMessageBox::information(this, "Готово", out);
        if (openAfter->isChecked()) QDesktopServices::openUrl(QUrl::fromLocalFile(outDir));
        accept(); return;
    }

    // --- видеокодек ---
    QString c = codec->currentText(), e = enc->currentText();
    QStringList pre, vargs; QString vf = "vflip", pix = "yuv420p";           // vflip: GL отдаёт строки снизу вверх
    if (container == "webm" && c != "AV1") vargs << "-c:v" << "libvpx-vp9" << "-crf" << "30" << "-b:v" << "0";
    else if (c == "ProRes") { vargs << "-c:v" << "prores_ks" << "-profile:v" << "3"; pix = "yuv422p10le"; }
    else {
        QString key = c == "H.264" ? "h264" : c.startsWith("H.265") ? "hevc" : "av1";
        if (e.startsWith("NVENC")) vargs << "-c:v" << key + "_nvenc" << "-cq" << "20" << "-preset" << "p5";
        else if (e.startsWith("VAAPI")) { pre << "-vaapi_device" << "/dev/dri/renderD128"; vf += ",format=nv12,hwupload"; vargs << "-c:v" << key + "_vaapi" << "-qp" << "21"; pix.clear(); }
        else if (key == "h264") vargs << "-c:v" << "libx264" << "-crf" << "18" << "-preset" << "medium";
        else if (key == "hevc") vargs << "-c:v" << "libx265" << "-crf" << "22" << "-preset" << "medium";
        else vargs << "-c:v" << "libsvtav1" << "-crf" << "30" << "-preset" << "8";
    }
    int W = project->W, H = project->H, fp = fps->currentText().toInt();
    QStringList a = {"-y", "-v", "error"};
    a << pre << "-f" << "rawvideo" << "-pix_fmt" << "rgba" << "-s" << QString("%1x%2").arg(W).arg(H) << "-r" << QString::number(fp) << "-i" << "-";
    if (hasAudio) a << "-i" << wav;
    a << "-vf" << vf << vargs;
    if (!pix.isEmpty()) a << "-pix_fmt" << pix;
    if (hasAudio) {
        if (container == "webm") a << "-c:a" << "libopus" << "-b:a" << "160k";
        else if (container == "avi") a << "-c:a" << "libmp3lame" << "-b:a" << "256k";
        else if (container == "mov" && c == "ProRes") a << "-c:a" << "pcm_s16le";
        else a << "-c:a" << "aac" << "-b:a" << "256k";
        a << "-shortest";
    }
    a << out;
    QProcess ff; ff.setProcessChannelMode(QProcess::ForwardedChannels);
    ff.start(ffmpegPath(), a);
    if (!ff.waitForStarted()) { QMessageBox::critical(this, "Экспорт", "Не удалось запустить ffmpeg"); running = false; go->setEnabled(true); return; }

    double oldT = player->time();
    player->pause(); player->setUpdatesPaused(true);
    player->makeCurrent();
    Compositor& comp = player->compositor();
    comp.defaultFbo = player->defaultFramebufferObject();
    int total = qMax((int)(project->duration() * fp), 1);
    std::vector<uint8_t> buf;
    for (int i = 0; i < total && !cancel; ++i) {
        comp.render(i / (double)fp, QString(), false);           // без прокси — полное качество
        comp.readPixels(buf);
        ff.write(reinterpret_cast<const char*>(buf.data()), (qint64)buf.size());
        ff.waitForBytesWritten(-1);
        if (i % 2 == 0) { water->setValue(i * 100 / total); status->setText(QString("Рендер: кадр %1 из %2").arg(i).arg(total)); qApp->processEvents(); }
    }
    player->doneCurrent();
    ff.closeWriteChannel();
    ff.waitForFinished(-1);
    player->setUpdatesPaused(false);
    player->seek(oldT);
    running = false; go->setEnabled(true);
    if (cancel) { QFile::remove(out); QMessageBox::information(this, "Экспорт", "Отменено"); return; }
    water->setValue(100); status->setText("Готово!");
    if (ff.exitCode() != 0) QMessageBox::critical(this, "Экспорт", "ffmpeg завершился с ошибкой (см. терминал)");
    else {
        QMessageBox::information(this, "Готово", out);
        if (openAfter->isChecked()) QDesktopServices::openUrl(QUrl::fromLocalFile(outDir));
        accept();
    }
}
