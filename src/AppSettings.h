#pragma once
#include <QSettings>
#include <QVariant>
#include <QString>

// Единое место для настроек всей программы (всё хранится в QSettings)
namespace AppSettings {
inline QVariant get(const QString& key, const QVariant& def = QVariant()) { return QSettings("UltimateEditor", "UltimateEditor").value(key, def); }
inline void set(const QString& key, const QVariant& v) { QSettings("UltimateEditor", "UltimateEditor").setValue(key, v); }

inline double imageDuration() { return get("general/image_sec", 5.0).toDouble(); }
inline int undoLimit() { return get("general/undo_limit", 100).toInt(); }
inline bool showStartScreen() { return get("general/start_screen", true).toBool(); }
inline int autosaveSec() { return get("autosave_sec", 60).toInt(); }
inline int trackHeight() { return get("timeline/track_height", 52).toInt(); }
inline int snapPixels() { return get("timeline/snap_px", 8).toInt(); }
inline bool showWaveforms() { return get("timeline/waveforms", true).toBool(); }
inline bool loopPlayback() { return get("player/loop", false).toBool(); }
inline bool showGrid() { return get("player/grid", false).toBool(); }
inline bool showSafeZones() { return get("player/safe", false).toBool(); }
inline int audioLatencyMs() { return get("audio/latency_ms", 80).toInt(); }
inline QByteArray audioOutputId() { return get("audio/output_id").toByteArray(); }
inline int proxyThreshold() { return get("perf/proxy_height", 1440).toInt(); }
}
