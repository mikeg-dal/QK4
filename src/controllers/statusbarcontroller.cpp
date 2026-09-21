#include "statusbarcontroller.h"

#include "controllers/connectioncontroller.h"
#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "settings/radiosettings.h"
#include "ui/popups/confirmpopup.h"
#include "ui/styling/k4glyphs.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/icontextlabel.h"
#include "ui/widgets/nethealthwidget.h"
#include "ui/widgets/powerstatusbutton.h"
#include "utils/radioutils.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace {
// 1 Hz clock tick. Sub-second drift on the status-bar clock is imperceptible;
// saves 50× timer overhead vs 50 ms updates.
constexpr int kClockUpdateIntervalMs = 1000;

// Generic "safe-for-ham-radio" PA/LPA thresholds — we don't have the K4's
// published spec for these stages, so these are conservative working bounds.
constexpr int kWarnTempC = 60;
constexpr int kCritTempC = 75;

// Low-voltage thresholds for a nominal 13.8 V K4 supply.
constexpr double kVoltsWarn = 12.0;
constexpr double kVoltsCrit = 11.0;

// Field colors split into (glyph, value) so the glyph can carry its own
// semantic baseline color in the normal range while still re-tinting with
// the value on warning / critical.
struct ColorPair {
    QColor glyph;
    QColor value;
};

ColorPair temperatureColors(int celsius) {
    const QColor red(K4Styles::Colors::TxRed);
    const QColor orange(K4Styles::Colors::MeterOrange);
    const QColor amber(K4Styles::Colors::AccentAmber);
    if (celsius >= kCritTempC)
        return {red, red};
    if (celsius >= kWarnTempC)
        return {orange, orange};
    // Normal: semantic warm-orange glyph (thermometer "reads hot") + amber value.
    return {orange, amber};
}

ColorPair voltageColors(double volts) {
    const QColor red(K4Styles::Colors::TxRed);
    const QColor orange(K4Styles::Colors::MeterOrange);
    const QColor amber(K4Styles::Colors::AccentAmber);
    const QColor gold(K4Styles::Colors::FilterIndicatorGold);
    if (volts < kVoltsCrit)
        return {red, red};
    if (volts < kVoltsWarn)
        return {orange, orange};
    // Normal: semantic gold bolt ("electric") + amber value.
    return {gold, amber};
}
} // namespace

