#include "Installer.h"
#include "Branding.h"
#include "Water.h"
#include <QStackedWidget>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QProcess>
#include <QTextStream>
#include <QMessageBox>

static QString dataDir() { return QDir::homePath() + "/.local/share"; }
static QString installDir() { return QDir::homePath() + "/.local/opt/UltimateEditor"; }
static QString markerFile() { return dataDir() + "/UltimateEditor/installed"; }

bool InstallerDialog::shouldRun(const QStringList& args) {
    if (args.contains("--no-installer")) return false;
    if (args.contains("--installer")) return true;
    return qEnvironmentVariableIsSet("APPIMAGE") && !QFile::exists(markerFile());
}

bool InstallerDialog::uninstall() {
    QFile::remove(dataDir() + "/applications/ultimate-video-editor.desktop");
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/ultimate-video-editor.desktop");
    QFile::remove(dataDir() + "/icons/hicolor/256x256/apps/ultimate-video-editor.png");
    QFile::remove(dataDir() + "/mime/packages/application-x-uvmvideos.xml");
    QDir(installDir()).removeRecursively();
    QFile::remove(markerFile());
    QProcess::execute("update-mime-database", QStringList() << dataDir() + "/mime");
    QProcess::execute("update-desktop-database", QStringList() << dataDir() + "/applications");
    return true;
}

static QPushButton* bigButton(const QString& text, bool primary) {
    QPushButton* b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(primary
        ? "QPushButton{background:#e5484d;color:white;border:none;border-radius:10px;padding:12px 26px;font-size:15px;font-weight:bold;}QPushButton:hover{background:#ff6369;}"
        : "QPushButton{background:rgba(255,255,255,45);color:white;border:none;border-radius:10px;padding:12px 26px;font-size:15px;}QPushButton:hover{background:rgba(255,255,255,90);}");
    return b;
}

InstallerDialog::InstallerDialog() : QDialog(nullptr, Qt::FramelessWindowHint) {
    setFixedSize(720, 460);
    banner = brandBanner();
    icon = brandIcon(120);
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    pages = new QStackedWidget;
    pages->setStyleSheet("QLabel,QCheckBox{color:white;font-size:14px;}");
    root->addWidget(pages);

    // --- страница 1: приветствие ---
    QWidget* p1 = new QWidget; QVBoxLayout* l1 = new QVBoxLayout(p1);
    l1->setContentsMargins(40, 30, 40, 30);
    QLabel* ic = new QLabel; ic->setPixmap(icon); ic->setAlignment(Qt::AlignCenter);
    QLabel* h = new QLabel("<span style='font-size:30px;font-weight:bold;'>Ultimate Video Editor</span>"); h->setAlignment(Qt::AlignCenter);
    QLabel* t = new QLabel("Видеоредактор для Linux: быстрый GPU-монтаж, шейдерные эффекты, звуковая студия.<br>Установи его в меню приложений или просто запусти."); t->setAlignment(Qt::AlignCenter); t->setWordWrap(true);
    QPushButton* inst = bigButton("Установить", true), *run = bigButton("Запустить без установки", false);
    QHBoxLayout* hb = new QHBoxLayout; hb->addStretch(1); hb->addWidget(inst); hb->addWidget(run); hb->addStretch(1);
    l1->addStretch(1); l1->addWidget(ic); l1->addWidget(h); l1->addWidget(t); l1->addSpacing(18); l1->addLayout(hb); l1->addStretch(1);
    pages->addWidget(p1);

    // --- страница 2: параметры ---
    QWidget* p2 = new QWidget; QVBoxLayout* l2 = new QVBoxLayout(p2);
    l2->setContentsMargins(60, 40, 60, 40);
    QLabel* h2 = new QLabel("<span style='font-size:22px;font-weight:bold;'>Что установить</span>");
    menuIcon = new QCheckBox("Ярлык в меню приложений"); menuIcon->setChecked(true);
    desktopIcon = new QCheckBox("Ярлык на рабочем столе"); desktopIcon->setChecked(true);
    assoc = new QCheckBox("Открывать проекты .uvmvideos двойным кликом"); assoc->setChecked(true);
    QPushButton* go = bigButton("Установить", true);
    QPushButton* back = bigButton("Назад", false);
    QHBoxLayout* hb2 = new QHBoxLayout; hb2->addWidget(back); hb2->addStretch(1); hb2->addWidget(go);
    l2->addWidget(h2); l2->addSpacing(10); l2->addWidget(menuIcon); l2->addWidget(desktopIcon); l2->addWidget(assoc); l2->addStretch(1); l2->addLayout(hb2);
    pages->addWidget(p2);

    // --- страница 3: прогресс (та же «вода», что и при экспорте) ---
    QWidget* p3 = new QWidget; QVBoxLayout* l3 = new QVBoxLayout(p3);
    water = new WaterProgress; water->setFixedSize(210, 210);
    stepLabel = new QLabel("Подготовка…"); stepLabel->setAlignment(Qt::AlignCenter);
    l3->addStretch(1); l3->addWidget(water, 0, Qt::AlignCenter); l3->addWidget(stepLabel); l3->addStretch(1);
    pages->addWidget(p3);

    // --- страница 4: готово ---
    QWidget* p4 = new QWidget; QVBoxLayout* l4 = new QVBoxLayout(p4);
    QLabel* done = new QLabel("<span style='font-size:28px;font-weight:bold;'>Готово! ✔</span><br><br>Программа установлена. Найдёшь её в меню приложений."); done->setAlignment(Qt::AlignCenter);
    launch = bigButton("Запустить редактор", true);
    l4->addStretch(1); l4->addWidget(done); l4->addSpacing(14); l4->addWidget(launch, 0, Qt::AlignCenter); l4->addStretch(1);
    pages->addWidget(p4);

    connect(inst, &QPushButton::clicked, this, [this]() { pages->setCurrentIndex(1); });
    connect(back, &QPushButton::clicked, this, [this]() { pages->setCurrentIndex(0); });
    connect(go, &QPushButton::clicked, this, [this]() { startInstall(); });
    connect(run, &QPushButton::clicked, this, [this]() { accept(); });           // без установки
    connect(launch, &QPushButton::clicked, this, [this]() { accept(); });
    connect(&timer, &QTimer::timeout, this, [this]() { step(); });
    QPushButton* x = new QPushButton("✕", this);
    x->setGeometry(width() - 40, 8, 30, 30);
    x->setStyleSheet("QPushButton{background:rgba(0,0,0,90);color:white;border:none;border-radius:15px;}QPushButton:hover{background:#e5484d;}");
    connect(x, &QPushButton::clicked, this, &QDialog::reject);
}

