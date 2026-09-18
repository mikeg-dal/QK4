#include "digitalmodewindows.h"

#include "ft8/ft8transmitter.h"
#include "audio/digitaltxguard.h"
#include "sstv/sstvmoderegistry.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSaveFile>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace {
const char *toolStyle = R"(
QMainWindow,QWidget { background:#111820; color:#e6edf3; }
QGroupBox { border:1px solid #34495a; border-radius:6px; margin-top:8px; padding-top:8px; }
QGroupBox::title { color:#69d2ee; subcontrol-origin:margin; left:8px; }
QLineEdit,QComboBox,QTableWidget { background:#172430; color:#e6edf3; border:1px solid #41586b; border-radius:4px; padding:4px; }
QPushButton { background:#243645; color:#e6edf3; border:1px solid #557086; border-radius:5px; padding:6px 10px; }
QPushButton:hover { background:#304a5d; } QPushButton:checked { color:#101820; background:#69d2ee; }
QPushButton#danger { background:#6e2424; border-color:#db6666; font-weight:bold; }
QHeaderView::section { background:#223545; color:#dbe8f1; padding:5px; border:0; }
QTabBar::tab { background:#20313f; color:#dbe8f1; padding:8px 18px; } QTabBar::tab:selected { background:#38627b; }
)";

QString formatFrequency(qint64 hz) {
    if (hz <= 0)
        return QStringLiteral("—");
    return QString::number(double(hz) / 1e6, 'f', 6) + QStringLiteral(" MHz");
}

QString logPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logbook/contacts.json");
}

AdifRecord basicContact(const QString &call, const QString &mode, qint64 frequencyHz,
                        const QString &operatorCall = {}) {
    const auto now = QDateTime::currentDateTimeUtc();
    AdifRecord record{{"CALL", call.trimmed().toUpper()},
                      {"MODE", mode},
                      {"STATION_CALLSIGN", operatorCall.trimmed().toUpper()},
                      {"QSO_DATE", now.toString("yyyyMMdd")},
                      {"TIME_ON", now.toString("HHmmss")}};
    if (frequencyHz > 0) {
        record["FREQ"] = QString::number(double(frequencyHz) / 1e6, 'f', 6);
        record["BAND"] = Ft8::bandFor(frequencyHz);
    }
    return record;
}

} // namespace

class FtxWaterfallWidget final : public QWidget {
public:
    explicit FtxWaterfallWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(70);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_image = QImage(1000, 220, QImage::Format_RGB32);
        m_image.fill(QColor("#07101a"));
    }

    std::function<void(int, bool)> frequencyChosen;
    void setMarkers(int rx, int tx) {
        m_rx = rx;
        m_tx = tx;
        update();
    }
    void addSpectrum(const QVector<float> &values, double firstHz, double binHz) {
        if (values.isEmpty() || !std::isfinite(firstHz) || !std::isfinite(binHz) || binHz <= 0)
            return;
        std::memmove(m_image.scanLine(1), m_image.constScanLine(0),
                     size_t(m_image.bytesPerLine()) * size_t(m_image.height() - 1));
        QVector<float> finite;
        finite.reserve(values.size());
        for (float value : values)
            if (std::isfinite(value))
                finite.append(value);
        if (finite.isEmpty())
            return;
        std::sort(finite.begin(), finite.end());
        const float floor = finite[qRound((finite.size() - 1) * .12)];
        const float ceiling = finite[qRound((finite.size() - 1) * .97)];
        auto *line = reinterpret_cast<QRgb *>(m_image.scanLine(0));
        for (int x = 0; x < m_image.width(); ++x) {
            const double hz = 3300.0 * x / qMax(1, m_image.width() - 1);
            const int bin = qRound((hz - firstHz) / binHz);
            const float value = bin >= 0 && bin < values.size() ? values[bin] : floor;
            const float n = qBound(0.0f, (value - floor) / qMax(3.0f, ceiling - floor), 1.0f);
            // Mobile QK4 palette: quiet bins remain deep blue; useful signals
            // pass through cyan/green, reserving yellow for the strongest peaks.
            const int r = qRound(255 * qMax(0.0f, (n - .78f) / .22f));
            const int g = qRound(255 * qBound(0.0f, (n - .28f) / .52f, 1.0f));
            const int b = qRound(28 + 205 * qBound(0.0f, n / .52f, 1.0f) - 190 * qMax(0.0f, (n - .64f) / .36f));
            line[x] = qRgb(r, g, b);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#07101a"));
        painter.drawImage(rect(), m_image);
        painter.setPen(QColor(255, 255, 255, 90));
        for (int hz = 0; hz <= 3000; hz += 500) {
            const int x = qRound(width() * hz / 3300.0);
            painter.drawLine(x, 0, x, height());
            painter.drawText(x + 3, 14, QString::number(hz));
        }
        for (const auto marker : {qMakePair(m_rx, QColor("#67ef89")), qMakePair(m_tx, QColor("#ff5c5c"))}) {
            const int x = qRound(width() * marker.first / 3300.0);
            painter.setPen(QPen(marker.second, 2));
            painter.drawLine(x, 0, x, height());
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if ((event->button() == Qt::LeftButton || event->button() == Qt::RightButton) && frequencyChosen) {
            frequencyChosen(qBound(100, qRound(3300.0 * event->position().x() / qMax(1, width())), 3200),
                            event->button() == Qt::RightButton);
        }
    }

private:
    QImage m_image;
    int m_rx = 1500;
    int m_tx = 1500;
};

FtxWindow::FtxWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("QK4 — FT8 / FT4"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(980, 760);
    setMinimumSize(720, 560);
    setStyleSheet(QString::fromLatin1(toolStyle));
    QSettings settings;
    m_autoLog = settings.value("ft8/autoLog", true).toBool();
    m_session.rxHz = settings.value("ft8/rxHz", 1500).toInt();
    m_session.txHz = settings.value("ft8/txHz", 1500).toInt();
    m_session.holdTx = settings.value("ft8/holdTx", true).toBool();
    m_session.callFirst = settings.value("ft8/callFirst", false).toBool();
    m_session.useRr73 = settings.value("ft8/rr73", true).toBool();
    m_session.maxRetries = settings.value("ft8/maxRetries", 6).toInt();
    m_session.even = settings.value("ft8/even", true).toBool();
    m_session.autoSequence = settings.value("ft8/autoSequence", true).toBool();
    m_decodeRows = settings.value("ft8/decodeRows", 2).toInt() == 1 ? 1 : 2;
    buildUi();
    m_call->setText(settings.value("digital/myCall").toString());
    m_grid->setText(settings.value("digital/myGrid").toString());
    setTone(false, m_session.rxHz);
    setTone(true, m_session.txHz);
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &FtxWindow::tick);
    m_timer->start();
}

void FtxWindow::buildUi() {
    auto *body = new QWidget(this);
    auto *root = new QVBoxLayout(body);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(7);

    auto *radioRow = new QHBoxLayout;
    m_radio = new QLabel(QStringLiteral("Disconnected"), body);
    m_radio->setStyleSheet("color:#f3b33d;font-weight:bold;");
    m_mode = new QComboBox(body);
    m_mode->addItems({QStringLiteral("FT8"), QStringLiteral("FT4")});
    m_mode->setCurrentIndex(QSettings().value(QStringLiteral("ft8/mode"), 0).toInt() == 1 ? 1 : 0);
    m_session.mode = mode();
    m_mode->setMinimumWidth(82);
    m_mode->view()->setMinimumWidth(82);
    m_band = new QComboBox(body);
    m_band->addItem(QStringLiteral("Working frequency…"), -1);
    for (int i = 0; i < Ft8::bands().size(); ++i)
        m_band->addItem(Ft8::bands()[i].name, i);
    const int savedBand = m_band->findData(QSettings().value(QStringLiteral("ft8/bandIndex"), -1).toInt());
    if (savedBand >= 0)
        m_band->setCurrentIndex(savedBand);
    m_frequency =
        new QLineEdit(QSettings().value(QStringLiteral("ft8/dialMHz"), QStringLiteral("14.074000")).toString(), body);
    m_frequency->setMaximumWidth(130);
    auto *tune = new QPushButton(QStringLiteral("Set frequency"), body);
    auto *rows = new QComboBox(body);
    rows->addItem(QStringLiteral("1-line rows"), 1);
    rows->addItem(QStringLiteral("2-line rows"), 2);
    rows->setCurrentIndex(rows->findData(m_decodeRows));
    auto *options = new QPushButton(QStringLiteral("Options…"), body);
    radioRow->addWidget(m_radio, 1);
    radioRow->addWidget(m_mode);
    radioRow->addWidget(m_band);
    radioRow->addWidget(m_frequency);
    radioRow->addWidget(tune);
    radioRow->addWidget(rows);
    radioRow->addWidget(options);
    root->addLayout(radioRow);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QStringLiteral("ft8/mode"), m_mode->currentIndex());
        halt();
        m_session.clear();
        m_session.mode = mode();
        chooseBand(m_band->currentIndex());
        emit captureChanged();
        refreshSession();
    });
    connect(m_band, &QComboBox::currentIndexChanged, this, [this](int index) {
        QSettings().setValue(QStringLiteral("ft8/bandIndex"), m_band->itemData(index));
        chooseBand(index);
    });
    connect(tune, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const double mhz = m_frequency->text().toDouble(&ok);
        if (ok && mhz > 0.1 && mhz < 10000.0) {
            QSettings().setValue(QStringLiteral("ft8/dialMHz"), QString::number(mhz, 'f', 6));
            emit frequencyRequested(qRound64(mhz * 1e6));
            m_frequency->clearFocus();
        } else {
            QMessageBox::warning(this, QStringLiteral("Set frequency"),
                                 QStringLiteral("Enter a frequency in MHz, for example 14.074000."));
        }
    });
    connect(m_frequency, &QLineEdit::returnPressed, tune, &QPushButton::click);
    connect(rows, &QComboBox::currentIndexChanged, this, [this, rows] {
        setDecodeRows(rows->currentData().toInt());
        QSettings().setValue(QStringLiteral("ft8/decodeRows"), m_decodeRows);
    });
    connect(options, &QPushButton::clicked, this, &FtxWindow::showOptions);

    m_optionsDialog = new QDialog(this);
    m_optionsDialog->setWindowTitle(QStringLiteral("FT8 / FT4 Options"));
    m_optionsDialog->setModal(false);
    m_optionsDialog->setMinimumWidth(470);
    auto *optionsRoot = new QVBoxLayout(m_optionsDialog);
    auto *stationGrid = new QFormLayout;
    m_call = new QLineEdit(m_optionsDialog);
    m_call->setPlaceholderText(QStringLiteral("My callsign"));
    m_grid = new QLineEdit(m_optionsDialog);
    m_grid->setPlaceholderText(QStringLiteral("Grid, e.g. EM10"));
    stationGrid->addRow(QStringLiteral("My callsign"), m_call);
    stationGrid->addRow(QStringLiteral("Grid square"), m_grid);
    m_rxAudio = new QSpinBox(m_optionsDialog);
    m_txAudio = new QSpinBox(m_optionsDialog);
    for (auto *tone : {m_rxAudio, m_txAudio}) {
        tone->setRange(100, 3200);
        tone->setSuffix(QStringLiteral(" Hz"));
    }
    m_rxAudio->setValue(m_session.rxHz);
    m_txAudio->setValue(m_session.txHz);
    stationGrid->addRow(QStringLiteral("RX audio frequency"), m_rxAudio);
    stationGrid->addRow(QStringLiteral("TX audio frequency"), m_txAudio);
    optionsRoot->addLayout(stationGrid);
    auto *calibrate = new QPushButton(QStringLiteral("Calibrate TX audio level…"), m_optionsDialog);
    auto *calibrationHelp =
        new QLabel(QStringLiteral("Runs a protected tone in K4 TEST mode and saves the level that produces ALC 3–5. "
                                  "FT8 and FT4 share the saved calibration."),
                   m_optionsDialog);
    calibrationHelp->setWordWrap(true);
    optionsRoot->addWidget(calibrate);
    optionsRoot->addWidget(calibrationHelp);
    auto *autoLog = new QCheckBox(QStringLiteral("Save completed QSOs automatically"), m_optionsDialog);
    autoLog->setChecked(m_autoLog);
    optionsRoot->addWidget(autoLog);
    auto *holdTx = new QCheckBox(QStringLiteral("Hold TX frequency when selecting a station"), m_optionsDialog);
    auto *callFirst = new QCheckBox(QStringLiteral("CQ: answer the first responder"), m_optionsDialog);
    auto *rr73 = new QCheckBox(QStringLiteral("Finish with RR73"), m_optionsDialog);
    holdTx->setChecked(m_session.holdTx);
    callFirst->setChecked(m_session.callFirst);
    rr73->setChecked(m_session.useRr73);
    auto *retries = new QSpinBox(m_optionsDialog);
    retries->setRange(1, 20);
    retries->setValue(m_session.maxRetries);
    auto *sequence = new QFormLayout;
    sequence->addRow(QStringLiteral("Maximum repeated transmissions"), retries);
    optionsRoot->addLayout(sequence);
    optionsRoot->addWidget(holdTx);
    optionsRoot->addWidget(callFirst);
    optionsRoot->addWidget(rr73);
    auto *udpBox = new QGroupBox(QStringLiteral("WSJT-X UDP broadcast"), m_optionsDialog);
    auto *udpForm = new QFormLayout(udpBox);
    auto *udpEnabled = new QCheckBox(QStringLiteral("Broadcast status, decodes, and logged QSOs"), udpBox);
    auto *udpAddress = new QLineEdit(
        QSettings().value(QStringLiteral("wsjtUdp/address"), QStringLiteral("127.0.0.1")).toString(), udpBox);
    auto *udpPort = new QSpinBox(udpBox);
    udpPort->setRange(1, 65535);
    udpPort->setValue(QSettings().value(QStringLiteral("wsjtUdp/port"), 2237).toInt());
    udpEnabled->setChecked(QSettings().value(QStringLiteral("wsjtUdp/enabled"), false).toBool());
    udpForm->addRow(udpEnabled);
    udpForm->addRow(QStringLiteral("Address"), udpAddress);
    udpForm->addRow(QStringLiteral("Port"), udpPort);
    optionsRoot->addWidget(udpBox);
    auto *closeOptions = new QDialogButtonBox(QDialogButtonBox::Close, m_optionsDialog);
    optionsRoot->addWidget(closeOptions);
    connect(closeOptions, &QDialogButtonBox::rejected, m_optionsDialog, &QDialog::hide);
    connect(calibrate, &QPushButton::clicked, this, [this] { emit txSetupRequested(int(mode())); });
    connect(autoLog, &QCheckBox::toggled, this, [this](bool enabled) {
        m_autoLog = enabled;
        QSettings().setValue(QStringLiteral("ft8/autoLog"), enabled);
    });
    connect(m_rxAudio, &QSpinBox::valueChanged, this, [this](int hz) { setTone(false, hz); });
    connect(m_txAudio, &QSpinBox::valueChanged, this, [this](int hz) { setTone(true, hz); });
    connect(holdTx, &QCheckBox::toggled, this, [this](bool enabled) {
        m_session.holdTx = enabled;
        QSettings().setValue(QStringLiteral("ft8/holdTx"), enabled);
    });
    connect(callFirst, &QCheckBox::toggled, this, [this](bool enabled) {
        m_session.callFirst = enabled;
        QSettings().setValue(QStringLiteral("ft8/callFirst"), enabled);
    });
    connect(rr73, &QCheckBox::toggled, this, [this](bool enabled) {
        m_session.useRr73 = enabled;
        QSettings().setValue(QStringLiteral("ft8/rr73"), enabled);
    });
    connect(retries, &QSpinBox::valueChanged, this, [this](int count) {
        m_session.maxRetries = count;
        QSettings().setValue(QStringLiteral("ft8/maxRetries"), count);
    });
    connect(udpEnabled, &QCheckBox::toggled, this,
            [](bool enabled) { QSettings().setValue(QStringLiteral("wsjtUdp/enabled"), enabled); });
    connect(udpAddress, &QLineEdit::editingFinished, this,
            [udpAddress] { QSettings().setValue(QStringLiteral("wsjtUdp/address"), udpAddress->text().trimmed()); });
    connect(udpPort, &QSpinBox::editingFinished, this,
            [udpPort] { QSettings().setValue(QStringLiteral("wsjtUdp/port"), udpPort->value()); });

    auto *operating = new QHBoxLayout;
    m_even = new QCheckBox(QStringLiteral("Transmit even periods"), body);
    m_even->setChecked(m_session.even);
    m_auto = new QCheckBox(QStringLiteral("Auto sequence"), body);
    m_auto->setChecked(m_session.autoSequence);
    m_power = new QDoubleSpinBox(body);
    m_power->setRange(1.0, 110.0);
    m_power->setDecimals(1);
    m_power->setSingleStep(1.0);
    m_power->setSuffix(QStringLiteral(" W"));
    operating->addWidget(m_even);
    operating->addWidget(m_auto);
    operating->addStretch();
    operating->addWidget(new QLabel(QStringLiteral("RF power"), body));
    operating->addWidget(m_power);
    root->addLayout(operating);
    connect(m_call, &QLineEdit::editingFinished, this, [this] {
        m_session.myCall = m_call->text().trimmed().toUpper();
        QSettings().setValue("digital/myCall", m_session.myCall);
    });
    connect(m_grid, &QLineEdit::editingFinished, this, [this] {
        m_session.myGrid = m_grid->text().trimmed().toUpper();
        QSettings().setValue("digital/myGrid", m_session.myGrid);
    });
    connect(m_even, &QCheckBox::toggled, this, [this](bool even) {
        m_session.even = even;
        QSettings().setValue(QStringLiteral("ft8/even"), even);
    });
    connect(m_auto, &QCheckBox::toggled, this, [this](bool enabled) {
        m_session.autoSequence = enabled;
        QSettings().setValue(QStringLiteral("ft8/autoSequence"), enabled);
    });
    connect(m_power, &QDoubleSpinBox::editingFinished, this, [this] { emit powerRequested(m_power->value()); });

    m_waterfall = new FtxWaterfallWidget(body);
    m_waterfall->setToolTip(QStringLiteral("Left-click sets RX tone; right-click sets TX tone"));
    m_waterfall->frequencyChosen = [this](int hz, bool transmit) { setTone(transmit, hz); };
    m_tones = new QLabel(QStringLiteral("RX 1500 Hz  ·  TX 1500 Hz   (left-click RX, right-click TX)"), body);
    m_tones->setAlignment(Qt::AlignCenter);
    auto *waterPane = new QWidget(body);
    auto *waterLayout = new QVBoxLayout(waterPane);
    waterLayout->setContentsMargins(0, 0, 0, 0);
    waterLayout->setSpacing(3);
    waterLayout->addWidget(m_waterfall, 1);
    waterLayout->addWidget(m_tones);

    m_activity = new QTableWidget(0, 5, body);
    m_activity->setHorizontalHeaderLabels({QStringLiteral("UTC"), QStringLiteral("dB"), QStringLiteral("Hz"),
                                           QStringLiteral("DT"), QStringLiteral("Message")});
    m_activity->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_activity->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_activity->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_activity->verticalHeader()->hide();
    auto *splitter = new QSplitter(Qt::Vertical, body);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(waterPane);
    splitter->addWidget(m_activity);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    root->addWidget(splitter, 1);
    const QByteArray splitterState = QSettings().value(QStringLiteral("ft8/splitterState")).toByteArray();
    QTimer::singleShot(0, splitter, [splitter, splitterState] {
        if (splitterState.isEmpty() || !splitter->restoreState(splitterState))
            splitter->setSizes({150, 520});
    });
    connect(splitter, &QSplitter::splitterMoved, this,
            [splitter] { QSettings().setValue(QStringLiteral("ft8/splitterState"), splitter->saveState()); });
    connect(m_activity, &QTableWidget::cellClicked, this, [this](int row, int) { selectDecode(row, false); });
    connect(m_activity, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { selectDecode(row, true); });

    m_partner = new QLabel(QStringLiteral("No station selected"), body);
    m_message = new QLineEdit(body);
    m_message->setPlaceholderText(QStringLiteral("Next standard message"));
    auto *actions = new QHBoxLayout;
    m_cqButton = new QPushButton(QStringLiteral("CQ"), body);
    m_callButton = new QPushButton(QStringLiteral("Call selected"), body);
    m_haltButton = new QPushButton(QStringLiteral("Halt TX"), body);
    m_haltButton->setObjectName(QStringLiteral("danger"));
    auto *log = new QPushButton(QStringLiteral("Log QSO"), body);
    auto *openLog = new QPushButton(QStringLiteral("Logbook…"), body);
    actions->addWidget(m_cqButton);
    actions->addWidget(m_callButton);
    actions->addWidget(m_haltButton);
    actions->addStretch();
    actions->addWidget(log);
    actions->addWidget(openLog);
    root->addWidget(m_partner);
    root->addWidget(m_message);
    root->addLayout(actions);
    connect(m_cqButton, &QPushButton::clicked, this, &FtxWindow::armCq);
    connect(m_callButton, &QPushButton::clicked, this, &FtxWindow::armSelected);
    connect(m_haltButton, &QPushButton::clicked, this, &FtxWindow::halt);
    connect(log, &QPushButton::clicked, this, [this] { emit logContactRequested(contactRecord()); });
    connect(openLog, &QPushButton::clicked, this, &FtxWindow::logbookRequested);

    m_protection = new QLabel(QStringLiteral("TX audio not calibrated"), body);
    m_protection->setStyleSheet(QStringLiteral("color:#f3b33d;"));
    root->addWidget(m_protection);
    m_status = new QLabel(QStringLiteral("Waiting for Main RX audio"), body);
    m_cycle = new QProgressBar(body);
    m_cycle->setRange(0, 1000);
    m_cycle->setTextVisible(false);
    m_txProgress = new QProgressBar(body);
    m_txProgress->setRange(0, 1000);
    m_txProgress->hide();
    root->addWidget(m_status);
    root->addWidget(m_cycle);
    root->addWidget(m_txProgress);
    setCentralWidget(body);
    setDecodeRows(m_decodeRows);
}

void FtxWindow::showOptions() {
    m_optionsDialog->show();
    m_optionsDialog->raise();
    m_optionsDialog->activateWindow();
}

void FtxWindow::setTone(bool transmit, int hz) {
    hz = qBound(100, hz, 3200);
    if (transmit) {
        m_session.txHz = hz;
        QSettings().setValue(QStringLiteral("ft8/txHz"), hz);
        if (m_txAudio) {
            const QSignalBlocker blocker(m_txAudio);
            m_txAudio->setValue(hz);
        }
    } else {
        m_session.rxHz = hz;
        QSettings().setValue(QStringLiteral("ft8/rxHz"), hz);
        if (m_rxAudio) {
            const QSignalBlocker blocker(m_rxAudio);
            m_rxAudio->setValue(hz);
        }
    }
    m_selectedTxTone = transmit;
    m_waterfall->setMarkers(m_session.rxHz, m_session.txHz);
    m_tones->setText(QStringLiteral("RX %1 Hz  ·  TX %2 Hz   (%3 selected)")
                         .arg(m_session.rxHz)
                         .arg(m_session.txHz)
                         .arg(m_selectedTxTone ? QStringLiteral("TX") : QStringLiteral("RX")));
}

void FtxWindow::adjustSelectedTone(int steps) {
    setTone(m_selectedTxTone, (m_selectedTxTone ? m_session.txHz : m_session.rxHz) + steps * 5);
}

void FtxWindow::switchSelectedTone() {
    m_selectedTxTone = !m_selectedTxTone;
    setTone(m_selectedTxTone, m_selectedTxTone ? m_session.txHz : m_session.rxHz);
}

void FtxWindow::setDecodeRows(int rows) {
    m_decodeRows = rows == 1 ? 1 : 2;
    for (int column = 0; column < 4; ++column)
        m_activity->setColumnHidden(column, m_decodeRows == 2);
    m_activity->verticalHeader()->setDefaultSectionSize(m_decodeRows == 2 ? 46 : 28);
    addDecodes({});
}

void FtxWindow::setTransmitProtection(const QString &status, bool fault) {
    m_protection->setText(status);
    m_protection->setStyleSheet(fault ? QStringLiteral("color:#ff7070;font-weight:bold;")
                                      : QStringLiteral("color:#67ef89;"));
}

Ft8::Mode FtxWindow::mode() const {
    return m_mode->currentIndex() == 1 ? Ft8::Mode::FT4 : Ft8::Mode::FT8;
}
bool FtxWindow::receiving() const {
    return isVisible() && !m_liveTx;
}

void FtxWindow::setRadioState(bool connected, qint64 frequencyHz, const QString &radioMode, bool transmitting,
                              double watts) {
    m_connected = connected;
    m_radioTx = transmitting;
    m_frequencyHz = frequencyHz;
    if (!m_power->hasFocus()) {
        const QSignalBlocker blocker(m_power);
        m_power->setValue(watts);
    }
    if (!m_frequency->hasFocus() && frequencyHz > 0)
        m_frequency->setText(QString::number(double(frequencyHz) / 1e6, 'f', 6));
    m_radio->setText(QStringLiteral("%1 · %2 · %3 · %4 W")
                         .arg(connected ? QStringLiteral("Connected") : QStringLiteral("Disconnected"),
                              formatFrequency(frequencyHz), radioMode)
                         .arg(watts, 0, 'f', watts < 10 ? 1 : 0));
    refreshSession();
}

void FtxWindow::addDecodes(const QVector<Ft8::Decode> &decodes) {
    for (const auto &decode : decodes) {
        if (decode.mode != mode())
            continue;
        if (std::none_of(m_decodes.cbegin(), m_decodes.cend(),
                         [&](const auto &old) { return old.id() == decode.id(); }))
            m_decodes.prepend(decode);
        if (m_session.receive(decode))
            refreshSession();
    }
    while (m_decodes.size() > 250)
        m_decodes.removeLast();
    m_activity->setRowCount(m_decodes.size());
    for (int row = 0; row < m_decodes.size(); ++row) {
        const auto &d = m_decodes[row];
        const QString time = d.utc.toUTC().toString("HH:mm:ss");
        const QString db = d.snr ? Ft8::reportText(*d.snr) : QStringLiteral("—");
        const QString detail =
            QStringLiteral("%1 UTC  ·  %2 dB  ·  %3 Hz  ·  DT %4").arg(time, db).arg(d.audioHz).arg(d.dt, 0, 'f', 1);
        const QStringList cells{time, d.snr ? Ft8::reportText(*d.snr) : QStringLiteral("—"), QString::number(d.audioHz),
                                QString::number(d.dt, 'f', 1),
                                m_decodeRows == 2 ? d.message + QLatin1Char('\n') + detail : d.message};
        for (int column = 0; column < cells.size(); ++column)
            m_activity->setItem(row, column, new QTableWidgetItem(cells[column]));
        const auto parsed = Ft8::parseMessage(d.message);
        const QColor color = parsed.cq                       ? QColor("#204f39")
                             : parsed.to == m_session.myCall ? QColor("#662d35")
                                                             : QColor("#172430");
        for (int column = 0; column < cells.size(); ++column)
            m_activity->item(row, column)->setBackground(color);
    }
}

void FtxWindow::addSpectrum(const QVector<float> &db, double firstHz, double binHz) {
    m_waterfall->addSpectrum(db, firstHz, binHz);
}
void FtxWindow::setReceiveStatus(const QString &status) {
    if (!m_liveTx)
        m_status->setText(status);
}
void FtxWindow::setTransmitState(bool active, const QString &status) {
    m_liveTx = active;
    m_txProgress->setVisible(active);
    m_status->setText(status);
    if (!active)
        m_txProgress->setValue(0);
    refreshSession();
}
void FtxWindow::setTransmitProgress(int emitted, int total) {
    if (total > 0)
        m_txProgress->setValue(qRound(1000.0 * emitted / total));
}
void FtxWindow::finishTransmit(bool success, const QString &status) {
    if (success)
        m_session.sent(QDateTime::currentDateTimeUtc());
    setTransmitState(false, status);
    refreshSession();
}

void FtxWindow::chooseBand(int index) {
    const int bandIndex = m_band->itemData(index).toInt();
    if (bandIndex < 0 || bandIndex >= Ft8::bands().size())
        return;
    const auto &band = Ft8::bands()[bandIndex];
    const qint64 hz = mode() == Ft8::Mode::FT4 ? band.ft4Hz : band.ft8Hz;
    if (hz > 0) {
        m_frequency->setText(QString::number(double(hz) / 1e6, 'f', 6));
        QSettings().setValue(QStringLiteral("ft8/dialMHz"), m_frequency->text());
        emit frequencyRequested(hz);
    }
}

void FtxWindow::selectDecode(int row, bool callNow) {
    if (row < 0 || row >= m_decodes.size())
        return;
    m_session.myCall = m_call->text().trimmed().toUpper();
    m_session.myGrid = m_grid->text().trimmed().toUpper();
    m_session.mode = mode();
    m_session.holdTx = true;
    if (!m_session.select(m_decodes[row]))
        return;
    setTone(false, m_session.rxHz);
    m_even->setChecked(m_session.even);
    if (callNow)
        armSelected();
    refreshSession();
}

void FtxWindow::armCq() {
    m_session.myCall = m_call->text().trimmed().toUpper();
    m_session.myGrid = m_grid->text().trimmed().toUpper();
    m_session.mode = mode();
    m_session.even = m_even->isChecked();
    if (!m_session.callCq() || !m_session.arm()) {
        QMessageBox::warning(this, QStringLiteral("FT8 / FT4"),
                             QStringLiteral("Enter a valid callsign and four- or six-character grid first."));
        return;
    }
    refreshSession();
}

void FtxWindow::armSelected() {
    m_session.myCall = m_call->text().trimmed().toUpper();
    m_session.myGrid = m_grid->text().trimmed().toUpper();
    m_session.nextMessage = m_message->text().trimmed().toUpper();
    m_session.even = m_even->isChecked();
    if (!m_session.arm()) {
        QMessageBox::warning(this, QStringLiteral("FT8 / FT4"),
                             QStringLiteral("Select a decoded station and enter valid station details first."));
        return;
    }
    refreshSession();
}

void FtxWindow::halt() {
    m_session.halt();
    m_lastQueuedSlot = -1;
    if (m_liveTx)
        emit stopRequested();
    refreshSession();
}

void FtxWindow::tick() {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const int period = Ft8::periodMs(mode());
    m_cycle->setValue(int((now % period) * 1000 / period));
    if (m_session.armed && !m_liveTx && !m_radioTx)
        queueNextTransmit();
}

void FtxWindow::queueNextTransmit() {
    if (!m_connected || m_session.nextMessage.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 slotUtc = Ft8Transmitter::nextSlot(now, mode(), m_session.even);
    if (slotUtc == m_lastQueuedSlot || slotUtc - now > 700)
        return;
    m_lastQueuedSlot = slotUtc;
    m_session.nextMessage = m_message->text().trimmed().toUpper();
    emit transmitRequested(m_session.nextMessage, int(mode()), m_session.txHz, slotUtc);
}

void FtxWindow::refreshSession() {
    m_message->setText(m_session.nextMessage);
    m_partner->setText(
        m_session.dxCall.isEmpty()
            ? (m_session.callingCq ? QStringLiteral("Calling CQ") : QStringLiteral("No station selected"))
            : QStringLiteral("QSO: %1  %2  sent %3  received %4")
                  .arg(m_session.dxCall, m_session.dxGrid,
                       m_session.sentReport.isEmpty() ? QStringLiteral("—") : m_session.sentReport,
                       m_session.receivedReport.isEmpty() ? QStringLiteral("—") : m_session.receivedReport));
    const bool canArm = !m_liveTx && !m_radioTx;
    m_cqButton->setEnabled(canArm);
    m_callButton->setEnabled(canArm && !m_session.dxCall.isEmpty());
    m_haltButton->setEnabled(m_session.armed || m_liveTx);
    m_waterfall->setMarkers(m_session.rxHz, m_session.txHz);
    if (m_session.complete && !m_completionHandled) {
        m_completionHandled = true;
        if (m_autoLog) {
            m_status->setText(QStringLiteral("QSO complete · saving contact…"));
            emit autoLogContactRequested(contactRecord());
        } else {
            m_status->setText(QStringLiteral("QSO complete · click Log QSO to review and save"));
        }
    } else if (!m_session.complete) {
        m_completionHandled = false;
    }
}

AdifRecord FtxWindow::contactRecord() const {
    auto record = basicContact(m_session.dxCall, Ft8::modeName(mode()), m_frequencyHz, m_call->text());
    record["GRIDSQUARE"] = m_session.dxGrid;
    record["MY_GRIDSQUARE"] = m_grid->text().trimmed().toUpper();
    record["RST_SENT"] = m_session.sentReport;
    record["RST_RCVD"] = m_session.receivedReport;
    const QDateTime start = m_session.started.isValid() ? m_session.started : QDateTime::currentDateTimeUtc();
    const QDateTime end = m_session.ended.isValid() ? m_session.ended : QDateTime::currentDateTimeUtc();
    record["QSO_DATE"] = start.toUTC().toString("yyyyMMdd");
    record["TIME_ON"] = start.toUTC().toString("HHmmss");
    record["QSO_DATE_OFF"] = end.toUTC().toString("yyyyMMdd");
    record["TIME_OFF"] = end.toUTC().toString("HHmmss");
    record["FREQ"] = QString::number(double(m_frequencyHz + m_session.txHz) / 1e6, 'f', 6);
    if (mode() == Ft8::Mode::FT4) {
        record["MODE"] = QStringLiteral("MFSK");
        record["SUBMODE"] = QStringLiteral("FT4");
    }
    return record;
}

void FtxWindow::closeEvent(QCloseEvent *event) {
    halt();
    emit captureChanged();
    event->accept();
}

SstvWindow::SstvWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("QK4 — SSTV"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(1040, 760);
    setMinimumSize(760, 560);
    setStyleSheet(QString::fromLatin1(toolStyle));
    buildUi();
    QSettings settings;
    m_myCall->setText(settings.value("digital/myCall").toString());
    refreshHistory();
}

void SstvWindow::buildUi() {
    auto *body = new QWidget(this);
    auto *root = new QVBoxLayout(body);
    m_radio = new QLabel(QStringLiteral("Disconnected"), body);
    m_radio->setStyleSheet("color:#f3b33d;font-weight:bold;");
    root->addWidget(m_radio);
    auto *protectionRow = new QHBoxLayout;
    m_txProtection = new QLabel(QStringLiteral("SSTV TX audio not calibrated"), body);
    m_txProtection->setStyleSheet(QStringLiteral("color:#f3b33d;"));
    auto *txSetup = new QPushButton(QStringLiteral("TX audio setup…"), body);
    protectionRow->addWidget(m_txProtection, 1);
    protectionRow->addWidget(txSetup);
    root->addLayout(protectionRow);
    connect(txSetup, &QPushButton::clicked, this, [this] { emit txSetupRequested(int(DigitalTxGuard::Mode::Sstv)); });
    m_tabs = new QTabWidget(body);
    root->addWidget(m_tabs, 1);

    auto *receive = new QWidget(m_tabs);
    auto *rx = new QVBoxLayout(receive);
    auto *rxTop = new QHBoxLayout;
    m_rxStatus = new QLabel(QStringLiteral("AUTO RX · waiting for Main RX audio"), receive);
    auto *reset = new QPushButton(QStringLiteral("Reset AUTO RX"), receive);
    m_historyCombo = new QComboBox(receive);
    m_historyCombo->addItem(QStringLiteral("Live image"));
    rxTop->addWidget(m_rxStatus, 1);
    rxTop->addWidget(m_historyCombo);
    rxTop->addWidget(reset);
    rx->addLayout(rxTop);
    m_rxImage = new QLabel(receive);
    m_rxImage->setAlignment(Qt::AlignCenter);
    m_rxImage->setMinimumSize(320, 240);
    m_rxImage->setStyleSheet("background:#050708;border:1px solid #405363;");
    rx->addWidget(m_rxImage, 1);
    m_rxLevel = new QProgressBar(receive);
    m_rxLevel->setRange(0, 100);
    m_rxLevel->setFormat(QStringLiteral("Input %p%"));
    rx->addWidget(m_rxLevel);
    auto *rxCallRow = new QHBoxLayout;
    m_rxCall = new QLineEdit(receive);
    m_rxCall->setPlaceholderText(QStringLiteral("Received callsign"));
    auto *reply = new QPushButton(QStringLiteral("Reply"), receive);
    auto *log = new QPushButton(QStringLiteral("Log QSO"), receive);
    auto *openLog = new QPushButton(QStringLiteral("Logbook…"), receive);
    rxCallRow->addWidget(new QLabel(QStringLiteral("RX call"), receive));
    rxCallRow->addWidget(m_rxCall, 1);
    rxCallRow->addWidget(reply);
    rxCallRow->addWidget(log);
    rxCallRow->addWidget(openLog);
    rx->addLayout(rxCallRow);
    m_tabs->addTab(receive, QStringLiteral("Receive"));
    connect(reset, &QPushButton::clicked, this, &SstvWindow::resetReceiveRequested);
    connect(m_historyCombo, &QComboBox::currentIndexChanged, this, &SstvWindow::selectHistory);
    connect(reply, &QPushButton::clicked, this, [this] {
        m_toCall->setText(m_rxCall->text().trimmed().toUpper());
        m_tabs->setCurrentIndex(1);
    });
    connect(log, &QPushButton::clicked, this, [this] { emit logContactRequested(contactRecord()); });
    connect(openLog, &QPushButton::clicked, this, &SstvWindow::logbookRequested);

    auto *transmit = new QWidget(m_tabs);
    auto *tx = new QVBoxLayout(transmit);
    auto *txTop = new QHBoxLayout;
    auto *choose = new QPushButton(QStringLiteral("Choose image…"), transmit);
    m_mode = new QComboBox(transmit);
    for (const auto &spec : SstvModeRegistry::all())
        m_mode->addItem(QStringLiteral("%1 · %2×%3 · %4 s")
                            .arg(spec.displayName)
                            .arg(spec.width)
                            .arg(spec.height)
                            .arg(spec.durationMs / 1000),
                        int(spec.id));
    m_mode->setCurrentIndex(m_mode->findData(int(SstvModeId::ScottieS1)));
    m_modeInfo = new QLabel(transmit);
    txTop->addWidget(choose);
    txTop->addWidget(m_mode, 1);
    txTop->addWidget(m_modeInfo);
    tx->addLayout(txTop);
    m_txImage = new QLabel(transmit);
    m_txImage->setAlignment(Qt::AlignCenter);
    m_txImage->setMinimumSize(320, 240);
    m_txImage->setStyleSheet("background:#050708;border:1px solid #405363;");
    tx->addWidget(m_txImage, 1);
    auto *ids = new QGridLayout;
    m_myCall = new QLineEdit(transmit);
    m_myCall->setPlaceholderText(QStringLiteral("My callsign"));
    m_toCall = new QLineEdit(transmit);
    m_toCall->setPlaceholderText(QStringLiteral("To callsign (for log/reply)"));
    m_fskId = new QCheckBox(QStringLiteral("FSK ID"), transmit);
    m_cwId = new QCheckBox(QStringLiteral("CW ID"), transmit);
    m_fskId->setChecked(true);
    m_cwId->setChecked(true);
    m_cwWpm = new QComboBox(transmit);
    for (int wpm : {10, 15, 20, 25, 30})
        m_cwWpm->addItem(QString::number(wpm) + QStringLiteral(" WPM"), wpm);
    m_cwWpm->setCurrentIndex(2);
    m_power = new QDoubleSpinBox(transmit);
    m_power->setRange(1.0, 110.0);
    m_power->setDecimals(1);
    m_power->setSingleStep(1.0);
    m_power->setSuffix(QStringLiteral(" W"));
    ids->addWidget(new QLabel(QStringLiteral("My call"), transmit), 0, 0);
    ids->addWidget(m_myCall, 0, 1);
    ids->addWidget(new QLabel(QStringLiteral("To"), transmit), 0, 2);
    ids->addWidget(m_toCall, 0, 3);
    ids->addWidget(m_fskId, 1, 0);
    ids->addWidget(m_cwId, 1, 1);
    ids->addWidget(m_cwWpm, 1, 2);
    ids->addWidget(m_power, 1, 3);
    tx->addLayout(ids);
    auto *txActions = new QHBoxLayout;
    m_send = new QPushButton(QStringLiteral("Transmit SSTV"), transmit);
    m_stop = new QPushButton(QStringLiteral("STOP SSTV"), transmit);
    m_stop->setObjectName(QStringLiteral("danger"));
    m_stop->hide();
    auto *logTx = new QPushButton(QStringLiteral("Log QSO"), transmit);
    m_txStatus = new QLabel(QStringLiteral("Choose an image to transmit"), transmit);
    txActions->addWidget(m_send);
    txActions->addWidget(m_stop);
    txActions->addWidget(m_txStatus, 1);
    txActions->addWidget(logTx);
    tx->addLayout(txActions);
    m_txProgress = new QProgressBar(transmit);
    m_txProgress->setRange(0, 1000);
    tx->addWidget(m_txProgress);
    m_tabs->addTab(transmit, QStringLiteral("Transmit"));
    connect(choose, &QPushButton::clicked, this, &SstvWindow::chooseImage);
    connect(m_mode, &QComboBox::currentIndexChanged, this, &SstvWindow::refreshPreview);
    connect(m_myCall, &QLineEdit::editingFinished, this, [this] {
        m_myCall->setText(m_myCall->text().trimmed().toUpper());
        QSettings().setValue("digital/myCall", m_myCall->text());
    });
    connect(m_power, &QDoubleSpinBox::editingFinished, this, [this] { emit powerRequested(m_power->value()); });
    connect(m_send, &QPushButton::clicked, this, [this] {
        if (m_sourceImage.isNull()) {
            QMessageBox::warning(this, QStringLiteral("SSTV"), QStringLiteral("Choose an image first."));
            return;
        }
        const auto *spec = SstvModeRegistry::find(SstvModeId(m_mode->currentData().toInt()));
        if (!spec)
            return;
        const auto scaled =
            m_sourceImage.scaled(spec->width, spec->height, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const auto frame = scaled.copy(QRect(qMax(0, (scaled.width() - spec->width) / 2),
                                             qMax(0, (scaled.height() - spec->height) / 2), spec->width, spec->height));
        emit transmitRequested(frame, int(spec->id), m_cwId->isChecked() ? m_myCall->text() : QString(),
                               m_cwWpm->currentData().toInt(), m_fskId->isChecked() ? m_myCall->text() : QString());
    });
    connect(m_stop, &QPushButton::pressed, this, &SstvWindow::stopRequested);
    connect(logTx, &QPushButton::clicked, this, [this] { emit logContactRequested(contactRecord()); });
    setCentralWidget(body);
    refreshPreview();
}

void SstvWindow::chooseImage() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Choose SSTV image"), {},
                                     QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All files (*)"));
    if (path.isEmpty())
        return;
    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, QStringLiteral("SSTV"), QStringLiteral("The selected image could not be read."));
        return;
    }
    m_sourceImage = image;
    refreshPreview();
}

