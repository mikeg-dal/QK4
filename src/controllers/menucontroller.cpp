#include "menucontroller.h"

#include "connectioncontroller.h"
#include "models/menumodel.h"
#include "spectrumcontroller.h"
#include "ui/overlays/menuoverlay.h"

#include <QLoggingCategory>
#include <QPoint>
#include <QWidget>

Q_LOGGING_CATEGORY(qk4Menu, "qk4.menu")

MenuController::MenuController(ConnectionController *connection, SpectrumController *spectrum, QWidget *parentWidget,
                               QObject *parent)
    : QObject(parent), m_connection(connection), m_spectrum(spectrum), m_parentWidget(parentWidget),
      m_menuModel(new MenuModel(this)), m_menuOverlay(new MenuOverlayWidget(m_menuModel, parentWidget)) {
    connect(m_menuModel, &MenuModel::menuValueChanged, this, &MenuController::menuValueChanged);

    m_menuOverlay->hide();

    connect(m_menuOverlay, &MenuOverlayWidget::menuValueChangeRequested, this,
            &MenuController::onMenuValueChangeRequested);
    connect(m_menuOverlay, &MenuOverlayWidget::closed, this, &MenuController::overlayClosed);

    connect(m_menuModel, &MenuModel::menuValueChanged, this, &MenuController::onMenuModelValueChanged);
    connect(m_menuModel, &MenuModel::menuItemAdded, this, &MenuController::onMenuItemAdded);
}

MenuController::~MenuController() {
    // Architecture Rule 11 — disconnect first to prevent queued signals
    // from arriving during partial destruction.
    disconnect(this);
}

void MenuController::ingestMedf(const QString &cmd) {
    if (m_menuModel)
        m_menuModel->parseMEDF(cmd);
}

void MenuController::ingestMe(const QString &cmd) {
    if (m_menuModel)
        m_menuModel->parseME(cmd);
}

void MenuController::clearModel() {
    if (m_menuModel)
        m_menuModel->clear();
}

bool MenuController::isOverlayVisible() const {
    return m_menuOverlay && m_menuOverlay->isVisible();
}

void MenuController::toggleOverlay() {
    if (!m_menuOverlay || !m_spectrum->spectrumContainer())
        return;

    if (m_menuOverlay->isVisible()) {
        m_menuOverlay->hide();
    } else {
        repositionOverlay();
        m_menuOverlay->show();
        m_menuOverlay->raise();
    }
}

void MenuController::closeOverlay() {
    if (m_menuOverlay && m_menuOverlay->isVisible())
        m_menuOverlay->hide();
}

void MenuController::repositionOverlay() {
    if (!m_menuOverlay || !m_spectrum->spectrumContainer() || !m_parentWidget)
        return;
    const QPoint pos = m_spectrum->spectrumContainer()->mapTo(m_parentWidget, QPoint(0, 0));
    m_menuOverlay->setGeometry(pos.x(), pos.y(), m_spectrum->spectrumContainer()->width(),
                               m_spectrum->spectrumContainer()->height());
}

void MenuController::addSyntheticDisplayFpsItem(int initialFps) {
    m_menuModel->addSyntheticDisplayFpsItem(initialFps);
}

void MenuController::setDisplayFps(int fps) {
    m_menuModel->updateValue(MenuModel::SYNTHETIC_DISPLAY_FPS_ID, fps);
}

