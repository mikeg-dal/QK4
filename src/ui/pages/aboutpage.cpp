#include "ui/pages/aboutpage.h"
#include "ui/styling/k4styles.h"
#include "models/radiostate.h"
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QMap>

namespace {
QStringList decodeOptionModules(const QString &om) {
    QStringList options;
    if (om.length() > 0 && om[0] == 'A')
        options << "KAT4 (ATU)";
    if (om.length() > 1 && om[1] == 'P')
        options << "KPA4 (PA)";
    if (om.length() > 2 && om[2] == 'X')
        options << "XVTR";
    if (om.length() > 3 && om[3] == 'S')
        options << "KRX4 (Sub RX)";
    if (om.length() > 4 && om[4] == 'H')
        options << "KHDR4 (HDR)";
    if (om.length() > 5 && om[5] == 'M')
        options << "K40 (Mini)";
    if (om.length() > 6 && om[6] == 'L')
        options << "Linear Amp";
    if (om.length() > 7 && om[7] == '1')
        options << "KPA1500";
    return options;
}
} // namespace

AboutPage::AboutPage(RadioState *radioState, QWidget *parent) : QWidget(parent), m_radioState(radioState) {
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "Connected Radio");

    auto *form = OptionsPageLayout::addForm(layout);
    m_radioIdLabel = new QLabel(QString(), this);
    m_radioIdLabel->setStyleSheet(K4Styles::Dialog::formValue());
    OptionsPageLayout::addRow(form, this, "Radio ID", m_radioIdLabel);
    m_radioModelLabel = new QLabel(QString(), this);
    m_radioModelLabel->setStyleSheet(K4Styles::Dialog::formValue());
    OptionsPageLayout::addRow(form, this, "Model", m_radioModelLabel);

    OptionsPageLayout::addSection(layout, this, "Installed Options");
    m_optionsWidget = new QWidget(this);
    auto *optionsItemLayout = new QVBoxLayout(m_optionsWidget);
    optionsItemLayout->setContentsMargins(0, 0, 0, 0);
    optionsItemLayout->setSpacing(K4Styles::Dimensions::PaddingSmall);
    layout->addWidget(m_optionsWidget);

    OptionsPageLayout::addSection(layout, this, "Software Versions");
    m_versionsWidget = new QWidget(this);
    auto *versionsForm = new QFormLayout(m_versionsWidget);
    OptionsPageLayout::configureForm(versionsForm);
    versionsForm->setVerticalSpacing(K4Styles::Dimensions::PaddingSmall);
    layout->addWidget(m_versionsWidget);

    layout->addStretch();
}

void AboutPage::refresh() {
    if (!m_radioIdLabel)
        return;

    m_radioIdLabel->setText(m_radioState ? m_radioState->radioID() : "Not connected");
    m_radioModelLabel->setText(m_radioState ? m_radioState->radioModel() : "Unknown");

    // Rebuild options list
    QLayout *optLayout = m_optionsWidget->layout();
    while (QLayoutItem *item = optLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (m_radioState && !m_radioState->optionModules().isEmpty()) {
        QStringList options = decodeOptionModules(m_radioState->optionModules());
        if (options.isEmpty()) {
            auto *label = new QLabel("No additional options", m_optionsWidget);
            label->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                     .arg(K4Styles::Colors::TextSecondary)
                                     .arg(K4Styles::Dimensions::FontSizeReadout));
            optLayout->addWidget(label);
        } else {
            for (const QString &opt : options) {
                auto *label = new QLabel(QString::fromUtf8("\u2022 ") + opt, m_optionsWidget);
                label->setStyleSheet(QString("color: %1; font-size: %2px;")
                                         .arg(K4Styles::Colors::TextPrimary)
                                         .arg(K4Styles::Dimensions::FontSizeReadout));
                optLayout->addWidget(label);
            }
        }
    } else {
        auto *label = new QLabel("Not connected", m_optionsWidget);
        label->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(K4Styles::Colors::TextSecondary)
                                 .arg(K4Styles::Dimensions::FontSizeReadout));
        optLayout->addWidget(label);
    }

    // Rebuild firmware versions list
    auto *verLayout = static_cast<QFormLayout *>(m_versionsWidget->layout());
    while (verLayout->rowCount() > 0)
        verLayout->removeRow(0);

    const QString nameStyle =
        K4Styles::Dialog::labelText(K4Styles::Colors::TextSecondary, K4Styles::Dimensions::FontSizeReadout);
    const QString valueStyle =
        K4Styles::Dialog::labelText(K4Styles::Colors::TextPrimary, K4Styles::Dimensions::FontSizeReadout);
    if (m_radioState && !m_radioState->firmwareVersions().isEmpty()) {
        static const QMap<QString, QString> componentNames = {
            {"DDC0", "DDC 0"},   {"DDC1", "DDC 1"},    {"DUC", "DUC"},   {"FP", "Front Panel"}, {"DSP", "DSP"},
            {"RFB", "RF Board"}, {"REF", "Reference"}, {"DAP", "DAP"},   {"KSRV", "K Server"},  {"KUI", "K UI"},
            {"KUP", "K Update"}, {"KCFG", "K Config"}, {"R", "Revision"}};
        const QMap<QString, QString> versions = m_radioState->firmwareVersions();
        for (auto it = versions.constBegin(); it != versions.constEnd(); ++it) {
            auto *nameLabel = new QLabel(componentNames.value(it.key(), it.key()), m_versionsWidget);
            nameLabel->setStyleSheet(nameStyle);
            auto *valueLabel = new QLabel(it.value(), m_versionsWidget);
            valueLabel->setStyleSheet(valueStyle);
            verLayout->addRow(nameLabel, valueLabel);
        }
    } else {
        auto *label = new QLabel("Connect to a radio to view version information", m_versionsWidget);
        label->setStyleSheet(nameStyle + QStringLiteral(" font-style: italic;"));
        verLayout->addRow(label);
    }
}
