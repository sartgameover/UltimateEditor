#pragma once
#include <QWidget>
#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QMap>
#include <QVector>
#include <QStringList>

class QComboBox; class QLineEdit; class QListWidget; class QSlider; class QLabel;

// ---- свой «штриховой» шрифт: каждая буква нарисована линиями (рисуется в редакторе шрифтов) ----
struct StrokeFont {
    QString name;
    double thickness = 0.08;                                        // толщина линии в долях размера
    QMap<int, QVector<QVector<QPointF>>> glyphs;                    // юникод -> штрихи (точки в квадрате 0..1)
};
QString strokeFontDir();
QStringList strokeFontNames();
bool loadStrokeFont(const QString& name, StrokeFont& out);
bool saveStrokeFont(const StrokeFont& f);

// ---- стиль текста и его отрисовка (тесные границы: текст можно двигать и масштабировать как отдельный объект) ----
struct TextStyle {
    QString text, font, color = "#ffffff", outlineColor = "#000000";
    double size = 1.0, outline = 2.0;
    bool bold = true, italic = false, shadow = false;
    int align = 1;                                                  // 0 слева, 1 центр, 2 справа
};
QImage renderTextImage(const TextStyle& s, int canvasW, int canvasH);
QImage renderStrokeTextImage(const StrokeFont& sf, const TextStyle& s, int px);

// ---- выбор шрифта: фильтр по письменности (латиница, кириллица, греческий, арабский, иероглифы…) ----
class FontPicker : public QWidget {
    Q_OBJECT
public:
    explicit FontPicker(QWidget* parent = nullptr);
    void setCurrent(const QString& font);
    QString current() const { return cur; }
    void refreshFonts();
signals:
    void fontChosen(QString font);
private:
    QComboBox* writing; QComboBox* fonts;
    QString cur;
    bool updating = false;
};

// ---- редактор своего шрифта ----
class GlyphCanvas;
class FontCreatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit FontCreatorDialog(QWidget* parent = nullptr);
private:
    void fillChars();
    void selectChar(int unicode);
    void updatePreview();
    StrokeFont font;
    int curChar = 'A';
    QLineEdit* nameEdit; QComboBox* group; QListWidget* chars; GlyphCanvas* canvas; QSlider* thick; QLabel* preview; QLineEdit* sample;
    QComboBox* existing;
};
