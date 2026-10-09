#include "Theme.h"
#include "AppSettings.h"
#include <QApplication>
#include <QPalette>
#include <QFont>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

Theme& Theme::instance() { static Theme t; return t; }

QStringList Theme::keys() const {
    return {"window", "panel", "text", "button", "buttonText", "accent", "timelineBg", "trackV", "trackA",
            "clipVideo", "clipAudio", "clipImage", "clipText", "playhead", "border"};
}

QString Theme::label(const QString& k) {
    static const QMap<QString, QString> m = {
        {"window", "Фон окон"}, {"panel", "Фон списков и полей"}, {"text", "Цвет текста"}, {"button", "Кнопки"},
        {"buttonText", "Текст кнопок"}, {"accent", "Акцент (выделение)"}, {"timelineBg", "Фон таймлайна"},
        {"trackV", "Видеодорожка"}, {"trackA", "Аудиодорожка"}, {"clipVideo", "Клип: видео"}, {"clipAudio", "Клип: звук"},
        {"clipImage", "Клип: фото"}, {"clipText", "Клип: текст"}, {"playhead", "Красная линия (плейхед)"}, {"border", "Рамки"}};
    return m.value(k, k);
}

QMap<QString, QColor> Theme::preset(const QString& mode) {
    QMap<QString, QColor> c;
    if (mode == "light") {
        c["window"] = "#eceef2"; c["panel"] = "#ffffff"; c["text"] = "#1c1e22"; c["button"] = "#dfe3ea"; c["buttonText"] = "#1c1e22";
        c["accent"] = "#3b82f6"; c["timelineBg"] = "#f4f5f8"; c["trackV"] = "#e3e7ee"; c["trackA"] = "#dde6e3";
        c["clipVideo"] = "#5b9bd5"; c["clipAudio"] = "#4fb59c"; c["clipImage"] = "#8bbd5a"; c["clipText"] = "#d9894a";
        c["playhead"] = "#e5484d"; c["border"] = "#b9bfca";
    } else {
        c["window"] = "#2b2d31"; c["panel"] = "#1e1f22"; c["text"] = "#e3e5e8"; c["button"] = "#383a40"; c["buttonText"] = "#e3e5e8";
        c["accent"] = "#3b6ea5"; c["timelineBg"] = "#1e1f22"; c["trackV"] = "#26282c"; c["trackA"] = "#22262a";
        c["clipVideo"] = "#3b6ea5"; c["clipAudio"] = "#2f6f62"; c["clipImage"] = "#6a8f3b"; c["clipText"] = "#a0602f";
        c["playhead"] = "#ff4d4f"; c["border"] = "#3a3d42";
    }
    return c;
}

void Theme::load() {
    themeMode = AppSettings::get("ui/theme", "dark").toString();
    cols = preset(themeMode == "light" ? "light" : "dark");
    if (themeMode == "custom") {
        QJsonObject o = QJsonDocument::fromJson(AppSettings::get("ui/custom_theme").toString().toUtf8()).object();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) cols[it.key()] = QColor(it.value().toString());
    }
}

void Theme::setMode(const QString& m) { AppSettings::set("ui/theme", m); load(); apply(); }

void Theme::setCustom(const QMap<QString, QColor>& c) {
    QJsonObject o;
    for (auto it = c.constBegin(); it != c.constEnd(); ++it) o[it.key()] = it.value().name();
    AppSettings::set("ui/custom_theme", QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    AppSettings::set("ui/theme", "custom");
    load(); apply();
}

void Theme::setFont(const QString& family, int size) {
    AppSettings::set("ui/font_family", family);
    AppSettings::set("ui/font_size", size);
    apply();
}

void Theme::apply() {
    if (cols.isEmpty()) load();
    QApplication* app = qApp ? static_cast<QApplication*>(QCoreApplication::instance()) : nullptr;
    if (!app) return;
    app->setStyle("Fusion");
    QPalette p;
    p.setColor(QPalette::Window, color("window"));
    p.setColor(QPalette::WindowText, color("text"));
    p.setColor(QPalette::Base, color("panel"));
    p.setColor(QPalette::AlternateBase, color("window"));
    p.setColor(QPalette::Text, color("text"));
    p.setColor(QPalette::Button, color("button"));
    p.setColor(QPalette::ButtonText, color("buttonText"));
    p.setColor(QPalette::Highlight, color("accent"));
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::ToolTipBase, color("panel"));
    p.setColor(QPalette::ToolTipText, color("text"));
    p.setColor(QPalette::PlaceholderText, color("text").darker(160));
    app->setPalette(p);
    QString fam = AppSettings::get("ui/font_family", "").toString();
    int sz = AppSettings::get("ui/font_size", 10).toInt();
    QFont f = app->font();
    if (!fam.isEmpty()) f.setFamily(fam);
    f.setPointSize(qBound(7, sz, 24));
    app->setFont(f);
    emit changed();
}

bool Theme::exportTo(const QString& file) const {
    QJsonObject o;
    for (auto it = cols.constBegin(); it != cols.constEnd(); ++it) o[it.key()] = it.value().name();
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson());
    return true;
}

bool Theme::importFrom(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty()) return false;
    QMap<QString, QColor> c = preset("dark");
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) c[it.key()] = QColor(it.value().toString());
    setCustom(c);
    return true;
}
