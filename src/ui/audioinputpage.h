#ifndef AUDIOINPUTPAGE_H
#define AUDIOINPUTPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>

class AudioEngine;

class AudioInputPage : public QWidget {
    Q_OBJECT

public:
    explicit AudioInputPage(AudioEngine *audioEngine, QWidget *parent = nullptr);
    ~AudioInputPage() = default;

    void refresh();

private slots:
    void onMicDeviceChanged(int index);
    void onMicGainChanged(int value);

private:
    void populateMicDevices();

    AudioEngine *m_audioEngine;
    QComboBox *m_micDeviceCombo = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QLabel *m_micGainValueLabel = nullptr;
};

#endif // AUDIOINPUTPAGE_H
