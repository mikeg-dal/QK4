#ifndef DATAMODEPAGE_H
#define DATAMODEPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSlider>

class DataModePage : public QWidget {
    Q_OBJECT

public:
    explicit DataModePage(QWidget *parent = nullptr);
    ~DataModePage() = default;

    void refresh();

private slots:
    void onEnableToggled(bool checked);
    void onMicDeviceChanged(int index);
    void onMicGainChanged(int value);
    void onSpeakerDeviceChanged(int index);

private:
    void populateMicDevices();
    void populateSpeakerDevices();
    void updateControlsEnabled(bool enabled);

    QCheckBox *m_enableCheckbox = nullptr;
    QComboBox *m_micDeviceCombo = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QLabel *m_micGainValueLabel = nullptr;
    QLabel *m_micDeviceLabel = nullptr;
    QLabel *m_micGainLabel = nullptr;
    QComboBox *m_speakerDeviceCombo = nullptr;
    QLabel *m_speakerDeviceLabel = nullptr;
};

#endif // DATAMODEPAGE_H
