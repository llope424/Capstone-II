#include "SessionLogger.h"

#include <QStringList>

SessionLogger::SessionLogger(QObject *parent) : QObject(parent)
{
    connect(&m_replayTimer, &QTimer::timeout, this, &SessionLogger::emitNextReplayFrame);
}

SessionLogger::~SessionLogger()
{
    stopRecording();
}

bool SessionLogger::startRecording(const QString &filePath)
{
    if (m_recording)
        stopRecording();

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit logMessage("Could not open log file for writing: " + m_file.errorString());
        return false;
    }
    m_stream.setDevice(&m_file);
    // Header compatible with common CAN log tooling.
    m_stream << "timestamp_us,id,extended,bus,dlc,data\n";
    m_recording = true;
    m_recordCount = 0;
    emit logMessage("Recording session to " + filePath);
    return true;
}

void SessionLogger::stopRecording()
{
    if (!m_recording)
        return;
    m_stream.flush();
    m_file.close();
    m_recording = false;
    emit logMessage(QString("Recording stopped (%1 frames saved).").arg(m_recordCount));
}

void SessionLogger::recordFrame(const CanFrame &frame)
{
    if (!m_recording)
        return;

    QStringList dataBytes;
    for (int i = 0; i < frame.length; ++i)
        dataBytes << QStringLiteral("%1").arg(frame.data[i], 2, 16, QChar('0')).toUpper();

    m_stream << frame.timestampUs << ','
             << QStringLiteral("0x%1").arg(frame.id, frame.extended ? 8 : 3, 16, QChar('0')).toUpper() << ','
             << (frame.extended ? 1 : 0) << ','
             << frame.bus << ','
             << frame.length << ','
             << dataBytes.join(' ') << '\n';
    m_recordCount++;
}

bool SessionLogger::startReplay(const QString &filePath)
{
    stopReplay();

    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage("Could not open replay file: " + in.errorString());
        return false;
    }

    m_replayFrames.clear();
    QTextStream ts(&in);
    bool firstLine = true;
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (firstLine) { // skip header
            firstLine = false;
            if (line.startsWith("timestamp", Qt::CaseInsensitive))
                continue;
        }
        const QStringList cols = line.split(',');
        if (cols.size() < 6)
            continue;

        CanFrame f;
        f.timestampUs = cols[0].toUInt();
        f.id = cols[1].trimmed().startsWith("0x", Qt::CaseInsensitive)
                   ? cols[1].trimmed().mid(2).toUInt(nullptr, 16)
                   : cols[1].trimmed().toUInt(nullptr, 16);
        f.extended = cols[2].trimmed().toInt() != 0;
        f.bus = static_cast<quint8>(cols[3].trimmed().toInt());
        f.length = static_cast<quint8>(qBound(0, cols[4].trimmed().toInt(), 8));
        const QStringList bytes = cols[5].trimmed().split(' ', Qt::SkipEmptyParts);
        for (int i = 0; i < f.length && i < bytes.size(); ++i)
            f.data[i] = static_cast<quint8>(bytes[i].toUInt(nullptr, 16));
        m_replayFrames.append(f);
    }
    in.close();

    if (m_replayFrames.isEmpty()) {
        emit logMessage("Replay file contained no frames.");
        return false;
    }

    m_replayIndex = 0;
    emit logMessage(QString("Replaying %1 frames from %2").arg(m_replayFrames.size()).arg(filePath));
    // Kick off immediately; emitNextReplayFrame schedules subsequent frames
    // according to their recorded timestamps.
    m_replayTimer.start(0);
    return true;
}

void SessionLogger::stopReplay()
{
    m_replayTimer.stop();
    m_replayFrames.clear();
    m_replayIndex = 0;
}

void SessionLogger::emitNextReplayFrame()
{
    if (m_replayIndex >= m_replayFrames.size()) {
        m_replayTimer.stop();
        emit replayFinished();
        emit logMessage("Replay finished.");
        return;
    }

    const CanFrame &frame = m_replayFrames.at(m_replayIndex);
    emit frameReplayed(frame);
    m_replayIndex++;

    // Schedule the next frame using the delta between recorded timestamps,
    // clamped so a big gap or clock wrap doesn't stall the replay.
    if (m_replayIndex < m_replayFrames.size()) {
        const quint32 t0 = frame.timestampUs;
        const quint32 t1 = m_replayFrames.at(m_replayIndex).timestampUs;
        int deltaMs = (t1 >= t0) ? int((t1 - t0) / 1000) : 0;
        deltaMs = qBound(0, deltaMs, 1000);
        m_replayTimer.start(deltaMs);
    } else {
        m_replayTimer.start(0);
    }
}
