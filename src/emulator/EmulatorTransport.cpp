#include "EmulatorTransport.h"

#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>

#include "Elm327EmulatorEngine.h"

// ---- EmulatorTransport (base) ----

EmulatorTransport::EmulatorTransport(Elm327EmulatorEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine)
{
}

QByteArray EmulatorTransport::consume(QByteArray &buffer)
{
    QByteArray out;
    int nl;
    while ((nl = buffer.indexOf('\r')) >= 0) {
        const QByteArray cmd = buffer.left(nl).trimmed();
        buffer.remove(0, nl + 1);
        if (cmd.isEmpty())
            continue;
        const QByteArray reply = m_engine->handleLine(cmd);
        m_rx += cmd.size();
        m_tx += reply.size();
        out += reply;
        emit exchanged(cmd, reply);
    }
    return out;
}

// ---- TcpEmulatorTransport ----

TcpEmulatorTransport::TcpEmulatorTransport(Elm327EmulatorEngine *engine, quint16 port,
                                           QObject *parent)
    : EmulatorTransport(engine, parent), m_port(port)
{
}

TcpEmulatorTransport::~TcpEmulatorTransport() { stop(); }

bool TcpEmulatorTransport::start(QString *err)
{
    if (running())
        return true;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &TcpEmulatorTransport::onNewConnection);
    if (!m_server->listen(QHostAddress::Any, m_port)) {
        if (err)
            *err = m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }
    m_port = m_server->serverPort();
    emit statusChanged(QStringLiteral("Listening"));
    return true;
}

void TcpEmulatorTransport::stop()
{
    if (!m_server)
        return;
    const auto sockets = m_buffers.keys();
    for (QTcpSocket *s : sockets)
        s->disconnectFromHost();
    m_buffers.clear();
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
    emit statusChanged(QStringLiteral("Stopped"));
}

bool TcpEmulatorTransport::running() const { return m_server && m_server->isListening(); }

quint16 TcpEmulatorTransport::port() const { return m_port; }

void TcpEmulatorTransport::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        m_buffers.insert(sock, QByteArray());
        connect(sock, &QTcpSocket::readyRead, this, &TcpEmulatorTransport::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &TcpEmulatorTransport::onDisconnected);
        emit statusChanged(QStringLiteral("Client connected"));
        // Data can arrive before readyRead is connected (the client may send
        // immediately); drain anything already buffered so we don't miss it.
        if (sock->bytesAvailable() > 0)
            handleSocket(sock);
    }
}

void TcpEmulatorTransport::onReadyRead()
{
    handleSocket(qobject_cast<QTcpSocket *>(sender()));
}

void TcpEmulatorTransport::handleSocket(QTcpSocket *sock)
{
    if (!sock || !m_buffers.contains(sock))
        return;
    QByteArray &buf = m_buffers[sock];
    buf += sock->readAll();
    const QByteArray reply = consume(buf);
    if (!reply.isEmpty()) {
        sock->write(reply);
        sock->flush();
    }
    emit counts(m_rx, m_tx);
}

void TcpEmulatorTransport::onDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock)
        return;
    m_buffers.remove(sock);
    sock->deleteLater();
    emit statusChanged(running() ? QStringLiteral("Listening") : QStringLiteral("Stopped"));
}

// ---- SerialEmulatorTransport ----

SerialEmulatorTransport::SerialEmulatorTransport(Elm327EmulatorEngine *engine,
                                                 const QString &portName, QObject *parent)
    : EmulatorTransport(engine, parent), m_portName(portName)
{
}

SerialEmulatorTransport::~SerialEmulatorTransport() { stop(); }

bool SerialEmulatorTransport::start(QString *err)
{
    if (running())
        return true;
    m_serial = new QSerialPort(m_portName, this);
    m_serial->setBaudRate(QSerialPort::Baud115200);
    if (!m_serial->open(QIODevice::ReadWrite)) {
        if (err)
            *err = m_serial->errorString();
        m_serial->deleteLater();
        m_serial = nullptr;
        return false;
    }
    connect(m_serial, &QSerialPort::readyRead, this, &SerialEmulatorTransport::onReadyRead);
    m_buffer.clear();
    emit statusChanged(QStringLiteral("Listening"));
    return true;
}

void SerialEmulatorTransport::stop()
{
    if (!m_serial)
        return;
    m_serial->close();
    m_serial->deleteLater();
    m_serial = nullptr;
    m_buffer.clear();
    emit statusChanged(QStringLiteral("Stopped"));
}

bool SerialEmulatorTransport::running() const { return m_serial && m_serial->isOpen(); }

void SerialEmulatorTransport::onReadyRead()
{
    m_buffer += m_serial->readAll();
    const QByteArray reply = consume(m_buffer);
    if (!reply.isEmpty())
        m_serial->write(reply);
    emit counts(m_rx, m_tx);
}
