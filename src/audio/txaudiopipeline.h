#ifndef TXAUDIOPIPELINE_H
#define TXAUDIOPIPELINE_H

#include <QAudioFormat>
#include <QAudioSource>
#include <QElapsedTimer>
#include <QIODevice>
#include <QMediaDevices>
#include <QObject>
#include <QTimer>
#include <atomic>

#include "audio/audiodiagnostics.h"
#include "audio/audioresampler.h"
#include "audio/opusencoder.h"
#include "utils/spscring.h"

/**
 * @brief The transmit audio path: microphone capture, TCI transmit audio, 12 kHz conversion, frame
 *        buffering, EM0-EM3 encoding and packetising for the K4.
 *
 * Lives on its own thread, AudioController::m_txThread ("AudioTX"), not on the RX audio thread.
 *
 * WHY its own thread: opening a microphone is slow and synchronous - enumerating devices,
 * QAudioSource::start(), on some systems an OS permission or Bluetooth profile switch, 200 ms to
 * 1.5 s measured on PipeWire, CoreAudio and WASAPI. On the RX thread that stalled the speaker feed
 * for the duration. Here it delays only the start of the transmission it belongs to, and the K4 is
 * half-duplex - it stops sending RX audio while transmitting - so nothing else is waiting on it.
 *
 * WHY the microphone is open only while transmitting from it: the OS shows a microphone-in-use
 * indicator (macOS, Windows) for as long as a capture stream runs, and an operator is entitled to
 * have it lit only when the room is actually being sent. It is opened on PTT-on when the microphone
 * is the TX source, and closed on PTT-off. A TCI transmission never opens it.
 *
 * Thread-safety: the setters marked as atomic are safe from any thread. Everything else is
 * Q_INVOKABLE and must be called queued; it runs on the TX thread.
 */
class TxAudioPipeline : public QObject {
    Q_OBJECT

public:
    enum class TxSource { Microphone = 0, Tci = 1 };

    // Whether the OS lets QK4 record from the microphone (QMicrophonePermission).
    enum class MicAccess {
        Undetermined = 0, // not asked yet, or the operator is answering the OS prompt right now
        Granted = 1,
        Denied = 2,
    };

    explicit TxAudioPipeline(QObject *parent = nullptr);
    ~TxAudioPipeline() override;

    // Teardown at disconnect or quit. Closes the microphone and clears transmit state (AUD-001).
    Q_INVOKABLE void stop();

    // PTT gate. On the rising edge, restarts the frame sequence and the partial-frame buffer and
    // opens the microphone if it is the source; on the falling edge closes it. `requestedAtNs` is
    // std::chrono::steady_clock time when PTT was asked for, so the delay to the first packet on
    // the wire can be measured from the operator's press, queueing included. 0 = unknown.
    Q_INVOKABLE void setPttActive(bool active, qint64 requestedAtNs = 0);
    bool isPttActive() const { return m_pttActive.load(std::memory_order_relaxed); }

    // Where TX audio comes from. Atomic, safe from any thread; queues its own buffer flush.
    //
    // WHY this must exist alongside any remote PTT: a TCI client keying the radio without a source
    // selector would transmit whatever the room hears. The two sources are never mixed.
    void setTxSource(TxSource source);
    TxSource txSource() const { return static_cast<TxSource>(m_txSource.load(std::memory_order_relaxed)); }

    // The microphone permission as the main thread last learned it. Atomic, safe from any thread;
    // queues a reconcile so a transmission waiting on the answer opens the microphone once granted.
    //
    // WHY the microphone is never opened unless this is Granted: with the permission undetermined,
    // starting a capture is what makes macOS ask, and the asking happened inside
    // QAudioSource::start() on THIS thread - which then sat in openMic() for as long as the prompt
    // was up, and took quit with it (stop() was a blocking call queued behind it). Permissions can
    // only be requested from the main thread (QCoreApplication::requestPermission), so that is
    // where AudioController asks; this thread only obeys the answer.
    void setMicAccess(MicAccess access);
    MicAccess micAccess() const { return static_cast<MicAccess>(m_micAccess.load(std::memory_order_acquire)); }

    // Feed 48 kHz mono Float32 from a TCI client. Ignored unless the TX source is Tci.
    //
    // Passes through m_tciTxGain - its OWN level, not the microphone's. WSJT-X transmits at or near
    // full scale, so without an operator-facing level this drives the K4 far too hard, as it did on
    // the first on-air test. It shared m_micGain for a while, which fixed the overdrive but created
    // a second problem: the curve is cubic, so a position calibrated for a microphone is far too
    // low for a line-level digital source and vice versa. There is no single position that serves
    // both, so there are now two controls.
    Q_INVOKABLE void feedTciTxAudio(const QByteArray &f32Mono48k);