StatusBarController::StatusBarController(RadioState *radioState, ConnectionController *connectionController,
                                         NetworkMetrics *networkMetrics, QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connectionController(connectionController),
      m_container(new QWidget(parentWidget)), m_clockTimer(new QTimer(this)) {
    m_container->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_container->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DarkBackground));

    auto *layout = new QHBoxLayout(m_container);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(20);

    // Elecraft K4 title
    m_titleLabel = new QLabel("Elecraft K4", m_container);
    m_titleLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: %2px;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(m_titleLabel);

    // Date/Time
    m_dateTimeLabel = new QLabel("--/-- --:--:-- Z", m_container);
    m_dateTimeLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                       .arg(K4Styles::Colors::TextGray)
                                       .arg(K4Styles::Dimensions::FontSizeButton));
    layout->addWidget(m_dateTimeLabel);

    layout->addStretch();

    // Lower-PA heatsink temperature (SIRF LT, °C).
    m_lpaTempField = new IconTextLabel(m_container);
    m_lpaTempField->setGlyph(K4Glyphs::thermometer());
    m_lpaTempField->setGlyphColor(QColor(K4Styles::Colors::MeterOrange));
    m_lpaTempField->setLabel("LPA");
    m_lpaTempField->setClickable(true);
    m_lpaTempField->setToolTip("Click to switch between °C and °F");
    layout->addWidget(m_lpaTempField);
    connect(m_lpaTempField, &IconTextLabel::clicked, this, &StatusBarController::toggleTemperatureUnit);

    // PA heatsink temperature (SIRF PT, °C).
    m_paTempField = new IconTextLabel(m_container);
    m_paTempField->setGlyph(K4Glyphs::thermometer());
    m_paTempField->setGlyphColor(QColor(K4Styles::Colors::MeterOrange));
    m_paTempField->setLabel("PA");
    m_paTempField->setClickable(true);
    m_paTempField->setToolTip("Click to switch between °C and °F");
    layout->addWidget(m_paTempField);
    connect(m_paTempField, &IconTextLabel::clicked, this, &StatusBarController::toggleTemperatureUnit);

    // Restore the operator's unit choice. Both fields are empty at this point, so this only sets
    // the unit labels - which is what makes a just-launched, not-yet-connected bar read "LPA -- °F"
    // rather than defaulting to °C until the first reading arrives.
    m_tempInFahrenheit = RadioSettings::instance()->temperatureInFahrenheit();
    refreshTemperatureFields();

    // Supply voltage.
    m_voltageField = new IconTextLabel(m_container);
    m_voltageField->setGlyph(K4Glyphs::lightning());
    m_voltageField->setGlyphColor(QColor(K4Styles::Colors::FilterIndicatorGold));
    m_voltageField->setUnit("V");
    layout->addWidget(m_voltageField);

    layout->addStretch();

    // KPA1500 status (to left of K4 status)
    m_kpa1500StatusLabel = new QLabel("", m_container);
    m_kpa1500StatusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                            .arg(K4Styles::Colors::InactiveGray)
                                            .arg(K4Styles::Dimensions::FontSizeButton));
    m_kpa1500StatusLabel->hide(); // Hidden when not enabled
    layout->addWidget(m_kpa1500StatusLabel);

    // K4 connection status
    m_connectionStatusLabel = new QLabel("K4", m_container);
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                               .arg(K4Styles::Colors::InactiveGray)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
    layout->addWidget(m_connectionStatusLabel);

    // Network health signal bars
    m_netHealthWidget = new NetHealthWidget(networkMetrics, m_container);
    layout->addWidget(m_netHealthWidget);

    // Remote power button — rightmost item.
    m_powerButton = new PowerStatusButton(m_container);
    layout->addWidget(m_powerButton);
    connect(m_powerButton, &QToolButton::clicked, this, &StatusBarController::onPowerButtonClicked);

    // Voltage updates → value field. Glyph stays gold (semantic baseline)
    // in the normal range, but flips with the value to orange/red on sag.
    connect(m_radioState, &RadioState::supplyVoltageChanged, this, [this](double volts) {
        const auto colors = voltageColors(volts);
        m_voltageField->setGlyphColor(colors.glyph);
        m_voltageField->setValueColor(colors.value);
        m_voltageField->setValue(QString("%1").arg(volts, 0, 'f', 1));
    });

    // PA / LPA temperatures from SIRF (PT / LT fields). Thermometer glyph
    // sits in the warm-orange "this is heat" baseline and re-tints to red
    // when the value crosses critical.
    connect(m_radioState, &RadioState::paTemperatureChanged, this, [this](int c) {
        m_paTempC = c;
        applyTemperature(m_paTempField, c);
    });
    connect(m_radioState, &RadioState::lpaTemperatureChanged, this, [this](int c) {
        m_lpaTempC = c;
        applyTemperature(m_lpaTempField, c);
    });

    // PS0/PS1 → power button state.
    connect(m_radioState, &RadioState::powerStateChanged, this, [this](bool on) {
        m_powerButton->setState(on ? PowerStatusButton::State::On : PowerStatusButton::State::Off);
    });

    // On disconnect, blank metric fields and force the power button to "Off"
    // (it will go back to Unknown → On as soon as the next connect's PS query
    // response lands).
    connect(m_connectionController, &ConnectionController::connectionStateChanged, this,
            [this](TcpClient::ConnectionState state) {
                if (state == TcpClient::Disconnected) {
                    clearReadings();
                    m_powerButton->setState(PowerStatusButton::State::Off);
                } else if (state == TcpClient::Connecting || state == TcpClient::Authenticating) {
                    m_powerButton->setState(PowerStatusButton::State::Unknown);
                }
            });

    // Clock tick.
    connect(m_clockTimer, &QTimer::timeout, this, &StatusBarController::updateDateTime);
    m_clockTimer->start(kClockUpdateIntervalMs);
    updateDateTime();
}

StatusBarController::~StatusBarController() {
    // Architecture Rule 11 — disconnect first to prevent queued signals from
    // arriving during partial destruction.
    disconnect(this);
}

QWidget *StatusBarController::widget() const {
    return m_container;
}

void StatusBarController::updateDateTime() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    m_dateTimeLabel->setText(now.toString("M-dd / HH:mm:ss") + " Z");
}

void StatusBarController::setTitle(const QString &text) {
    m_titleLabel->setText(text);
}

