#include "aboutpage.h"
#include "k4styles.h"
#include "../models/radiostate.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
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
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("Connected Radio", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Separator line
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Two-column layout for Radio Info and Installed Options
    auto *infoRow = new QHBoxLayout();
    infoRow->setSpacing(K4Styles::Dimensions::DialogMargin);

    // Left column widget: Radio ID and Model
    auto *leftWidget = new QWidget(this);
    auto *leftColumn = new QVBoxLayout(leftWidget);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    // Radio ID
    auto *idLayout = new QHBoxLayout();
    auto *idTitleLabel = new QLabel("Radio ID:", leftWidget);
    idTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    idTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_radioIdLabel = new QLabel(QString(), leftWidget);
    m_radioIdLabel->setStyleSheet(K4Styles::Dialog::formValue());

    idLayout->addWidget(idTitleLabel);
    idLayout->addWidget(m_radioIdLabel);
    idLayout->addStretch();
    leftColumn->addLayout(idLayout);

    // Radio Model
    auto *modelLayout = new QHBoxLayout();
    auto *modelTitleLabel = new QLabel("Model:", leftWidget);
    modelTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    modelTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_radioModelLabel = new QLabel(QString(), leftWidget);
    m_radioModelLabel->setStyleSheet(K4Styles::Dialog::formValue());

    modelLayout->addWidget(modelTitleLabel);
    modelLayout->addWidget(m_radioModelLabel);
    modelLayout->addStretch();
    leftColumn->addLayout(modelLayout);
    leftColumn->addStretch();

    // Vertical demarcation line
    auto *vline = new QFrame(this);
    vline->setFrameShape(QFrame::VLine);
    vline->setFrameShadow(QFrame::Plain);
    vline->setStyleSheet(K4Styles::Dialog::separator());
    vline->setFixedWidth(K4Styles::Dimensions::SeparatorHeight);

    // Right column widget: Installed Options
    auto *rightWidget = new QWidget(this);
    auto *rightColumn = new QVBoxLayout(rightWidget);
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(K4Styles::Dimensions::PaddingSmall);

    auto *optionsTitle = new QLabel("Installed Options", rightWidget);
    optionsTitle->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    rightColumn->addWidget(optionsTitle);

    m_optionsWidget = new QWidget(rightWidget);
    auto *optionsItemLayout = new QVBoxLayout(m_optionsWidget);
    optionsItemLayout->setContentsMargins(0, 0, 0, 0);
    optionsItemLayout->setSpacing(K4Styles::Dimensions::PaddingSmall);
    rightColumn->addWidget(m_optionsWidget);
    rightColumn->addStretch();

    // Assemble the info row
    infoRow->addWidget(leftWidget, 1);
    infoRow->addWidget(vline);
    infoRow->addWidget(rightWidget, 1);

    layout->addLayout(infoRow);

    // Software Versions section
    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    auto *versionsTitle = new QLabel("Software Versions", this);
    versionsTitle->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::AccentAmber)
                                     .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(versionsTitle);

    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    m_versionsWidget = new QWidget(this);
    auto *versionsItemLayout = new QVBoxLayout(m_versionsWidget);
    versionsItemLayout->setContentsMargins(0, 0, 0, 0);
    versionsItemLayout->setSpacing(K4Styles::Dimensions::PaddingSmall);
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
                                     .arg(K4Styles::Colors::TextGray)
                                     .arg(K4Styles::Dimensions::FontSizeButton));
            optLayout->addWidget(label);
        } else {
            for (const QString &opt : options) {
                auto *label = new QLabel(QString::fromUtf8("\u2022 ") + opt, m_optionsWidget);
                label->setStyleSheet(QString("color: %1; font-size: %2px;")
                                         .arg(K4Styles::Colors::TextWhite)
                                         .arg(K4Styles::Dimensions::FontSizeButton));
                optLayout->addWidget(label);
            }
        }
    } else {
        auto *label = new QLabel("Not connected", m_optionsWidget);
        label->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
        optLayout->addWidget(label);
    }

    // Rebuild firmware versions list
    QLayout *verLayout = m_versionsWidget->layout();
    while (QLayoutItem *item = verLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (m_radioState && !m_radioState->firmwareVersions().isEmpty()) {
        static const QMap<QString, QString> componentNames = {
            {"DDC0", "DDC 0"},   {"DDC1", "DDC 1"},    {"DUC", "DUC"},   {"FP", "Front Panel"}, {"DSP", "DSP"},
            {"RFB", "RF Board"}, {"REF", "Reference"}, {"DAP", "DAP"},   {"KSRV", "K Server"},  {"KUI", "K UI"},
            {"KUP", "K Update"}, {"KCFG", "K Config"}, {"R", "Revision"}};
        const QMap<QString, QString> versions = m_radioState->firmwareVersions();
        for (auto it = versions.constBegin(); it != versions.constEnd(); ++it) {
            auto *row = new QWidget(m_versionsWidget);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            auto *nameLabel = new QLabel(componentNames.value(it.key(), it.key()) + ":", row);
            nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                         .arg(K4Styles::Colors::TextGray)
                                         .arg(K4Styles::Dimensions::FontSizeButton));
            nameLabel->setFixedWidth(K4Styles::Dimensions::InputFieldWidthMedium);
            auto *valueLabel = new QLabel(it.value(), row);
            valueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                          .arg(K4Styles::Colors::TextWhite)
                                          .arg(K4Styles::Dimensions::FontSizeButton));
            rowLayout->addWidget(nameLabel);
            rowLayout->addWidget(valueLabel);
            rowLayout->addStretch();
            verLayout->addWidget(row);
        }
    } else {
        auto *label = new QLabel("Connect to a radio to view version information", m_versionsWidget);
        label->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
        verLayout->addWidget(label);
    }
}