    // Atomic, safe from any thread.
    void setFrameSamples(int samples); // 240, 480, 720, or 1440 (the SL tier)
    // TX encode mode (0=RAW S32LE (24-bit), 1=RAW S16LE, 2/3=Opus (same bitstream, int vs float decode)). See
    // audio/rawaudioformat.h for the RAW wire scales.
    void setEncodeMode(int mode);

    Q_INVOKABLE void setMicGain(float gain);   // 0.0 to 1.0, cubic
    Q_INVOKABLE void setTciTxGain(float gain); // 0.0 to 1.0, the same cubic curve
    Q_INVOKABLE void setMicDevice(const QString &deviceId);
    // Only for the operator-facing explanation when a Bluetooth microphone delivers nothing.
    Q_INVOKABLE void setOutputDeviceHint(const QString &deviceId);
    Q_INVOKABLE void flushMicBuffer();

    // Consumer side of the qk4.audio.tx diagnostics: ONE thread only (AudioController's main-thread
    // sampler). False when there is nothing to report.
    bool takeTxFrameReport(AudioDiagnostics::TxFrameReport &out);

    // (id, description), "System Default" first. For the settings UI.
    static QList<QPair<QString, QString>> availableInputDevices();

signals:
    // Encoded TX packet ready for the wire. Emitted on the TX thread; TcpClient::sendRaw is queued
    // to the I/O thread.
    void txPacketReady(const QByteArray &packet);

    // The microphone could not be opened, with the reason as a sentence for the operator. The
    // operator has to act - a different device, or a different rate.
    //
    // WHY A SIGNAL AND NOT JUST THE LOG: every path that sets this emits a qCWarning already, and
    // has for some time. The field reports still arrived as "PTT does not work", because a local
    // PTT press keys the K4 by streaming audio - no microphone means no audio means the radio never
    // keys - and the explanation was only ever in a log nobody reads unless they start QK4 from a
    // terminal. Emitted on the TX thread; connect queued.
    void micUnavailable(const QString &reason);

    // Once per PTT press, on the first packet handed to the wire: microseconds from the PTT request
    // to that packet, and how much of it was spent opening the microphone (0 when it was not
    // opened). firstPacketUs is -1 when PTT was released before any packet was sent. Emitted on the
    // TX thread; connect queued and format it on the receiving thread, not here.
    void pttLatencyMeasured(qint64 firstPacketUs, qint64 micOpenUs, bool fromMicrophone);

private slots:
    void onMicDataReady();
    // WHY: when the user leaves the device on "System Default" (empty id), follow the OS default
    // live instead of caching it for the whole session. Pinned devices are reclaimed when they
    // reappear. See audio/audiodeviceselection.h.
    void onSystemDefaultInputChanged();

private:
    bool setupAudioInput();
    void openMic();
    void closeMic();
    // Open or close the microphone to match PTT and the TX source.
    void reconcileMic();

    // Shared tail of both TX sources: take 12 kHz Float32, apply `gain`, buffer it, and emit whole
    // SL-tier frames while PTT is asserted.
    void bufferAndEmitTxFrames(const QByteArray &pcm12k, float gain);

    // Decimate Float32 samples to the K4's 12 kHz by `factor` (averaging), writing into the
    // pre-allocated m_resampleBuf12k member and returning a const reference to it. The microphone
    // passes the factor for the rate its device actually opened at (see audio/audiodecimator.h);
    // TCI passes a fixed 4, because TCI audio is always 48 kHz.
    const QByteArray &resampleTo12k(const QByteArray &input, int factor);

    // Resample Float32 microphone samples to 12 kHz when the capture rate is NOT an exact multiple
    // of it, writing into m_resampleBuf12k. Stateful across calls: the filter history and phase
    // have to survive block boundaries or every poll starts a discontinuity.
    const QByteArray &rationalResampleTo12k(const QByteArray &input);

    // Emit micUnavailable, but only while the microphone is the TX source. The qCWarning at each
    // call site still records the fault.
    void reportMicUnavailable(const QString &reason);

    // Encode + packetize one captured Float32 mono frame and emit txPacketReady.
    void encodeAndSendFrame(const QByteArray &f32MonoFrame, int frameSamples, int encodeMode);

