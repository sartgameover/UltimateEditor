#pragma once
#include "Project.h"
#include <QWidget>
#include <QImage>
#include <QList>

class QListWidget; class QTreeWidget; class QSpinBox; class QComboBox; class QLabel; class QPlainTextEdit;
class QTableWidget; class QPushButton; class QAction; class FontPicker; class QLineEdit;

// Дополнительные окна (меню «Вид»): маркеры, свойства проекта, структура, заметки, пресеты, запись, файлы, таймкод,
// горячие клавиши, статистика, журнал, scopes, шрифты.

class MarkersPanel : public QWidget {
    Q_OBJECT
public:
    explicit MarkersPanel(Project* p, QWidget* parent = nullptr);
    void setProject(Project* p) { project = p; sig.clear(); refresh(); }
    void refresh();
signals:
    void seekRequested(double t);
    void addRequested();
    void changed();
private:
    Project* project; QListWidget* list; QString sig;
};

class ProjectPropsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPropsPanel(Project* p, QWidget* parent = nullptr);
    void setProject(Project* p) { project = p; reload(); }
    void reload();
signals:
    void changed();
private:
    Project* project; QSpinBox* w; QSpinBox* h; QComboBox* fps; QComboBox* preset; QPushButton* bg; QString bgCol;
};

class StructurePanel : public QWidget {
    Q_OBJECT
public:
    explicit StructurePanel(Project* p, QWidget* parent = nullptr);
    void setProject(Project* p) { project = p; sig.clear(); refresh(); }
    void refresh();
signals:
    void clipActivated(int clipId);
private:
    Project* project; QTreeWidget* tree; QString sig;
};

class NotesPanel : public QWidget {
    Q_OBJECT
public:
    explicit NotesPanel(Project* p, QWidget* parent = nullptr);
    void setProject(Project* p) { project = p; reload(); }
    void reload();
private:
    Project* project; QPlainTextEdit* edit;
};

class PresetsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PresetsPanel(QWidget* parent = nullptr);
    void refresh();
signals:
    void applyPreset(QString name);
private:
    QListWidget* list;
};

class RecorderPanel : public QWidget {
    Q_OBJECT
public:
    explicit RecorderPanel(QWidget* parent = nullptr);
signals:
    void voiceRequested();
    void screenRequested();
    void stopScreenRequested();
};

class FileBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit FileBrowserPanel(QWidget* parent = nullptr);
signals:
    void fileActivated(QString path);
};

class TimecodePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimecodePanel(QWidget* parent = nullptr);
    void setTime(double t, int fps);
private:
    QLabel* label;
};

class HotkeysPanel : public QWidget {
    Q_OBJECT
public:
    explicit HotkeysPanel(QWidget* parent = nullptr);
    void setActions(const QList<QAction*>& a) { actions = a; refresh(); }
    void refresh();
private:
    QTableWidget* table; QList<QAction*> actions;
};

class StatsPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatsPanel(QWidget* parent = nullptr);
    void setText(const QString& t);
private:
    QLabel* label;
};

class LogPanel : public QWidget {
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);
    void append(const QString& msg);
private:
    QPlainTextEdit* edit;
};

// Гистограмма RGB, осциллограф яркости и вектороскоп
class ScopesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScopesPanel(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(360, 150); }
    void setImage(const QImage& rgb);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int hist[3][256] = {};
    QImage wave, vec;
};

class FontsPanel : public QWidget {
    Q_OBJECT
public:
    explicit FontsPanel(QWidget* parent = nullptr);
signals:
    void applyFont(QString font);
private:
    void updatePreview();
    FontPicker* picker; QLineEdit* sample; QLabel* preview; QString font;
};
