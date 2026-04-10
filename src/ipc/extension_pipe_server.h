// ExtensionPipeServer — listens on a local named pipe for connections
// from creechr-bridge.exe (which chrome / edge spawn when the browser
// extension's background script calls connectNative('com.creechr.bridge')).
//
// pipe name: "creechr-extension". on windows that resolves to
// \\.\pipe\creechr-extension via QLocalServer's name mangling. the
// bridge opens it directly with CreateFile.
//
// protocol: newline-delimited json on the wire. one json object per
// line. nothing fancy. messages flow in both directions:
//
//   bridge -> creechr:
//     {"type":"hello","ext":"<extension id>"}
//     {"type":"scan_result","items":[...]}
//     {"type":"steal_ack","id":"creechr-N"}
//     {"type":"restore_ack","id":"creechr-N"}
//     {"type":"goodbye"}
//
//   creechr -> bridge:
//     {"type":"scan"}
//     {"type":"steal","id":"creechr-N"}
//     {"type":"restore","id":"creechr-N"}
//
// only one bridge can be connected at a time in v1.0 — if a second
// connects, we accept it and replace the previous. last-bridge-wins.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace cr {

class ExtensionPipeServer : public QObject
{
    Q_OBJECT

public:
    explicit ExtensionPipeServer(QObject* parent = nullptr);
    ~ExtensionPipeServer() override;

    bool start(); // begin listening. true on success.

    bool isConnected() const;
    QString extensionId() const { return m_extId; }

    // send a json message to the connected bridge. returns false if
    // there's no bridge or the write failed. caller doesnt need to
    // include a trailing newline; we add it.
    bool send(const QJsonObject& message);

signals:
    void bridgeConnected();
    void bridgeDisconnected();
    // every received line, parsed as json. consumers (e.g. ExtensionTargetProvider)
    // hook this and dispatch on the "type" field.
    void messageReceived(const QJsonObject& message);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    QLocalServer* m_server = nullptr;
    QLocalSocket* m_client = nullptr;
    QByteArray    m_rxBuffer;
    QString       m_extId;
};

} // namespace cr