    // Record what one TX frame actually put on the wire, for qk4.audio.tx. Fills a record in
    // m_txDiagnostics; formatting happens on the main thread (audio/audiodiagnostics.h).
    void recordTxFrameDiagnostic(const QByteArray &f32MonoFrame, const QByteArray &wireData, int frameSamples,
                                 int encodeMode);
    SpscRing<AudioDiagnostics::TxFrameReport, 16> m_txDiagnostics;

    // Frames between qk4.audio.tx reports during a transmission. The first frame of
    // every transmission is always reported; this throttles the rest.
    static constexpr int TX_DIAG_FRAME_INTERVAL = 50;

    // The microphone's ACTUAL capture rate, taken from the device rather than demanded of it.
    // 0 until a device has been opened. See setupAudioInput().
    int m_micSampleRate = 0;
    // Non-zero when the rate is an exact multiple of 12 kHz and AudioDecimator handles it. Zero
    // when m_micResampler is doing the work instead - the two are mutually exclusive, and the
    // integer path is kept for the rates that already worked so their audio does not change.
    int m_micDecimationFactor = 0;
    AudioResampler::Resampler m_micResampler;

    // Mic-silence watchdog. A microphone that opens successfully and then delivers nothing looks
    // exactly like a quiet room from everywhere downstream, so PTT lights the indicator, the gate
    // opens, and the operator transmits nothing while believing they are on the air. That is
    // INT-005, and it stayed undiagnosed because the read path treats an empty read as normal -
    // which it is, most of the time.
    // How long a keyed transmitter may produce no microphone data before we say so.
    static constexpr qint64 MIC_SILENCE_WARN_MS = 500;
    QElapsedTimer m_micSilenceTimer;
    bool m_micSilenceReported = false;

    // Loudest mic sample since PTT engaged, tracked across EVERY frame while qk4.audio.tx is on.
    // WHY: reporting only the throttled frames' own peaks made short transmissions unreadable -
    // a brief over keys, logs frame 0 (still silence, before the operator speaks) and ends before
    // frame 50, so the log showed a near-zero peak next to a high ALC reading.
    float m_txPeakSinceKey = 0.0f;

    // PTT -> first packet measurement, per transmission. TX thread only.
    qint64 m_pttRequestedAtNs = 0;
    qint64 m_micOpenUs = 0;
    bool m_awaitingFirstPacket = false;

    // Audio input format template: mono Float32; the rate is the device's own (setupAudioInput).
    QAudioFormat m_inputFormat;

    QAudioSource *m_audioSource = nullptr;
    QIODevice *m_audioSourceDevice = nullptr;
    bool m_micOpen = false;
    QString m_selectedMicDeviceId; // Empty = use system default
    QString m_activeMicDeviceId;   // id of the device the source was actually opened on
    QString m_outputDeviceHint;    // the speaker device, for the Bluetooth explanation only
    QMediaDevices *m_mediaDevices = nullptr;

    std::atomic<float> m_micGain{0.25f}; // Default 25% (macOS mic input is typically hot)
    // Default 25% slider -> 0.015625x, set so an injected signal keeps the K4's ALC at or below 5.
    // See kTciTxGainDefault. Same number as m_micGain's default by coincidence, not by derivation.
    std::atomic<float> m_tciTxGain{0.015625f};

    // Input: 48kHz * 4 bytes/sample * 0.1 sec = 19200 bytes
    static constexpr int INPUT_BUFFER_SIZE = 19200;

    OpusEncoder m_opusEncoder;            // by value: used only on the TX thread
    quint8 m_txSequence = 0;              // TX-thread only
    std::atomic<int> m_encodeMode{3};     // EM3 (Opus float) default
    std::atomic<bool> m_pttActive{false}; // TX gate; read on every frame
    std::atomic<int> m_txSource{static_cast<int>(TxSource::Microphone)};
    std::atomic<int> m_micAccess{static_cast<int>(MicAccess::Undetermined)};

    // Frame buffering: 12 kHz Float32 until a whole SL-tier frame (240/480/720/1440 samples).
    std::atomic<int> m_frameSamples{240};
    QByteArray m_micBuffer;
    // Offset into m_micBuffer of the next byte to emit; compacted lazily instead of a per-poll
    // memmove from QByteArray::remove(0, n).
    int m_micReadOffset = 0;

    // Pre-allocated scratch for resampleTo12k, sized for the worst case (the whole input buffer).
    QByteArray m_resampleBuf12k;

    // Timer for polling microphone data (more reliable than readyRead signal)
    QTimer *m_micPollTimer = nullptr;
};

#endif // TXAUDIOPIPELINE_H
