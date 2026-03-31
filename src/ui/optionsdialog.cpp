#include "optionsdialog.h"
#include "aboutpage.h"
#include "audioinputpage.h"
#include "audiooutputpage.h"
#include "rigcontrolpage.h"
#include "cwkeyerpage.h"
#include "kpodpage.h"
#include "kpa1500page.h"
#include "k4styles.h"
#include "controllers/audiocontroller.h"
#include "controllers/hardwarecontroller.h"
#include "audio/audioengine.h"
#include "hardware/kpoddevice.h"
#include "hardware/halikeydevice.h"
#include <QHBoxLayout>

OptionsDialog::OptionsDialog(RadioState *radioState, AudioController *audioController,
                             HardwareController *hardwareController, CatServer *catServer, KPA1500Client *kpa1500Client,
                             QWidget *parent)
    : QDialog(parent), m_radioState(radioState), m_audioController(audioController),
      m_hardwareController(hardwareController), m_catServer(catServer), m_kpa1500Client(kpa1500Client) {
    setWindowModality(Qt::ApplicationModal);
    setupUi();
}

OptionsDialog::~OptionsDialog() = default;

void OptionsDialog::setupUi() {
    setWindowTitle("Options");
    setMinimumSize(700, 550);
    resize(800, 650);

    // Dark theme styling
    setStyleSheet(QString("QDialog { background-color: %1; }"
                          "QLabel { color: %2; }"
                          "QListWidget { background-color: %3; color: %2; border: 1px solid %4; "
                          "             font-size: %6px; outline: none; }"
                          "QListWidget::item { padding: 10px 15px; border-bottom: 1px solid %4; }"
                          "QListWidget::item:selected { background-color: %5; color: %3; }"
                          "QListWidget::item:hover { background-color: %7; }"
                          "QListWidget::item:selected:hover { background-color: %5; color: %3; }")
                      .arg(K4Styles::Colors::Background, K4Styles::Colors::TextWhite, K4Styles::Colors::DarkBackground,
                           K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber)
                      .arg(K4Styles::Dimensions::FontSizePopup)
                      .arg(K4Styles::Colors::GradientBottom));

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left side: vertical tab list
    m_tabList = new QListWidget(this);
    m_tabList->setFixedWidth(K4Styles::Dimensions::TabListWidth);
    m_tabList->addItem("About");
    m_tabList->addItem("Audio Input");
    m_tabList->addItem("Audio Output");
    m_tabList->addItem("Rig Control");
    m_tabList->addItem("CW Keyer");
    m_tabList->addItem("K-Pod");
    m_tabList->addItem("KPA1500");
    m_tabList->setCurrentRow(0);

    // Right side: stacked pages -- lazy creation (only About page created eagerly)
    m_pageStack = new QStackedWidget(this);
    m_aboutPage = new AboutPage(m_radioState, this);
    m_pageStack->addWidget(m_aboutPage);
    m_pageCreated[PageAbout] = true;
    for (int i = 1; i < PageCount; ++i)
        m_pageStack->addWidget(new QWidget(this)); // placeholders

    // Connect tab selection to page switching
    connect(m_tabList, &QListWidget::currentRowChanged, this, [this](int index) {
        ensurePageCreated(index);
        m_pageStack->setCurrentIndex(index);
        refreshPage(index);
    });

    // Push notifications for audio device changes (hot-plug/unplug)
    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, [this]() {
        if (m_audioInputPage)
            m_audioInputPage->refresh();
    });
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, [this]() {
        if (m_audioOutputPage)
            m_audioOutputPage->refresh();
    });

    mainLayout->addWidget(m_tabList);
    mainLayout->addWidget(m_pageStack, 1);
}

void OptionsDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    // Ensure current page is created (for first show)
    int current = m_tabList->currentRow();
    ensurePageCreated(current);
    refreshPage(current);
}

void OptionsDialog::hideEvent(QHideEvent *event) {
    QDialog::hideEvent(event);
}

void OptionsDialog::ensurePageCreated(int index) {
    if (index < 0 || index >= PageCount || m_pageCreated[index])
        return;

    QWidget *page = nullptr;
    switch (index) {
    case PageAudioInput:
        m_audioInputPage = new AudioInputPage(m_audioController->audioEngine(), this);
        page = m_audioInputPage;
        break;
    case PageAudioOutput:
        m_audioOutputPage = new AudioOutputPage(m_audioController->audioEngine(), this);
        page = m_audioOutputPage;
        break;
    case PageRigControl:
        m_rigControlPage = new RigControlPage(m_catServer, this);
        page = m_rigControlPage;
        break;
    case PageCwKeyer:
        m_cwKeyerPage = new CwKeyerPage(m_hardwareController->halikeyDevice(), this);
        page = m_cwKeyerPage;
        break;
    case PageKpod:
        m_kpodPage = new KpodPage(m_hardwareController->kpodDevice(), this);
        page = m_kpodPage;
        break;
    case PageKpa1500:
        m_kpa1500Page = new Kpa1500Page(m_kpa1500Client, this);
        page = m_kpa1500Page;
        break;
    default:
        return;
    }

    QWidget *placeholder = m_pageStack->widget(index);
    m_pageStack->removeWidget(placeholder);
    delete placeholder;
    m_pageStack->insertWidget(index, page);
    m_pageCreated[index] = true;
}

void OptionsDialog::refreshPage(int index) {
    if (index < 0 || index >= PageCount || !m_pageCreated[index])
        return;

    switch (index) {
    case PageAbout:
        if (m_aboutPage)
            m_aboutPage->refresh();
        break;
    case PageAudioInput:
        // No refresh on tab switch -- device list populated at creation, hot-plug handles updates
        break;
    case PageAudioOutput:
        // No refresh on tab switch -- device list populated at creation, hot-plug handles updates
        break;
    case PageRigControl:
        if (m_rigControlPage)
            m_rigControlPage->refresh();
        break;
    case PageCwKeyer:
        if (m_cwKeyerPage)
            m_cwKeyerPage->refresh();
        break;
    case PageKpod:
        if (m_kpodPage)
            m_kpodPage->refresh();
        break;
    case PageKpa1500:
        if (m_kpa1500Page)
            m_kpa1500Page->refresh();
        break;
    default:
        break;
    }
}