void StatusBarController::showDisconnected() {
    // WHY the guard: TcpClient reports a failure as errorOccurred() followed by
    // setState(Disconnected), both queued to this thread in that order. Without this, the
    // Disconnected handler landed a moment after showError() and reset the very label it had just
    // written - so every connection and authentication failure was replaced by a plain grey "K4"
    // before the operator could read it. The error was in the log and nowhere on screen.
    //
    // The error stays until the next attempt starts; showConnecting() is what clears it.
    if (m_errorShown) {
        m_titleLabel->setText("Elecraft K4");
        return;
    }

    m_connectionStatusLabel->setText("K4");
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                               .arg(K4Styles::Colors::InactiveGray)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
    m_titleLabel->setText("Elecraft K4");
}

void StatusBarController::showConnecting() {
    // A new attempt is the one thing that supersedes a previous failure.
    m_errorShown = false;
    m_connectionStatusLabel->setText("K4");
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                               .arg(K4Styles::Colors::AccentAmber)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
}

void StatusBarController::showConnected() {
    m_errorShown = false;
    m_connectionStatusLabel->setText("K4");
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                               .arg(K4Styles::Colors::StatusGreen)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
}

void StatusBarController::showError(const QString &errorMessage) {
    // The status bar carries the STATE, not the story. The whole failure message used to be
    // written into this label in red, which crammed a sentence naming a host and a port into a
    // slot sized for the word "K4" and looked nothing like the themed banner every device
    // notification uses. MainWindow sends the text to the NotificationWidget instead; what stays
    // here is a red "K4", which is the part that should persist after the banner times out.
    Q_UNUSED(errorMessage)
    m_errorShown = true;
    m_connectionStatusLabel->setText("K4");
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                               .arg(K4Styles::Colors::TxRed)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
}

void StatusBarController::clearReadings() {
    m_lpaTempField->clear();
    m_paTempField->clear();
    m_voltageField->clear();
    // Drop the cached readings with the display. Without this, toggling the unit after a
    // disconnect would repaint the last values over the "--" placeholder.
    m_paTempC = kNoTempReading;
    m_lpaTempC = kNoTempReading;
}

void StatusBarController::toggleTemperatureUnit() {
    m_tempInFahrenheit = !m_tempInFahrenheit;
    RadioSettings::instance()->setTemperatureInFahrenheit(m_tempInFahrenheit);
    refreshTemperatureFields();
}

void StatusBarController::applyTemperature(IconTextLabel *field, int celsius) {
    // Colour is decided on the CELSIUS value whichever unit is displayed, so the warn and
    // critical thresholds mean the same heatsink temperature in both.
    const auto colors = temperatureColors(celsius);
    field->setGlyphColor(colors.glyph);
    field->setValueColor(colors.value);
    field->setUnit(m_tempInFahrenheit ? "°F" : "°C");
    field->setValue(QString::number(m_tempInFahrenheit ? RadioUtils::celsiusToFahrenheit(celsius) : celsius));
}

void StatusBarController::refreshTemperatureFields() {
    // The unit label moves even with no reading yet, so a field sitting at "--" still shows
    // which unit it will report in.
    m_paTempField->setUnit(m_tempInFahrenheit ? "°F" : "°C");
    m_lpaTempField->setUnit(m_tempInFahrenheit ? "°F" : "°C");
    if (m_paTempC != kNoTempReading)
        applyTemperature(m_paTempField, m_paTempC);
    if (m_lpaTempC != kNoTempReading)
        applyTemperature(m_lpaTempField, m_lpaTempC);
}

void StatusBarController::setKpa1500Visible(bool visible) {
    m_kpa1500StatusLabel->setVisible(visible);
}

void StatusBarController::setKpa1500Status(const QString &text, const QString &styleSheet) {
    m_kpa1500StatusLabel->setText(text);
    if (!styleSheet.isEmpty())
        m_kpa1500StatusLabel->setStyleSheet(styleSheet);
}

void StatusBarController::onPowerButtonClicked() {
    if (m_confirmPopup && m_confirmPopup->isVisibleOrJustHidden())
        return;

    if (!m_confirmPopup) {
        m_confirmPopup = new ConfirmPopup(
            QStringLiteral("Power off K4?"),
            QStringLiteral("This will power off the remote K4. You will not be able to power it back on from QK4. "
                           "Continue?"),
            QStringLiteral("Power Off"), QStringLiteral("Cancel"), m_container);
        connect(m_confirmPopup, &ConfirmPopup::confirmed, this, [this]() {
            if (m_connectionController)
                m_connectionController->sendCAT(QStringLiteral("PS0;"));
            // No auto-disconnect — K4 closes the socket on its end, and the
            // existing disconnected flow takes over.
        });
    }

    m_confirmPopup->showAboveWidget(m_powerButton);
}
