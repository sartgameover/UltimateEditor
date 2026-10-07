#include "ScreenRecorder.h"
#include <QAudioInput>
#include <QMediaFormat>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

ScreenRecorder::ScreenRecorder(QObject* parent) : QObject(parent) {
    connect(&recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState st) {
        if (st == QMediaRecorder::StoppedState && stopping) {
            stopping = false; rec = false;
            emit finished(recorder.actualLocation().toLocalFile());
        }
    });
    connect(&recorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString& msg) {
        if (rec || stopping) { rec = false; stopping = false; capture.stop(); emit failed(msg); }
    });
}

bool ScreenRecorder::start(const QString& path, bool withMic, const QAudioDevice& mic) {
    if (rec) return false;
    session.setScreenCapture(&capture);
    capture.setScreen(QGuiApplication::primaryScreen());
    if (withMic && !mic.isNull()) {
        if (!audioIn) audioIn = new QAudioInput(mic, this); else audioIn->setDevice(mic);
        session.setAudioInput(audioIn);
    } else {
        session.setAudioInput(nullptr);
    }
    session.setRecorder(&recorder);
    QMediaFormat f;
    f.setFileFormat(QMediaFormat::MPEG4);
    f.setVideoCodec(QMediaFormat::VideoCodec::H264);
    f.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    recorder.setMediaFormat(f);
    recorder.setQuality(QMediaRecorder::HighQuality);
    recorder.setVideoFrameRate(30);
    recorder.setOutputLocation(QUrl::fromLocalFile(path));
    capture.start();
    recorder.record();
    rec = true;
    return true;
}

void ScreenRecorder::stop() {
    if (!rec) return;
    stopping = true;
    recorder.stop();
    capture.stop();
}

RecordHud::RecordHud() : QWidget(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint) {
    setStyleSheet("QWidget{background:#202124;color:white;border:2px solid #e5484d;border-radius:10px;}"
                  "QPushButton{background:#e5484d;border:none;padding:6px 14px;border-radius:6px;font-weight:bold;}");
    QHBoxLayout* l = new QHBoxLayout(this);
    label = new QLabel("● REC 00:00");
    QPushButton* b = new QPushButton("■ Стоп");
    l->addWidget(label); l->addWidget(b);
    connect(b, &QPushButton::clicked, this, &RecordHud::stopClicked);
    connect(&timer, &QTimer::timeout, this, [this]() {
        int s = (int)(clock.elapsed() / 1000);
        label->setText(QString("● REC %1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0')));
    });
}

void RecordHud::begin() {
    clock.start(); timer.start(500);
    adjustSize();
    QScreen* sc = QGuiApplication::primaryScreen();
    if (sc) move(sc->geometry().center().x() - width() / 2, sc->geometry().top() + 20);
    show();
}
