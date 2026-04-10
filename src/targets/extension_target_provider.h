// ExtensionTargetProvider — knows about DOM elements that the browser
// extension reported. lives on top of an ExtensionPipeServer and acts
// as the bridge between the heist orchestrator (which speaks in
// HeistTargets and synchronous "give me one" calls) and the async
// extension protocol (scan request → scan_result eventually).
//
// flow:
//   - on bridge connect, kick off the auto-scan timer (every 3s)
//   - each scan request sends {"type":"scan_targets"} on the pipe
//   - the extension's background.js scans the active tab's dom and
//     replies with {"type":"scan_result","items":[{id,rect,label}...]}
//   - we cache the items as DomTargets, dpi-corrected
//   - pickRandom() reads the cache, returns nullopt if empty/stale
//
// for the heist flow we expose requestSteal(id) / requestRestore(id)
// which fire-and-forget messages to the extension. the bridge ack is
// non-blocking — heist states proceed when the ack lands via signal.
#pragma once

#include "targets/target_provider.h"

#include <QHash>
#include <QObject>
#include <QRect>
#include <QString>
#include <QVector>
#include <optional>

class QJsonObject;
class QTimer;

namespace cr {

class ExtensionPipeServer;

struct DomTarget {
    QString id;       // extension-assigned, e.g. "creechr-3"
    QString tag;      // "img", "a", "button", "li"
    QRect screenRect; // already dpi-corrected
    QString label;
};

class ExtensionTargetProvider : public QObject
{
    Q_OBJECT

public:
    ExtensionTargetProvider(ExtensionPipeServer* server, QObject* parent = nullptr);

    std::optional<HeistTarget> pickRandom(const QRect& virtualDesktop) const;

    // fire-and-forget. ack arrives via stealAcked / restoreAcked signals.
    void requestSteal(const QString& targetId);
    void requestRestore(const QString& targetId);

    // last successful scan time, in ms since epoch. 0 = never.
    qint64 lastScanMs() const { return m_lastScanMs; }

    bool isReady() const;

signals:
    void stealAcked(const QString& targetId);
    void restoreAcked(const QString& targetId);

private slots:
    void onBridgeConnected();
    void onBridgeDisconnected();
    void onPipeMessage(const QJsonObject& msg);
    void onAutoScanTimer();

private:
    void requestScan();

    ExtensionPipeServer* m_server;
    QTimer* m_autoScanTimer = nullptr;
    QVector<DomTarget> m_cached;
    qint64 m_lastScanMs = 0;
};

} // namespace cr
