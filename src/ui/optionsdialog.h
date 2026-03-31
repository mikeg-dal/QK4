#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QShowEvent>
#include <QHideEvent>
#include <QMediaDevices>

class RadioState;
class AudioController;
class HardwareController;
class CatServer;
class KPA1500Client;
class AboutPage;
class AudioInputPage;
class AudioOutputPage;
class RigControlPage;
class CwKeyerPage;
class KpodPage;
class Kpa1500Page;

class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    enum Page {
        PageAbout = 0,
        PageAudioInput,
        PageAudioOutput,
        PageRigControl,
        PageCwKeyer,
        PageKpod,
        PageKpa1500,
        PageCount
    };

    explicit OptionsDialog(RadioState *radioState, AudioController *audioController,
                           HardwareController *hardwareController, CatServer *catServer, KPA1500Client *kpa1500Client,
                           QWidget *parent = nullptr);
    ~OptionsDialog();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setupUi();
    void ensurePageCreated(int index);
    void refreshPage(int index);

    RadioState *m_radioState;
    AudioController *m_audioController;
    HardwareController *m_hardwareController;
    CatServer *m_catServer;
    KPA1500Client *m_kpa1500Client;
    QListWidget *m_tabList;
    QStackedWidget *m_pageStack;
    QMediaDevices *m_mediaDevices;
    bool m_pageCreated[PageCount] = {};

    // Page widgets
    AboutPage *m_aboutPage = nullptr;
    AudioInputPage *m_audioInputPage = nullptr;
    AudioOutputPage *m_audioOutputPage = nullptr;
    RigControlPage *m_rigControlPage = nullptr;
    CwKeyerPage *m_cwKeyerPage = nullptr;
    KpodPage *m_kpodPage = nullptr;
    Kpa1500Page *m_kpa1500Page = nullptr;
};

#endif // OPTIONSDIALOG_H
