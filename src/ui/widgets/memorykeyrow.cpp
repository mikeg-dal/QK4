#include "ui/widgets/memorykeyrow.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/sidepanelbutton.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace MemoryKeyRow {

namespace D = K4Styles::Dimensions;

QHBoxLayout *create(QWidget *parent, int rowWidth, Keys &keys) {
    auto createMemoryButton = [parent](const QString &label, const QString &subLabel,
                                       SidePanelButton::Shade shade) -> QWidget * {
        QPushButton *btn = nullptr;
        return SidePanelButton::createWithSubLabel(label, subLabel, shade, btn, parent);
    };

    // Single row: M1-M4 group, REC, STORE, RCL (all centered)
    auto *memoryRow = new QHBoxLayout();
    memoryRow->setContentsMargins(0, 0, 0, 0);
    memoryRow->setSpacing(D::SideButtonColumnGap);

    memoryRow->addStretch();

    // M1-M4 group with MESSAGE label underneath
    auto *messageGroup = new QWidget(parent);
    auto *messageGroupLayout = new QVBoxLayout(messageGroup);
    messageGroupLayout->setContentsMargins(0, 0, 0, 0);
    messageGroupLayout->setSpacing(D::SideButtonSubLabelGap);

    // M1-M4 button row
    auto *m1m4Row = new QHBoxLayout();
    m1m4Row->setContentsMargins(0, 0, 0, 0);
    m1m4Row->setSpacing(D::SideButtonColumnGap);

    auto createSimpleButton = [parent](const QString &label) -> QPushButton * {
        return SidePanelButton::create(label, SidePanelButton::Shade::Dark, parent);
    };

    keys.m1 = createSimpleButton("M1");
    m1m4Row->addWidget(keys.m1);

    keys.m2 = createSimpleButton("M2");
    m1m4Row->addWidget(keys.m2);

    keys.m3 = createSimpleButton("M3");
    m1m4Row->addWidget(keys.m3);

    keys.m4 = createSimpleButton("M4");
    m1m4Row->addWidget(keys.m4);

    messageGroupLayout->addLayout(m1m4Row);

    // MESSAGE label with connecting lines: ——— MESSAGE ———
    auto *messageLabel = new QWidget(messageGroup);
    auto *messageLabelLayout = new QHBoxLayout(messageLabel);
    messageLabelLayout->setContentsMargins(0, 0, 0, 0);
    messageLabelLayout->setSpacing(2);

    auto *leftLine = new QFrame(messageLabel);
    leftLine->setFrameShape(QFrame::HLine);
    leftLine->setStyleSheet(QString("background-color: %1; max-height: 1px;").arg(K4Styles::Colors::BorderSelected));
    leftLine->setFixedHeight(D::SeparatorHeight);

    auto *msgText = new QLabel("MESSAGE", messageLabel);
    msgText->setStyleSheet(
        QString("color: %1; font-size: %2px;").arg(K4Styles::Colors::BorderSelected).arg(D::FontSizeCaption));
    msgText->setAlignment(Qt::AlignCenter);
    // WHY: one sub-label line tall, so MESSAGE sits on the same line as BANK / AF REC / AF PLAY.
    msgText->setFixedHeight(SidePanelButton::subLabelHeight(msgText));

    auto *rightLine = new QFrame(messageLabel);
    rightLine->setFrameShape(QFrame::HLine);
    rightLine->setStyleSheet(QString("background-color: %1; max-height: 1px;").arg(K4Styles::Colors::BorderSelected));
    rightLine->setFixedHeight(D::SeparatorHeight);

    messageLabelLayout->addWidget(leftLine, 1);
    messageLabelLayout->addWidget(msgText, 0);
    messageLabelLayout->addWidget(rightLine, 1);

    messageGroupLayout->addWidget(messageLabel);
    memoryRow->addWidget(messageGroup);

    // REC (dark grey like M keys, BANK sub-label)
    auto *recContainer = createMemoryButton("REC", "BANK", SidePanelButton::Shade::Dark);
    keys.rec = recContainer->findChild<QPushButton *>();
    memoryRow->addWidget(recContainer);

    // STORE (lighter grey, AF REC sub-label)
    auto *storeContainer = createMemoryButton("STORE", "AF REC", SidePanelButton::Shade::Light);
    keys.store = storeContainer->findChild<QPushButton *>();
    memoryRow->addWidget(storeContainer);

    // RCL (lighter grey, AF PLAY sub-label)
    auto *rclContainer = createMemoryButton("RCL", "AF PLAY", SidePanelButton::Shade::Light);
    keys.rcl = rclContainer->findChild<QPushButton *>();
    memoryRow->addWidget(rclContainer);

    // The memory keys are side-panel keys narrowed to fit seven across between the VFOs, in two sets
    // that each share one width. REC / STORE / RCL take the width their widest text needs (STORE);
    // M1-M4 share what is left of the column, so the row still spans it.
    // WHY two widths: one width from STORE for all seven would not fit the column, and the column
    // is what keeps the VFOs apart.
    const int recallWidth = SidePanelButton::keyWidthFor({keys.rec, keys.store, keys.rcl});
    const int messageWidth = qMax(SidePanelButton::keyWidthFor({keys.m1, keys.m2, keys.m3, keys.m4}),
                                  (rowWidth - 3 * recallWidth - 6 * D::SideButtonColumnGap) / 4);
    for (QPushButton *key : {keys.rec, keys.store, keys.rcl})
        key->setFixedWidth(recallWidth);
    for (QPushButton *key : {keys.m1, keys.m2, keys.m3, keys.m4})
        key->setFixedWidth(messageWidth);

    memoryRow->addStretch();
    return memoryRow;
}

} // namespace MemoryKeyRow
