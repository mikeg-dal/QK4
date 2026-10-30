#ifndef DSP_POWERAVERAGER_H
#define DSP_POWERAVERAGER_H

#include <QVector>

// The radio's #AVG n as QK4 draws it: the mean POWER of each bin over the last n frames, in dB.
//
// WHY power: averaging dB values pulls noise down (for noise the mean of the dB readings sits ~2.5 dB
// under the dB of the mean power) and flattens its peaks; at #AVG 3 QK4's noise grass came out about
// half as tall and 2-3 dB lower than the K4's (bench 2026-10-30).
// WHY the last n frames and not a running blend: a running power average lets a strong signal's power
// dominate long after the signal stops - a 60 dB carrier took over a second to fall away at #AVG 3,
// a tail the trace and the waterfall both showed (bench 2026-10-30). Over a fixed window a stopped
// signal is gone after n frames, whatever its level. The K4 does not average the stream it sends
// (measured: frame-to-frame variation identical at #AVG 1 and 20), so this runs client-side.
//
// Not thread-safe; used on the GUI thread by the panadapter and the mini-pan.
class PowerAverager {
public:
    static constexpr int MaxLength = 20; // #AVG range 1..20

    // Frames to average (#AVG level, clamped 1..MaxLength). Changing it restarts the average.
    void setLength(int frames);
    int length() const { return m_length; }

    // Forget the history: the next frame is drawn as it arrives.
    void reset();

    // Adds one frame (dBm per bin) and writes the mean power of the frames held so far (up to
    // length()), in dBm, to out. A frame with a different bin count restarts the average.
    void add(const QVector<float> &frameDbm, QVector<float> &outDbm);

    // For the drawn trace: add() the frame, then move shownDbm halfway (TraceEase) toward the average,
    // in dB. A plain n-frame average jumps when a loud frame enters or leaves the window, which reads
    // as a stutter at 15 frames a second (bench 2026-10-30, voice); easing in dB softens the steps
    // and still settles fast (a 60 dB drop is within 1 dB after 6 frames, no long tail). At length 1
    // (#AVG 1) the frame is shown as it arrives, like the K4. A bin-count change shows the average
    // directly. The waterfall uses add(): its rows need no easing.
    void addEased(const QVector<float> &frameDbm, QVector<float> &shownDbm);
    static constexpr float TraceEase = 0.5f;

    int framesHeld() const { return m_count; }

private:
    void restart(int bins);

    int m_length = 1;
    int m_bins = 0;
    int m_count = 0;          // frames held, <= m_length
    int m_head = 0;           // ring slot the next frame goes into
    int m_sinceSum = 0;       // frames since the running sums were recomputed from the ring
    QVector<double> m_ring;   // m_length x m_bins linear powers, oldest overwritten
    QVector<double> m_sum;    // per-bin sum of the frames held
    QVector<float> m_average; // addEased's scratch: the n-frame average before easing
};

#endif // DSP_POWERAVERAGER_H