void MenuController::onMenuValueChangeRequested(int menuId, const QString &action) {
    // Synthetic Display FPS — doesn't use ME command, uses #FPS.
    if (menuId == MenuModel::SYNTHETIC_DISPLAY_FPS_ID) {
        MenuItem *item = m_menuModel->getMenuItem(menuId);
        if (!item)
            return;

        int newValue = item->currentValue;
        if (action == "+") {
            newValue = qMin(item->currentValue + 1, 30);
        } else if (action == "-") {
            newValue = qMax(item->currentValue - 1, 12);
        }

        m_menuModel->updateValue(menuId, newValue);

        if (m_connection->isConnected()) {
            qCDebug(qk4Menu) << "Display FPS change:" << QString("#FPS%1;").arg(newValue);
            m_connection->sendCAT(QString("#FPS%1;").arg(newValue));
        }

        m_connection->setDisplayFps(newValue);
        return;
    }

    // Real K4 menu items — ME command.
    // action: "+" = increment, "-" = decrement, "/" = toggle
    const QString cmd = QString("ME%1.%2;").arg(menuId, 4, 10, QChar('0')).arg(action);
    qCDebug(qk4Menu) << "Menu value change:" << cmd;

    // Optimistic local update.
    MenuItem *item = m_menuModel->getMenuItem(menuId);
    if (item) {
        int newValue = item->currentValue;
        if (action == "+") {
            newValue = qMin(item->currentValue + item->step, item->maxValue);
        } else if (action == "-") {
            newValue = qMax(item->currentValue - item->step, item->minValue);
        } else if (action == "/") {
            // Toggle binary
            newValue = (item->currentValue == 0) ? 1 : 0;
        }
        m_menuModel->updateValue(menuId, newValue);
    }

    if (m_connection->isConnected())
        m_connection->sendCAT(cmd);
}

void MenuController::onMenuModelValueChanged(int menuId, int newValue) {
    const MenuItem *item = m_menuModel->getMenuItem(menuId);

    // Spectrum Amplitude Units (dBm vs S-UNITS).
    if (item && item->name == "Spectrum Amplitude Units") {
        const bool useSUnits = (newValue == 1);
        qCDebug(qk4Menu) << "Spectrum amplitude units changed:" << (useSUnits ? "S-UNITS" : "dBm");
        m_spectrum->setAmplitudeUnits(useSUnits);
    }

    // "Mouse L/R Button QSY" — track the ID and forward changes to spectrum.
    if (menuId == m_mouseQsyMenuId) {
        m_mouseQsyMode = newValue;
        m_spectrum->setMouseQsyMode(newValue);
        qCDebug(qk4Menu) << "Mouse L/R Button QSY changed to:" << m_mouseQsyMode;
    }

    // "FSK Mark-Tone" — forward the tone value to panadapters.
    if (menuId == m_fskMarkToneMenuId) {
        const auto *mi = m_menuModel->getMenuItem(menuId);
        if (mi && newValue >= 0 && newValue < mi->options.size()) {
            const int toneHz = mi->options[newValue].toInt();
            qCDebug(qk4Menu) << "FSK Mark-Tone changed to:" << toneHz << "Hz";
            m_spectrum->setFskMarkTone(toneHz);
        }
    }
}

void MenuController::onMenuItemAdded(int menuId) {
    const MenuItem *item = m_menuModel->getMenuItem(menuId);
    if (!item)
        return;

    if (item->name == "Spectrum Amplitude Units") {
        const bool useSUnits = (item->currentValue == 1);
        qCDebug(qk4Menu) << "Initial spectrum amplitude units:" << (useSUnits ? "S-UNITS" : "dBm");
        m_spectrum->setAmplitudeUnits(useSUnits);
    }
    if (item->name == "Mouse L/R Button QSY") {
        m_mouseQsyMenuId = item->id;
        m_mouseQsyMode = item->currentValue;
        m_spectrum->setMouseQsyMode(m_mouseQsyMode);
        qCDebug(qk4Menu) << "Mouse L/R Button QSY: menuId=" << m_mouseQsyMenuId << "mode=" << m_mouseQsyMode;
    }
    if (item->name == "FSK Mark-Tone") {
        m_fskMarkToneMenuId = item->id;
        const int toneHz = item->options[item->currentValue].toInt();
        qCDebug(qk4Menu) << "FSK Mark-Tone: menuId=" << m_fskMarkToneMenuId << "tone=" << toneHz << "Hz";
        m_spectrum->setFskMarkTone(toneHz);
    }
}

bool MenuController::menuValue(int menuId, int *valueOut) const {
    const MenuItem *item = m_menuModel ? m_menuModel->getMenuItem(menuId) : nullptr;
    if (!item) {
        return false;
    }
    if (valueOut) {
        *valueOut = item->currentValue;
    }
    return true;
}
