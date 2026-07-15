#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QTcpServer;
class QTcpSocket;
class QSerialPort;
QT_END_NAMESPACE

class Elm327EmulatorEngine;

// Base for the emulator's servers. A transport receives request bytes from a
// client, splits them into CR-terminated commands, feeds each to the engine, and
// writes the reply back. Concrete transports differ only in the byte pipe (a TCP
// socket or a serial/com0com port).
class EmulatorTransport : public QObject
{
    Q_OBJECT

public:
    explicit EmulatorTransport(Elm327EmulatorEngine *engine, QObject *parent = nullptr);

    virtual bool start(QString *err) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;

signals:
    void statusChanged(const QString &status); // "Listening" / "Client connected" / "Stopped"
    void counts(quint64 rx, quint64 tx);
    void exchanged(const QByteArray &rx, const QByteArray &tx);

protected:
    // Pull CR-terminated commands out of buffer, run them, return bytes to send back.
    QByteArray consume(QByteArray &buffer);

    Elm327EmulatorEngine *m_engine;
    quint64 m_rx = 0;
    quint64 m_tx = 0;
};

// Serves the emulator over TCP (default port 35000, matching WiFi ELM327 tools).
class TcpEmulatorTransport : public EmulatorTransport
{
    Q_OBJECT

public:
    explicit TcpEmulatorTransport(Elm327EmulatorEngine *engine, quint16 port = 35000,
                                  QObject *parent = nullptr);
    ~TcpEmulatorTransport() override;

    bool start(QString *err) override;
    void stop() override;
    bool running() const override;
    quint16 port() const; // actual listening port (useful when constructed with 0)

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleSocket(QTcpSocket *sock); // read -> engine -> write for one client

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    quint16 m_port;
};

// Serves the emulator over a serial port (one end of a com0com pair on Windows).
class SerialEmulatorTransport : public EmulatorTransport
{
    Q_OBJECT

public:
    explicit SerialEmulatorTransport(Elm327EmulatorEngine *engine, const QString &portName,
                                     QObject *parent = nullptr);
    ~SerialEmulatorTransport() override;

    bool start(QString *err) override;
    void stop() override;
    bool running() const override;

private slots:
    void onReadyRead();

private:
    QSerialPort *m_serial = nullptr;
    QString m_portName;
    QByteArray m_buffer;
};
