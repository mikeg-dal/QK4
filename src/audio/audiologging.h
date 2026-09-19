#ifndef AUDIO_AUDIOLOGGING_H
#define AUDIO_AUDIOLOGGING_H

#include <QLoggingCategory>

// Shared logging category for the audio subsystem (AudioController, AudioEngine,
// OpusDecoder, OpusEncoder, SidetoneGenerator). Definition lives in audiocontroller.cpp.
//
// Runtime filter example:
//     QT_LOGGING_RULES="qk4.audio.debug=false"
Q_DECLARE_LOGGING_CATEGORY(qk4Audio)

// TX wire-format diagnostic, separate so it can be switched on without the rest of the
// audio subsystem's chatter. Off by default: it scans every sample of the frames it
// reports, which the RT audio thread should not do in normal operation.
//
//     QT_LOGGING_RULES="qk4.audio.tx.debug=true"
Q_DECLARE_LOGGING_CATEGORY(qk4AudioTx)

#endif // AUDIO_AUDIOLOGGING_H