void SstvWindow::refreshPreview() {
    const auto *spec = m_mode ? SstvModeRegistry::find(SstvModeId(m_mode->currentData().toInt())) : nullptr;
    if (!spec)
        return;
    m_modeInfo->setText(
        QStringLiteral("%1×%2 · about %3 s").arg(spec->width).arg(spec->height).arg(spec->durationMs / 1000));
    if (!m_sourceImage.isNull())
        m_txImage->setPixmap(
            QPixmap::fromImage(m_sourceImage).scaled(m_txImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void SstvWindow::setRadioState(bool connected, qint64 rxFrequencyHz, const QString &rxMode, qint64 txFrequencyHz,
                               const QString &txMode, double watts) {
    m_connected = connected;
    m_rxFrequency = rxFrequencyHz;
    m_txFrequency = txFrequencyHz;
    if (!m_power->hasFocus()) {
        const QSignalBlocker blocker(m_power);
        m_power->setValue(watts);
    }
    m_radio->setText(QStringLiteral("%1 · RX %2 %3 · TX %4 %5 · %6 W")
                         .arg(connected ? QStringLiteral("Connected") : QStringLiteral("Disconnected"),
                              formatFrequency(rxFrequencyHz), rxMode, formatFrequency(txFrequencyHz), txMode)
                         .arg(watts, 0, 'f', watts < 10 ? 1 : 0));
}
void SstvWindow::setReceiveStatus(const QString &status) {
    m_rxStatus->setText(status);
}
void SstvWindow::setReceiveLevel(int percent) {
    m_rxLevel->setValue(percent);
}
void SstvWindow::setReceiveImage(const QImage &image, int completedRows, int totalRows, const QString &slantStatus) {
    m_receiveImageValue = image;
    m_rxImage->setPixmap(
        QPixmap::fromImage(image).scaled(m_rxImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_rxStatus->setText(QStringLiteral("Decoding %1/%2 lines · %3").arg(completedRows).arg(totalRows).arg(slantStatus));
}
void SstvWindow::completeReceiveImage(const QImage &image, int modeId, const QString &slantStatus, qint64 frequencyHz) {
    m_receiveImageValue = image;
    const auto *spec = SstvModeRegistry::find(SstvModeId(modeId));
    QString error;
    if (!m_storage.saveReceived(image, modeId, spec ? spec->displayName : QStringLiteral("SSTV"), slantStatus,
                                frequencyHz, nullptr, &error))
        m_rxStatus->setText(QStringLiteral("Decoded, but could not save: %1").arg(error));
    else
        m_rxStatus->setText(QStringLiteral("Image complete · %1 · saved to history")
                                .arg(spec ? spec->displayName : QStringLiteral("SSTV")));
    m_rxImage->setPixmap(
        QPixmap::fromImage(image).scaled(m_rxImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    refreshHistory();
}
void SstvWindow::receiveCallsign(const QString &callsign, const QString &source, int confidence) {
    m_rxCall->setText(callsign);
    m_rxCall->setToolTip(QStringLiteral("Detected by %1, confidence %2").arg(source).arg(confidence));
}
void SstvWindow::setTransmitState(bool active, const QString &status) {
    m_programTx = active;
    m_send->setVisible(!active);
    m_stop->setVisible(active);
    m_tabs->setTabEnabled(0, !active);
    m_txStatus->setText(status);
    if (!active)
        m_txProgress->setValue(0);
}
void SstvWindow::setTransmitProgress(int emitted, int total) {
    if (total > 0)
        m_txProgress->setValue(qRound(1000.0 * emitted / total));
}

void SstvWindow::setTransmitProtection(const QString &status, bool fault) {
    m_txProtection->setText(status);
    m_txProtection->setStyleSheet(fault ? QStringLiteral("color:#ff7070;font-weight:bold;")
                                        : QStringLiteral("color:#67ef89;"));
}
bool SstvWindow::receiving() const {
    return isVisible() && !m_programTx;
}

void SstvWindow::refreshHistory() {
    QString error;
    m_history = m_storage.received(&error);
    const QSignalBlocker blocker(m_historyCombo);
    m_historyCombo->clear();
    m_historyCombo->addItem(QStringLiteral("Live image"));
    for (const auto &record : m_history)
        m_historyCombo->addItem(
            QStringLiteral("%1 · %2 · %3")
                .arg(record.receivedUtc.toLocalTime().toString("yyyy-MM-dd HH:mm"), record.modeName, record.callsign));
}
void SstvWindow::selectHistory(int index) {
    if (index <= 0 || index - 1 >= m_history.size())
        return;
    const auto &record = m_history[index - 1];
    QImage image(record.imagePath);
    if (!image.isNull()) {
        m_receiveImageValue = image;
        m_rxImage->setPixmap(
            QPixmap::fromImage(image).scaled(m_rxImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_rxCall->setText(record.callsign);
        m_rxFrequency = record.frequencyHz;
    }
}
AdifRecord SstvWindow::contactRecord() const {
    return basicContact(m_tabs->currentIndex() == 0 ? m_rxCall->text() : m_toCall->text(), QStringLiteral("SSTV"),
                        m_tabs->currentIndex() == 0 ? m_rxFrequency : m_txFrequency, m_myCall->text());
}
void SstvWindow::closeEvent(QCloseEvent *event) {
    if (m_programTx)
        emit stopRequested();
    event->accept();
}

LogbookWindow::LogbookWindow(QWidget *parent) : QMainWindow(parent), m_logbook(logPath()) {
    setWindowTitle(QStringLiteral("QK4 — Logbook"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(980, 650);
    setMinimumSize(700, 420);
    setStyleSheet(QString::fromLatin1(toolStyle));
    buildUi();
}

void LogbookWindow::buildUi() {
    auto *body = new QWidget(this);
    auto *root = new QVBoxLayout(body);
    m_search = new QLineEdit(body);
    m_search->setPlaceholderText(QStringLiteral("Search call, date, band, mode, grid, or notes"));
    root->addWidget(m_search);
    m_table = new QTableWidget(0, 7, body);
    m_table->setHorizontalHeaderLabels({QStringLiteral("UTC"), QStringLiteral("Call"), QStringLiteral("Band"),
                                        QStringLiteral("Mode"), QStringLiteral("Frequency"), QStringLiteral("Grid"),
                                        QStringLiteral("Comments")});
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 1);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add…"), body);
    auto *edit = new QPushButton(QStringLiteral("Edit…"), body);
    auto *import = new QPushButton(QStringLiteral("Import ADIF…"), body);
    auto *exportButton = new QPushButton(QStringLiteral("Export ADIF…"), body);
    m_status = new QLabel(body);
    buttons->addWidget(add);
    buttons->addWidget(edit);
    buttons->addWidget(import);
    buttons->addWidget(exportButton);
    buttons->addWidget(m_status, 1);
    root->addLayout(buttons);
    setCentralWidget(body);
    connect(m_search, &QLineEdit::textChanged, this, &LogbookWindow::populate);
    connect(add, &QPushButton::clicked, this, [this] {
        auto record = m_defaults;
        const auto now = QDateTime::currentDateTimeUtc();
        record["QSO_DATE"] = now.toString("yyyyMMdd");
        record["TIME_ON"] = now.toString("HHmmss");
        editRecord(record);
    });
    connect(edit, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row < 0 || !m_table->item(row, 0))
            return;
        const int index = m_table->item(row, 0)->data(Qt::UserRole).toInt();
        if (index >= 0 && index < m_logbook.records().size())
            editRecord(m_logbook.records()[index], index);
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, edit, [edit](int, int) { edit->click(); });
    connect(import, &QPushButton::clicked, this, &LogbookWindow::importAdif);
    connect(exportButton, &QPushButton::clicked, this, &LogbookWindow::exportAdif);
}

void LogbookWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    reload();
}
void LogbookWindow::setDefaults(const AdifRecord &record) {
    m_defaults = record;
}
void LogbookWindow::reload() {
    QString error;
    if (!m_logbook.load(&error))
        m_status->setText(error);
    else {
        m_status->setText(QStringLiteral("%1 contacts").arg(m_logbook.records().size()));
        populate();
    }
}

void LogbookWindow::populate() {
    const QString filter = m_search->text().trimmed();
    m_table->setRowCount(0);
    for (int index = m_logbook.records().size() - 1; index >= 0; --index) {
        const auto &record = m_logbook.records()[index];
        QString haystack;
        for (auto it = record.cbegin(); it != record.cend(); ++it)
            haystack += it.value() + QLatin1Char(' ');
        if (!filter.isEmpty() && !haystack.contains(filter, Qt::CaseInsensitive))
            continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        const QStringList values{record.value("QSO_DATE") + QLatin1Char(' ') + record.value("TIME_ON"),
                                 record.value("CALL"),
                                 record.value("BAND"),
                                 Ft8Logbook::canonicalMode(record),
                                 record.value("FREQ"),
                                 record.value("GRIDSQUARE"),
                                 record.value("COMMENT")};
        for (int column = 0; column < values.size(); ++column)
            m_table->setItem(row, column, new QTableWidgetItem(values[column]));
        m_table->item(row, 0)->setData(Qt::UserRole, index);
    }
}

bool LogbookWindow::editRecord(AdifRecord record, int index) {
    QDialog dialog(this);
    dialog.setWindowTitle(index < 0 ? QStringLiteral("Add contact") : QStringLiteral("Edit contact"));
    auto *root = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    const QList<QPair<QString, QString>> definitions{{"CALL", "Callsign"},
                                                     {"QSO_DATE", "UTC date (YYYYMMDD)"},
                                                     {"TIME_ON", "UTC time (HHMMSS)"},
                                                     {"FREQ", "Frequency (MHz)"},
                                                     {"BAND", "Band"},
                                                     {"MODE", "Mode"},
                                                     {"SUBMODE", "Submode"},
                                                     {"RST_SENT", "Report sent"},
                                                     {"RST_RCVD", "Report received"},
                                                     {"GRIDSQUARE", "Station grid"},
                                                     {"STATION_CALLSIGN", "My callsign"},
                                                     {"MY_GRIDSQUARE", "My grid"},
                                                     {"NAME", "Name"},
                                                     {"QTH", "Location"},
                                                     {"COMMENT", "Comments"}};
    QMap<QString, QLineEdit *> fields;
    for (const auto &definition : definitions) {
        auto *field = new QLineEdit(record.value(definition.first), &dialog);
        form->addRow(definition.second, field);
        fields.insert(definition.first, field);
    }
    root->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QString value = it.value()->text().trimmed();
        if (value.isEmpty())
            record.remove(it.key());
        else
            record[it.key()] = value;
    }
    QString error;
    const bool saved = index < 0 ? m_logbook.append(record, &error) : m_logbook.replace(index, record, &error);
    if (!saved)
        QMessageBox::warning(this, QStringLiteral("Logbook"), error);
    reload();
    return saved;
}

bool LogbookWindow::addContact(const AdifRecord &record, bool review, QString *error) {
    reload();
    if (review) {
        show();
        raise();
        activateWindow();
    }
    return review ? editRecord(record) : m_logbook.append(record, error);
}

void LogbookWindow::importAdif() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import ADIF"), {},
                                                      QStringLiteral("ADIF (*.adi *.adif);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Import ADIF"), file.errorString());
        return;
    }
    const auto preview = m_logbook.preview(QString::fromUtf8(file.readAll()));
    if (!preview.errors.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Import ADIF"), preview.errors.join(QLatin1Char('\n')));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Import ADIF"),
                              QStringLiteral("Import %1 new contacts? %2 duplicates will be skipped.")
                                  .arg(preview.records.size())
                                  .arg(preview.duplicates)) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_logbook.importRecords(preview, &error))
        QMessageBox::warning(this, QStringLiteral("Import ADIF"), error);
    reload();
}

void LogbookWindow::exportAdif() {
    QString error;
    const QString text = m_logbook.exportAdif(&error);
    if (text.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Export ADIF"), error);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export ADIF"),
                                                      QStringLiteral("qk4-log.adi"), QStringLiteral("ADIF (*.adi)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const QByteArray data = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        QMessageBox::warning(this, QStringLiteral("Export ADIF"), file.errorString());
}
