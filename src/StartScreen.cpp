#include "StartScreen.h"
#include "Branding.h"
#include <QPainter>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QMouseEvent>
#include <QThreadPool>
#include <QRunnable>
#include <QPointer>
#include <QElapsedTimer>
#include <QMetaObject>

// фоновый поиск *.uvmvideos по домашней папке (ограничены глубина и время)
class ScanJob : public QRunnable {
public:
    explicit ScanJob(StartScreen* s) : target(s) { setAutoDelete(true); }
    void run() override {
        QElapsedTimer t; t.start();
        scan(QDir::homePath(), 0, t);
        if (target) QMetaObject::invokeMethod(target.data(), "scanFinished", Qt::QueuedConnection);
    }
private:
    void scan(const QString& dir, int depth, QElapsedTimer& t) {
        if (depth > 6 || t.elapsed() > 6000 || !target) return;
        QDir d(dir);
        for (const QFileInfo& fi : d.entryInfoList(QStringList() << "*.uvmvideos", QDir::Files))
            if (target) QMetaObject::invokeMethod(target.data(), "addFound", Qt::QueuedConnection, Q_ARG(QString, fi.absoluteFilePath()));
        for (const QFileInfo& sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString n = sub.fileName();
            if (n.startsWith('.') || n == "node_modules" || n == "proc" || n == "snap") continue;
            scan(sub.absoluteFilePath(), depth + 1, t);
        }
    }
    QPointer<StartScreen> target;
};

static QString listStyle() {
    return "QListWidget{background:rgba(10,12,20,170);border:1px solid rgba(255,255,255,40);border-radius:10px;color:white;padding:4px;font-size:13px;}"
           "QListWidget::item{padding:8px;border-radius:6px;}"
           "QListWidget::item:hover{background:rgba(77,171,247,90);}"
           "QListWidget::item:selected{background:rgba(77,171,247,150);}";
}

StartScreen::StartScreen() : QDialog(nullptr, Qt::FramelessWindowHint) {
    setFixedSize(980, 560);
    banner = brandBanner();
    icon = brandIcon(150);
    QHBoxLayout* root = new QHBoxLayout(this);
    root->setContentsMargins(28, 28, 28, 28);
    root->setSpacing(20);
    // левая часть: значок и кнопки
    QVBoxLayout* left = new QVBoxLayout;
    QLabel* ic = new QLabel; ic->setPixmap(icon); ic->setAlignment(Qt::AlignCenter);
    QLabel* title = new QLabel("<span style='font-size:24px;font-weight:bold;color:white;'>Ultimate<br>Video Editor</span>");
    title->setAlignment(Qt::AlignCenter);
    QString bs = "QPushButton{background:rgba(229,72,77,230);color:white;border:none;border-radius:8px;padding:10px;font-size:14px;font-weight:bold;}"
                 "QPushButton:hover{background:#ff6369;}";
    QPushButton* nw = new QPushButton("＋ Новый проект"); nw->setStyleSheet(bs);
    QPushButton* op = new QPushButton("📂 Открыть файл…");
    op->setStyleSheet("QPushButton{background:rgba(255,255,255,40);color:white;border:none;border-radius:8px;padding:10px;font-size:14px;}QPushButton:hover{background:rgba(255,255,255,80);}");
    left->addStretch(1); left->addWidget(ic); left->addWidget(title); left->addSpacing(14);
    left->addWidget(nw); left->addWidget(op); left->addStretch(2);
    QWidget* lw = new QWidget; lw->setLayout(left); lw->setFixedWidth(230);
    root->addWidget(lw);
    root->addStretch(1);
    // правые столбцы
    auto column = [&](const QString& head, QListWidget*& lst) {
        QVBoxLayout* c = new QVBoxLayout;
        QLabel* h = new QLabel("<span style='color:white;font-size:15px;font-weight:bold;'>" + head + "</span>");
        lst = new QListWidget; lst->setStyleSheet(listStyle());
        c->addWidget(h); c->addWidget(lst, 1);
        QWidget* w = new QWidget; w->setLayout(c); w->setFixedWidth(240);
        root->addWidget(w);
    };
    column("Недавние", recent);
    column("Найдено на компьютере", found);
    for (const QString& p : recentProjects()) {
        QListWidgetItem* it = new QListWidgetItem(QFileInfo(p).completeBaseName() + "\n" + QFileInfo(p).lastModified().toString("dd.MM.yyyy HH:mm"));
        it->setData(Qt::UserRole, p); it->setToolTip(p);
        recent->addItem(it);
    }
    if (recent->count() == 0) { QListWidgetItem* it = new QListWidgetItem("Пока пусто"); it->setFlags(Qt::NoItemFlags); recent->addItem(it); }
    QListWidgetItem* scanning = new QListWidgetItem("Ищу проекты…"); scanning->setFlags(Qt::NoItemFlags); found->addItem(scanning);

    connect(recent, &QListWidget::itemClicked, this, &StartScreen::pick);
    connect(found, &QListWidget::itemClicked, this, &StartScreen::pick);
    connect(nw, &QPushButton::clicked, this, [this]() { chosen.clear(); accept(); });
    connect(op, &QPushButton::clicked, this, [this]() {
        QString f = QFileDialog::getOpenFileName(this, "Открыть проект", QDir::homePath(), "Проекты (*.uvmvideos)");
        if (!f.isEmpty()) { chosen = f; accept(); }
    });
    QPushButton* closeBtn = new QPushButton("✕", this);
    closeBtn->setGeometry(width() - 40, 8, 30, 30);
    closeBtn->setStyleSheet("QPushButton{background:rgba(0,0,0,90);color:white;border:none;border-radius:15px;}QPushButton:hover{background:#e5484d;}");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    QThreadPool::globalInstance()->start(new ScanJob(this));
}

void StartScreen::pick(QListWidgetItem* it) {
    QString p = it->data(Qt::UserRole).toString();
    if (p.isEmpty()) return;
    chosen = p;                                             // и «недавнее», и «найденное» открывают студию с этим проектом
    accept();
}

void StartScreen::addFound(const QString& path) {
    if (found->count() == 1 && found->item(0)->flags() == Qt::NoItemFlags) delete found->takeItem(0);
    for (int i = 0; i < found->count(); ++i) if (found->item(i)->data(Qt::UserRole).toString() == path) return;
    QListWidgetItem* it = new QListWidgetItem(QFileInfo(path).completeBaseName() + "\n" + QFileInfo(path).absolutePath());
    it->setData(Qt::UserRole, path); it->setToolTip(path);
    found->addItem(it);
}

void StartScreen::scanFinished() {
    if (found->count() == 1 && found->item(0)->flags() == Qt::NoItemFlags) found->item(0)->setText("Проектов не найдено");
}

void StartScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPixmap scaled = banner.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);   // Baner на заднем фоне
    QLinearGradient g(0, 0, width(), 0);                    // затемнение, чтобы текст читался
    g.setColorAt(0, QColor(0, 0, 0, 140)); g.setColorAt(1, QColor(0, 0, 0, 60));
    p.fillRect(rect(), g);
    p.setPen(QPen(QColor(255, 255, 255, 60), 2));
    p.drawRect(rect().adjusted(1, 1, -1, -1));
}

void StartScreen::mousePressEvent(QMouseEvent* e) { dragOff = e->globalPosition().toPoint() - frameGeometry().topLeft(); }
void StartScreen::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) move(e->globalPosition().toPoint() - dragOff);
}
