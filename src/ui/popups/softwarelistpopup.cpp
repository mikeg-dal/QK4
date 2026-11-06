#include "ui/popups/softwarelistpopup.h"

#include "ui/styling/k4styles.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {
const int ContentMargin = 16;
const int TitleHeight = 26;
const int TitleGap = 14;
const int RowHeight = 22;
const int RowVGap = 2;
const int LabelColWidth = 58;
const int ValueColWidth = 110;
const int LabelValueGap = 6;
const int ColPairGap = 28;

const QString Placeholder = QStringLiteral("—"); // em dash

// Row order: {CAT key, front-panel label}. Left column rows 0-5, right
// column rows 6-11 — matching the K4's own "Software List" screen.
//
// WHY: the K4 reports DDCs zero-indexed over CAT (DDC0/DDC1) but the front
// panel labels them one-indexed (DDC1/DDC2); CAT "RFB" displays as "RF".
struct VersionRow {
    const char *catKey;
    const char *label;
};
const VersionRow kRows[12] = {
    {"KUI", "KUI"}, {"KSRV", "KSRV"}, {"KUP", "KUP"},   {"KCFG", "KCFG"}, {"FP", "FP"},   {"RFB", "RF"},
    {"DSP", "DSP"}, {"DAP", "DAP"},   {"DDC0", "DDC1"}, {"DDC1", "DDC2"}, {"DUC", "DUC"}, {"REF", "REF"},
};
} // namespace

SoftwareListPopupWidget::SoftwareListPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup(); // must be last
}

void SoftwareListPopupWidget::setupUi() {
    const int sm = K4Styles::Dimensions::ShadowMargin;

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(sm + ContentMargin, sm + ContentMargin, sm + ContentMargin, sm + ContentMargin);
    outer->setSpacing(TitleGap);

    m_titleLabel = new QLabel("SOFTWARE LIST", this);
    m_titleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_titleLabel->setFixedHeight(TitleHeight);
    m_titleLabel->setStyleSheet(
        K4Styles::Dialog::labelTextBold(K4Styles::Colors::TextPrimary, K4Styles::Dimensions::FontSizeHeading));
    outer->addWidget(m_titleLabel);

    const QString labelStyle =
        K4Styles::Dialog::labelText(K4Styles::Colors::TextSecondary, K4Styles::Dimensions::FontSizeReadout);
    const QString valueStyle =
        K4Styles::Dialog::labelText(K4Styles::Colors::TextPrimary, K4Styles::Dimensions::FontSizeReadout);

    auto *columns = new QHBoxLayout();
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(ColPairGap);

    for (int col = 0; col < 2; ++col) {
        auto *grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(LabelValueGap);
        grid->setVerticalSpacing(RowVGap);

        for (int gridRow = 0; gridRow < 6; ++gridRow) {
            const int i = col * 6 + gridRow;

            auto *nameLabel = new QLabel(kRows[i].label, this);
            nameLabel->setStyleSheet(labelStyle);
            nameLabel->setFixedSize(LabelColWidth, RowHeight);

            auto *valueLabel = new QLabel(Placeholder, this);
            valueLabel->setStyleSheet(valueStyle);
            valueLabel->setFixedSize(ValueColWidth, RowHeight);
            m_valueLabels[i] = valueLabel;

            grid->addWidget(nameLabel, gridRow, 0);
            grid->addWidget(valueLabel, gridRow, 1);
        }
        columns->addLayout(grid);
    }

    outer->addLayout(columns);
}

void SoftwareListPopupWidget::setVersions(const QMap<QString, QString> &versions) {
    const QString rev = versions.value(QStringLiteral("R"));
    m_titleLabel->setText(rev.isEmpty() ? QStringLiteral("SOFTWARE LIST")
                                        : QStringLiteral("SOFTWARE LIST ( %1 )").arg(rev));

    for (int i = 0; i < 12; ++i) {
        const QString value = versions.value(QString::fromLatin1(kRows[i].catKey));
        m_valueLabels[i]->setText(value.isEmpty() ? Placeholder : value);
    }
}

QSize SoftwareListPopupWidget::contentSize() const {
    const int pairWidth = LabelColWidth + LabelValueGap + ValueColWidth;
    const int width = ContentMargin + pairWidth + ColPairGap + pairWidth + ContentMargin;
    const int gridHeight = 6 * RowHeight + 5 * RowVGap;
    const int height = ContentMargin + TitleHeight + TitleGap + gridHeight + ContentMargin;
    return QSize(width, height);
}
