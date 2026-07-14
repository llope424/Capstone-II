#pragma once

#include <QFile>
#include <QObject>
#include <QTextStream>
#include <QTimer>
#include <QVector>

#include "CanFrame.h"

// Records a diagnostic session (incoming CAN frames) to a CSV file, and replays
// a previously recorded file back into the app on a timer that approximates the
// original inter-frame timing. Covers the SDD data-logging requirements:
// start/stop recording, save recorded data, and replay recorded sessions.
class SessionLogger : public QObject
{
    Q_OBJECT

public:
    explicit SessionLogger(QObject *parent = nullptr);
    ~SessionLogger() override;

    bool isRecording() const { return m_recording; }
    bool isReplaying() const { return m_replayTimer.isActive(); }

    bool startRecording(const QString &filePath);
    void stopRecording();
    void recordFrame(const CanFrame &frame);

    // Loads a CSV recording and streams its frames via frameReplayed() on a timer.
    bool startReplay(const QString &filePath);
    void stopReplay();

signals:
    void frameReplayed(const CanFrame &frame);
    void replayFinished();
    void logMessage(const QString &text);

private slots:
    void emitNextReplayFrame();

private:
    // Recording state
    QFile m_file;
    QTextStream m_stream;
    bool m_recording = false;
    quint64 m_recordCount = 0;

    // Replay state
    QVector<CanFrame> m_replayFrames;
    int m_replayIndex = 0;
    QTimer m_replayTimer;
};
