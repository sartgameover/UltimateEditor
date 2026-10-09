#pragma once
#include <QObject>
#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>

class QApplication;

// Темы оформления: тёмная, светлая и своя (каждый цвет и шрифт настраиваются).
class Theme : public QObject {
    Q_OBJECT
public:
    static Theme& instance();
    QColor color(const QString& key) const { return cols.value(key, QColor("#ff00ff")); }
    QString mode() const { return themeMode; }                       // dark | light | custom
    QStringList keys() const;
    static QString label(const QString& key);                        // подпись для редактора
    static QMap<QString, QColor> preset(const QString& mode);
    QMap<QString, QColor> colors() const { return cols; }
    void load();                                                     // из настроек
    void setMode(const QString& m);                                  // dark | light | custom
    void setCustom(const QMap<QString, QColor>& c);                  // свои цвета (и режим custom)
    void setFont(const QString& family, int size);
    void apply();                                                    // применить к приложению
    bool exportTo(const QString& file) const;
    bool importFrom(const QString& file);
signals:
    void changed();
private:
    Theme() {}
    QString themeMode = "dark";
    QMap<QString, QColor> cols;
};
