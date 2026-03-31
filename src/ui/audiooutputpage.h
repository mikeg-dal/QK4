#ifndef AUDIOOUTPUTPAGE_H
#define AUDIOOUTPUTPAGE_H

#include <QWidget>
#include <QComboBox>

class AudioEngine;

class AudioOutputPage : public QWidget {
    Q_OBJECT

public:
    explicit AudioOutputPage(AudioEngine *audioEngine, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onSpeakerDeviceChanged(int index);

private:
    void populateSpeakerDevices();

    AudioEngine *m_audioEngine;
    QComboBox *m_speakerDeviceCombo = nullptr;
};

#endif // AUDIOOUTPUTPAGE_H
