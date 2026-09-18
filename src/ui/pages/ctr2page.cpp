#include "ctr2page.h"
#include "controllers/hardwarecontroller.h"
#include "hardware/ctr2mididevice.h"
#include "hardware/midimapping.h"
#include "ui/styling/k4styles.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

Ctr2Page::Ctr2Page(HardwareController *controller, QWidget *parent) : QWidget(parent), m_controller(controller) {
    setStyleSheet(K4Styles::Dialog::pageBackground());
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                             K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    root->setSpacing(K4Styles::Dimensions::PaddingLarge);
    auto *title = new QLabel(QStringLiteral("CTR2-MIDI controller"), this);
    title->setStyleSheet(K4Styles::Dialog::titleLabel());
    root->addWidget(title);
    auto *help =
        new QLabel(QStringLiteral("Connects CTR2-MIDI independently from HaliKey. The K4-Control mapping is editable "
                                  "below, including the FT8/FT4 RX/TX tone actions."),
                   this);
    help->setStyleSheet(K4Styles::Dialog::helpText());
    help->setWordWrap(true);
    root->addWidget(help);
    auto *deviceBox = new QGroupBox(QStringLiteral("MIDI connection"), this);
    const QString groupStyle = QString("QGroupBox { color: %1; border: 1px solid %2; border-radius: 4px; "
                                       "            margin-top: 10px; padding: 12px 8px 8px 8px; font-weight: bold; }"
                                       "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }")
                                   .arg(K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder);
    deviceBox->setStyleSheet(groupStyle);
    auto *deviceLayout = new QGridLayout(deviceBox);
    m_device = new QComboBox(deviceBox);
    m_device->setStyleSheet(K4Styles::Dialog::comboBox());
    auto *scan = new QPushButton(QStringLiteral("Refresh"), deviceBox);
    m_connect = new QPushButton(QStringLiteral("Connect"), deviceBox);
    scan->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_connect->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_status = new QLabel(deviceBox);
    m_status->setStyleSheet(K4Styles::Dialog::formValue());
    deviceLayout->addWidget(m_device, 0, 0);
    deviceLayout->addWidget(scan, 0, 1);
    deviceLayout->addWidget(m_connect, 0, 2);
    deviceLayout->addWidget(m_status, 1, 0, 1, 3);
    root->addWidget(deviceBox);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        QString("QScrollArea { background-color: %1; border: none; }").arg(K4Styles::Colors::Background));
    scroll->viewport()->setStyleSheet(K4Styles::Dialog::pageBackground());
    auto *mappingWidget = new QWidget(scroll);
    mappingWidget->setStyleSheet(K4Styles::Dialog::pageBackground());
    auto *mapRoot = new QVBoxLayout(mappingWidget);
    m_extended = new QCheckBox(QStringLiteral("Extended BTN mode (must match CTR2-MIDI setup)"), mappingWidget);
    m_extended->setStyleSheet(K4Styles::Dialog::checkBox());
    mapRoot->addWidget(m_extended);
    auto *knobs = new QGroupBox(QStringLiteral("Knob modes"), mappingWidget);
    knobs->setStyleSheet(groupStyle);
    auto *knobForm = new QFormLayout(knobs);
    for (int cc = 100; cc <= 107; ++cc) {
        auto *combo = new QComboBox(knobs);
        combo->setStyleSheet(K4Styles::Dialog::comboBox());
        for (const QString &action : MidiMapping::supportedKnobActions())
            combo->addItem(MidiMapping::knobActionLabel(action), action);
        knobForm->addRow(QStringLiteral("CC %1").arg(cc), combo);
        m_knobs.insert(cc, combo);
    }
    mapRoot->addWidget(knobs);
    auto *buttons = new QGroupBox(QStringLiteral("Normal button mode"), mappingWidget);
    buttons->setStyleSheet(groupStyle);
    auto *buttonForm = new QFormLayout(buttons);
    for (int note : MidiMapping::ctr2ButtonNotes(false)) {
        auto *combo = new QComboBox(buttons);
        combo->setStyleSheet(K4Styles::Dialog::comboBox());
        for (const QString &action : MidiMapping::supportedButtonActions())
            combo->addItem(MidiMapping::buttonActionLabel(action), action);
        buttonForm->addRow(MidiMapping::ctr2ButtonLabel(false, note), combo);
        m_buttons.insert(note, combo);
    }
    mapRoot->addWidget(buttons);
    auto *applyButton = new QPushButton(QStringLiteral("Apply mapping"), mappingWidget);
    auto *defaults = new QPushButton(QStringLiteral("Restore K4-Control defaults"), mappingWidget);
    applyButton->setStyleSheet(K4Styles::Dialog::actionButton());
    defaults->setStyleSheet(K4Styles::Dialog::actionButton());
    mapRoot->addWidget(applyButton);
    mapRoot->addWidget(defaults);
    mapRoot->addStretch();
    scroll->setWidget(mappingWidget);
    root->addWidget(scroll, 1);
    connect(scan, &QPushButton::clicked, this, &Ctr2Page::refresh);
    connect(m_connect, &QPushButton::clicked, this, [this] {
        if (m_controller->ctr2MidiDevice()->isConnected())
            m_controller->connectCtr2({});
        else
            m_controller->connectCtr2(m_device->currentText());
        refresh();
    });
    connect(applyButton, &QPushButton::clicked, this, &Ctr2Page::apply);
    connect(defaults, &QPushButton::clicked, this, [this] {
        m_controller->setCtr2Mapping(m_extended->isChecked() ? MidiMapping::ctr2ExtendedDefault()
                                                             : MidiMapping::ctr2Default());
        refresh();
    });
    refresh();
}

void Ctr2Page::refresh() {
    auto *device = m_controller->ctr2MidiDevice();
    const QString selected = device->isConnected() ? device->portName() : m_device->currentText();
    m_device->clear();
    m_device->addItems(Ctr2MidiDevice::availableMidiDevices());
    const int selectedIndex = m_device->findText(selected);
    if (selectedIndex >= 0)
        m_device->setCurrentIndex(selectedIndex);
    m_status->setText(device->statusMessage());
    m_connect->setText(device->isConnected() ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
    const auto mapping = m_controller->ctr2Mapping();
    m_extended->setChecked(mapping.extendedButtons);
    for (auto it = m_knobs.begin(); it != m_knobs.end(); ++it) {
        const int index = it.value()->findData(mapping.knobs.value(it.key()).action);
        if (index >= 0)
            it.value()->setCurrentIndex(index);
    }
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        const int index = it.value()->findData(mapping.buttons.value(it.key()).action);
        if (index >= 0)
            it.value()->setCurrentIndex(index);
    }
}

void Ctr2Page::apply() {
    auto mapping = MidiMapping::withCtr2ButtonMode(m_controller->ctr2Mapping(), m_extended->isChecked());
    for (auto it = m_knobs.cbegin(); it != m_knobs.cend(); ++it)
        mapping.knobs[it.key()].action = it.value()->currentData().toString();
    if (!mapping.extendedButtons)
        for (auto it = m_buttons.cbegin(); it != m_buttons.cend(); ++it)
            mapping.buttons[it.key()].action = it.value()->currentData().toString();
    m_controller->setCtr2Mapping(mapping);
    m_status->setText(QStringLiteral("Mapping saved"));
}
