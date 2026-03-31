#ifndef AUDIOINPUTPAGE_H
#define AUDIOINPUTPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>

class AudioEngine;
class MicMeterWidget;

class AudioInputPage : public QWidget {
    Q_OBJECT

public:
    explicit AudioInputPage(AudioEngine *audioEngine, QWidget *parent = nullptr);
    ~AudioInputPage();

    void refresh();

protected:
    void hideEvent(QHideEvent *event) override;

private slots:
    void onMicDeviceChanged(int index);
    void onMicGainChanged(int value);
    void onMicTestToggled(bool checked);
    void onMicLevelChanged(float level);

private:
    void populateMicDevices();

    AudioEngine *m_audioEngine;
    QComboBox *m_micDeviceCombo = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QLabel *m_micGainValueLabel = nullptr;
    QPushButton *m_micTestBtn = nullptr;
    MicMeterWidget *m_micMeter = nullptr;
    bool m_micTestActive = false;
};

#endif // AUDIOINPUTPAGE_H
