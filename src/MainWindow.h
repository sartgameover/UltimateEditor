#pragma once
#include "Project.h"
#include <QMainWindow>
#include <QTimer>
#include <QMap>
#include <QElapsedTimer>

class PlayerView; class TimelineWidget; class TransportBar; class Inspector; class MixerPanel;
class AssetsPanel; class EffectsTree; class AnimationsList; class QTabWidget; class QDockWidget;
class VoiceRecorder; class ScreenRecorder; class RecordHud;
class MarkersPanel; class ProjectPropsPanel; class StructurePanel; class NotesPanel; class PresetsPanel; class RecorderPanel;
class FileBrowserPanel; class TimecodePanel; class HotkeysPanel; class StatsPanel; class LogPanel; class ScopesPanel; class FontsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void importPaths(const QStringList& paths, double t = 0, const QString& track = "V1", bool toTimeline = true);
    bool openProjectFile(const QString& path);
protected:
    void closeEvent(QCloseEvent* e) override;
private:
    QDockWidget* addDock(const QString& title, const QString& obj, QWidget* w, Qt::DockWidgetArea area);
    void buildMenus();
    void setProject();
    void refreshAssets();
    void newProject(); void openProject(); void saveProject(bool saveAs); void recover();
    void importDialog(); void exportDialog();
    void defaultLayout();
    void applySettings();
    void refreshPanels();
    QDockWidget* addExtra(const QString& title, const QString& obj, QWidget* w);
    void applyToClip(const ClipPtr& c, const QString& spec);     // "fx:Имя" | "afx:Имя" | "anim:Имя" | "preset:Имя"
    ClipPtr selectedOrAtPlayhead();
    void startScreenRecording(); void stopScreenRecording();
    void applyEffect(const QString& name);
    void applyPreset(const QString& name);
    void applyAnim(const QString& name);
    void addText(double t); void editTextClip(const ClipPtr& c);
    void makeProxy(int assetId);
    ClipPtr target();
    void autosave();
    void showAllDocks();
    void showShortcuts();
    // undo / буфер обмена
    void pushUndo(const QString& key = QString());
    void undo(); void redo(); void restore(const QByteArray& snap);
    void copySelection(); void pasteAt(double t); void duplicateSelection();
    // запись голоса
    void toggleRecording(); void beginRecording(); void stopRecording();

    Project project;
    PlayerView* player; TimelineWidget* timeline; TransportBar* transport; Inspector* inspector; MixerPanel* mixer;
    AssetsPanel* assets; EffectsTree* fxTree; AnimationsList* anims; QTabWidget* assetTabs;
    QList<QDockWidget*> docks;
    QDockWidget* dockAssets = nullptr;
    QDockWidget *dMonitor = nullptr, *dInsp = nullptr, *dMix = nullptr, *dTl = nullptr;
    ScreenRecorder* screenRec = nullptr;
    RecordHud* hud = nullptr;
    QString screenPath;
    // дополнительные окна (по умолчанию скрыты, включаются в меню «Вид»)
    QList<QDockWidget*> extraDocks;
    QList<QAction*> allActions;
    QTimer panelTimer;
    QDockWidget* dScopes = nullptr;
    EffectsTree* fxTree2 = nullptr; EffectsTree* afxTree = nullptr; AnimationsList* anims2 = nullptr;
    MarkersPanel* markersP = nullptr; ProjectPropsPanel* propsP = nullptr; StructurePanel* structP = nullptr; NotesPanel* notesP = nullptr;
    PresetsPanel* presetsP = nullptr; RecorderPanel* recP = nullptr; FileBrowserPanel* filesP = nullptr; TimecodePanel* tcP = nullptr;
    HotkeysPanel* keysP = nullptr; StatsPanel* statsP = nullptr; LogPanel* logP = nullptr; ScopesPanel* scopesP = nullptr; FontsPanel* fontsP = nullptr;
    QTimer autosaveTimer, meterTimer;
    QString cacheDir;
    QList<QByteArray> undoStack, redoStack;
    QElapsedTimer undoClock;
    QString undoKey;
    QList<Clip> clipboard;
    VoiceRecorder* recorder = nullptr;
    QString recTrack;
    double recStart = 0;
    bool recActive = false;
};
