#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QShowEvent>
#include <QMediaDevices>

class RadioState;
class AudioController;
class HardwareController;
class CatServer;
class TciController;
class KPA1500Client;
class DxClusterController;
class CwController;
class AboutPage;
class StationPage;
class DisplayPage;
class AudioInputPage;
class AudioOutputPage;
class RigControlPage;
class TciServerPage;
class CwKeyerPage;
class KpodPage;
class Kpa1500Page;
class DxClusterPage;
class DiagnosticsPage;

/**
 * @brief Tabbed Options dialog. Left QListWidget drives a QStackedWidget holding the option
 *        pages (About, Station, Display, Audio Input/Output, Rig Control, TCI Server, CW Keyer,
 *        KPOD, KPA1500, DX Cluster, Diagnostics).
 *        Pages are lazy-constructed on first tab activation via @c ensurePageCreated.
 */
class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    enum Page {
        PageAbout = 0,
        PageStation,
        PageDisplay,
        PageAudioInput,
        PageAudioOutput,
        PageRigControl,
        PageTciServer,
        PageCwKeyer,
        PageKpod,
        PageKpa1500,
        PageDxCluster,
        PageDiagnostics,
        PageCount
    };

    explicit OptionsDialog(RadioState *radioState, AudioController *audioController,
                           HardwareController *hardwareController, CatServer *catServer, TciController *tciController,
                           KPA1500Client *kpa1500Client, DxClusterController *dxClusterController,
                           CwController *cwController, QWidget *parent = nullptr);
    ~OptionsDialog();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void setupUi();
    void ensurePageCreated(int index);
    void refreshPage(int index);

    RadioState *m_radioState;
    AudioController *m_audioController;
    HardwareController *m_hardwareController;
    TciController *m_tciController = nullptr;
    CatServer *m_catServer;
    KPA1500Client *m_kpa1500Client;
    DxClusterController *m_dxClusterController;
    CwController *m_cwController;
    QListWidget *m_tabList;
    QStackedWidget *m_pageStack;
    QMediaDevices *m_mediaDevices;
    bool m_pageCreated[PageCount] = {};

    // Page widgets
    AboutPage *m_aboutPage = nullptr;
    StationPage *m_stationPage = nullptr;
    DisplayPage *m_displayPage = nullptr;
    AudioInputPage *m_audioInputPage = nullptr;
    AudioOutputPage *m_audioOutputPage = nullptr;
    TciServerPage *m_tciServerPage = nullptr;
    RigControlPage *m_rigControlPage = nullptr;
    CwKeyerPage *m_cwKeyerPage = nullptr;
    KpodPage *m_kpodPage = nullptr;
    Kpa1500Page *m_kpa1500Page = nullptr;
    DxClusterPage *m_dxClusterPage = nullptr;
    DiagnosticsPage *m_diagnosticsPage = nullptr;
};

#endif // OPTIONSDIALOG_H