void InstallerDialog::startInstall() {
    pages->setCurrentIndex(2);
    stepNo = 0;
    timer.start(650);
}

void InstallerDialog::step() {
    static const char* names[] = {"Копирую программу…", "Устанавливаю значок…", "Создаю ярлыки…", "Регистрирую формат .uvmvideos…", "Заканчиваю…"};
    const int total = 5;
    if (stepNo < total) stepLabel->setText(names[stepNo]);
    switch (stepNo) {
    case 0: {
        QDir().mkpath(installDir());
        QString src = qEnvironmentVariable("APPIMAGE");
        if (src.isEmpty()) src = QCoreApplication::applicationFilePath();
        installedExe = installDir() + "/UltimateEditor.AppImage";
        QFile::remove(installedExe);
        QFile::copy(src, installedExe);
        QFile(installedExe).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                           QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
        break; }
    case 1: {
        QDir().mkpath(dataDir() + "/icons/hicolor/256x256/apps");
        brandIcon(256).save(dataDir() + "/icons/hicolor/256x256/apps/ultimate-video-editor.png", "PNG");
        break; }
    case 2: {
        QString desktop =
            "[Desktop Entry]\nType=Application\nName=Ultimate Video Editor\nComment=Видеоредактор\n"
            "Exec=\"" + installedExe + "\" --no-installer %F\nIcon=ultimate-video-editor\nTerminal=false\n"
            "Categories=AudioVideo;Video;AudioVideoEditing;\nMimeType=application/x-uvmvideos;\n";
        if (menuIcon->isChecked()) {
            QDir().mkpath(dataDir() + "/applications");
            QFile f(dataDir() + "/applications/ultimate-video-editor.desktop");
            if (f.open(QIODevice::WriteOnly)) { f.write(desktop.toUtf8()); f.close(); }
        }
        if (desktopIcon->isChecked()) {
            QString dst = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/ultimate-video-editor.desktop";
            QFile f(dst);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(desktop.toUtf8()); f.close();
                f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
            }
        }
        break; }
    case 3: {
        if (assoc->isChecked()) {
            QDir().mkpath(dataDir() + "/mime/packages");
            QFile f(dataDir() + "/mime/packages/application-x-uvmvideos.xml");
            if (f.open(QIODevice::WriteOnly)) {
                f.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                        " <mime-type type=\"application/x-uvmvideos\"><comment>Ultimate Video Editor project</comment><glob pattern=\"*.uvmvideos\"/></mime-type>\n</mime-info>\n");
                f.close();
            }
            QProcess::execute("update-mime-database", QStringList() << dataDir() + "/mime");
        }
        QProcess::execute("update-desktop-database", QStringList() << dataDir() + "/applications");
        break; }
    case 4: {
        QDir().mkpath(dataDir() + "/UltimateEditor");
        QFile m(markerFile());
        if (m.open(QIODevice::WriteOnly)) { m.write("1"); m.close(); }
        didInstall = true;
        break; }
    default:
        timer.stop();
        water->setValue(100);
        pages->setCurrentIndex(3);
        return;
    }
    ++stepNo;
    water->setValue(stepNo * 100 / total);
}

void InstallerDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPixmap s = banner.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    p.drawPixmap((width() - s.width()) / 2, (height() - s.height()) / 2, s);
    p.fillRect(rect(), QColor(0, 0, 0, 120));
}
