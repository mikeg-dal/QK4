#include "ui/pages/displaypage.h"
#include "settings/interfacescale.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include <QApplication>
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <cmath>

namespace {

QString percentText(double factor) {
    return QString("%1%").arg(std::lround(factor * 100.0));
}

} // namespace

DisplayPage::DisplayPage(QWidget *parent) : QWidget(parent) {
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "Display");

    auto *form = OptionsPageLayout::addForm(layout);
    m_scaleCombo = new QComboBox(this);
    m_scaleCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_scaleCombo->addItem("Auto (fit the screen)", RadioSettings::kInterfaceScaleAuto);
    for (int percent : RadioSettings::kInterfaceScales)
        m_scaleCombo->addItem(QString("%1%").arg(percent), percent);
    const int index = m_scaleCombo->findData(RadioSettings::instance()->interfaceScale());
    m_scaleCombo->setCurrentIndex(index >= 0 ? index : 0);
    OptionsPageLayout::addRow(form, this, "Interface Scale", m_scaleCombo);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(K4Styles::Dialog::formValue());
    QSizePolicy statusPolicy = m_statusLabel->sizePolicy();
    statusPolicy.setHorizontalPolicy(QSizePolicy::Expanding);
    m_statusLabel->setSizePolicy(statusPolicy);
    form->addRow(QString(), m_statusLabel);

    m_restartButton = new QPushButton("Restart Now", this);
    m_restartButton->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_restartButton->setCursor(Qt::PointingHandCursor);
    m_restartButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    form->addRow(QString(), m_restartButton);

    OptionsPageLayout::addHelp(layout, this,
                               "Scales the whole window without rearranging it. Auto shrinks the interface when the "
                               "screen is too small for it, and enlarges it to 125% on a screen with room for that. "
                               "A new scale takes effect when QK4 restarts.");

    layout->addStretch();

    connect(m_scaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DisplayPage::onScaleChanged);
    connect(m_restartButton, &QPushButton::clicked, this, &DisplayPage::onRestartClicked);

    updateStatus();
}

void DisplayPage::onScaleChanged(int index) {
    if (index < 0)
        return;
    RadioSettings::instance()->setInterfaceScale(m_scaleCombo->itemData(index).toInt());
    updateStatus();
}

void DisplayPage::onRestartClicked() {
    InterfaceScale::requestRestart();
    // WHY: closing rather than quitting runs MainWindow's ordered shutdown (TCI, devices, audio);
    // main() relaunches only once that has finished.
    QApplication::closeAllWindows();
}

void DisplayPage::updateStatus() {
    if (InterfaceScale::environmentOverride()) {
        m_statusLabel->setText(QString("QT_SCALE_FACTOR is set in the environment, so QK4 is running at %1 and "
                                       "this setting is not applied.")
                                   .arg(percentText(InterfaceScale::applied())));
        return;
    }
    const int chosen = RadioSettings::instance()->interfaceScale();
    const double applied = InterfaceScale::applied();
    if (chosen == RadioSettings::kInterfaceScaleAuto) {
        m_statusLabel->setText(QString("Auto is running at %1 on this screen.").arg(percentText(applied)));
    } else if (std::abs(chosen / 100.0 - applied) > 0.001) {
        m_statusLabel->setText(
            QString("Running at %1. %2% applies after a restart.").arg(percentText(applied)).arg(chosen));
    } else {
        m_statusLabel->setText(QString("Running at %1.").arg(percentText(applied)));
    }
}
