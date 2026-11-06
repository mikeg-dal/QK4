#include "menucontroller.h"
#include "logging/logcategories.h"

#include "connectioncontroller.h"
#include "models/menumodel.h"
#include "models/radiostate.h"
#include "network/catframes.h"
#include "spectrumcontroller.h"
#include "ui/overlays/menuoverlay.h"

#include <QLoggingCategory>
#include <QPoint>
#include <QWidget>

#include <algorithm>

MenuController::MenuController(ConnectionController *connection, RadioState *radioState, SpectrumController *spectrum,
                               QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_connection(connection), m_radioState(radioState), m_spectrum(spectrum),
      m_parentWidget(parentWidget), m_menuModel(new MenuModel(this)),
      m_menuOverlay(new MenuOverlayWidget(m_menuModel, parentWidget)) {
    connect(m_menuModel, &MenuModel::menuValueChanged, this, &MenuController::menuValueChanged);

    m_menuOverlay->hide();

    connect(m_menuOverlay, &MenuOverlayWidget::menuValueChangeRequested, this,
            &MenuController::onMenuValueChangeRequested);
    connect(m_menuOverlay, &MenuOverlayWidget::menuValueSetRequested, this, &MenuController::onMenuValueSetRequested);
    connect(m_menuOverlay, &MenuOverlayWidget::closed, this, &MenuController::overlayClosed);

    connect(m_menuModel, &MenuModel::menuValueChanged, this, &MenuController::onMenuModelValueChanged);
    connect(m_menuModel, &MenuModel::menuItemAdded, this, &MenuController::onMenuItemAdded);
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
        const MenuItem *item = m_menuModel->getMenuItem(menuId);
        if (!item)
            return;

        int newValue = item->currentValue;
        if (action == "+") {
            newValue = (std::min)(item->currentValue + 1, item->maxValue);
        } else if (action == "-") {
            newValue = (std::max)(item->currentValue - 1, item->minValue);
        }
        if (newValue == item->currentValue)
            return;

        m_menuModel->updateValue(menuId, newValue);

        if (m_connection->isConnected()) {
            // "#FPSnn;" (Programmer's Reference D12). The radio may grant less; the read-back puts
            // what it granted in the menu through RadioState::displayFpsChanged.
            const QString cmd = QString("#FPS%1;").arg(newValue, 2, 10, QChar('0'));
            qCDebug(lcUiMenu) << "Display FPS change:" << cmd;
            m_connection->sendCAT(cmd + "#FPS;");
        }

        m_connection->setDisplayFps(newValue);
        return;
    }

    // Real K4 menu items — ME command.
    // action: "+" = increment, "-" = decrement, "/" = toggle
    const QString cmd = QString("ME%1.%2;").arg(menuId, 4, 10, QChar('0')).arg(action);
    qCDebug(lcUiMenu) << "Menu value change:" << cmd;

    // Optimistic local update.
    const MenuItem *item = m_menuModel->getMenuItem(menuId);
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

void MenuController::onMenuValueSetRequested(int menuId, int value) {
    // XVTR R.F., I.F. and Offset go as the transverter setup commands, which D12 documents at their
    // full width (R.F. 0-99999 MHz); ME's value is documented as four digits. The rest go as ME.
    QByteArray frame;
    const MenuItem *select = m_menuModel->getMenuItem(MenuModel::XVTR_BAND_SELECT_ID);
    const int band = select ? select->currentValue : 0;
    if (band >= 1 && band <= 12) {
        if (menuId == MenuModel::XVTR_RF_ID)
            frame = CatFrames::setXvtrRfMhz(band, value);
        else if (menuId == MenuModel::XVTR_IF_ID)
            frame = CatFrames::setXvtrIfMhz(band, value);
        else if (menuId == MenuModel::XVTR_OFFSET_ID)
            frame = CatFrames::setXvtrOffsetHz(band, value);
    }
    if (frame.isEmpty())
        frame = CatFrames::setMenuValue(menuId, value);
    qCDebug(lcUiMenu) << "Menu value set:" << frame;
    // The radio does not report back a change QK4 made (AI4): show it now.
    m_menuModel->updateValue(menuId, value);
    if (m_connection->isConnected())
        m_connection->sendCAT(QString::fromLatin1(frame));
}

void MenuController::mirrorXvtrItem(int menuId, int value) {
    if (!m_radioState)
        return;
    const MenuItem *select = m_menuModel->getMenuItem(MenuModel::XVTR_BAND_SELECT_ID);
    const int band = select ? select->currentValue : 0;
    if (band < 1 || band > 12)
        return;
    QByteArray frames;
    if (menuId == MenuModel::XVTR_MODE_ID)
        frames = CatFrames::setXvtrMode(band, value);
    else if (menuId == MenuModel::XVTR_RF_ID)
        frames = CatFrames::setXvtrRfMhz(band, value);
    else if (menuId == MenuModel::XVTR_IF_ID)
        frames = CatFrames::setXvtrIfMhz(band, value);
    else if (menuId == MenuModel::XVTR_OFFSET_ID)
        frames = CatFrames::setXvtrOffsetHz(band, value);
    else if (menuId == MenuModel::XVTR_POWER_ID) // D10mW: the same 0.1 mW units as XVP
        frames = CatFrames::setXvtrPowerTenthsMw(band, value);
    const QString commands = QString::fromLatin1(frames);
    for (const QString &command : commands.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        m_radioState->parseCATCommand(command + QLatin1Char(';'));
}

void MenuController::onMenuModelValueChanged(int menuId, int newValue) {
    const MenuItem *item = m_menuModel->getMenuItem(menuId);

    mirrorXvtrItem(menuId, newValue);

    // Spectrum Amplitude Units (dBm vs S-UNITS).
    if (item && item->name == "Spectrum Amplitude Units") {
        const bool useSUnits = (newValue == 1);
        qCDebug(lcUiMenu) << "Spectrum amplitude units changed:" << (useSUnits ? "S-UNITS" : "dBm");
        m_spectrum->setAmplitudeUnits(useSUnits);
    }

    // "Mouse L/R Button QSY" — track the ID and forward changes to spectrum.
    if (menuId == m_mouseQsyMenuId) {
        m_mouseQsyMode = newValue;
        m_spectrum->setMouseQsyMode(newValue);
        qCDebug(lcUiMenu) << "Mouse L/R Button QSY changed to:" << m_mouseQsyMode;
    }

    // "FSK Mark-Tone" — forward the tone value to panadapters.
    if (menuId == m_fskMarkToneMenuId) {
        const auto *mi = m_menuModel->getMenuItem(menuId);
        if (mi && newValue >= 0 && newValue < mi->options.size()) {
            const int toneHz = mi->options[newValue].toInt();
            qCDebug(lcUiMenu) << "FSK Mark-Tone changed to:" << toneHz << "Hz";
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
        qCDebug(lcUiMenu) << "Initial spectrum amplitude units:" << (useSUnits ? "S-UNITS" : "dBm");
        m_spectrum->setAmplitudeUnits(useSUnits);
    }
    if (item->name == "Mouse L/R Button QSY") {
        m_mouseQsyMenuId = item->id;
        m_mouseQsyMode = item->currentValue;
        m_spectrum->setMouseQsyMode(m_mouseQsyMode);
        qCDebug(lcUiMenu) << "Mouse L/R Button QSY: menuId=" << m_mouseQsyMenuId << "mode=" << m_mouseQsyMode;
    }
    if (item->name == "FSK Mark-Tone") {
        m_fskMarkToneMenuId = item->id;
        if (item->currentValue >= 0 && item->currentValue < item->options.size()) {
            const int toneHz = item->options[item->currentValue].toInt();
            qCDebug(lcUiMenu) << "FSK Mark-Tone: menuId=" << m_fskMarkToneMenuId << "tone=" << toneHz << "Hz";
            m_spectrum->setFskMarkTone(toneHz);
        }
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
