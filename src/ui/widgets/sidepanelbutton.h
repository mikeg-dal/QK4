#ifndef SIDEPANELBUTTON_H
#define SIDEPANELBUTTON_H

#include "ui/styling/k4constants.h"

#include <QSize>
#include <QString>
#include <QList>

class QBoxLayout;
class QGridLayout;
class QLabel;
class QPushButton;
class QWidget;

// The one builder for side-panel buttons: left and right panels, the memory row, the KPA1500
// mini-panel. Geometry comes from the K4Styles::Dimensions::Side* tokens; the shade picks colour only.
namespace SidePanelButton {

enum class Shade { Dark, Light };

inline constexpr QSize kFunctionSize{K4Styles::Dimensions::SideButtonWidth, K4Styles::Dimensions::SideButtonHeight};

// A fixed-size button with the shade's sheet and Qt::WA_LayoutUsesWidgetRect set.
QPushButton *create(const QString &text, Shade shade, QWidget *parent, QSize size = kFunctionSize);

// The button with its orange sub-label below, in a container that holds nothing else. An empty
// sub-label still reserves its line so rows stay aligned.
QWidget *createWithSubLabel(const QString &text, const QString &subText, Shade shade, QPushButton *&btnOut,
                            QWidget *parent, QSize size = kFunctionSize);

// One line of sub-label text tall, measured from the font it is drawn with.
QLabel *createSubLabel(const QString &text, QWidget *parent);
int subLabelHeight(const QWidget *context);

// The narrowest width that holds the text of every one of `keys` (lines split at '\n'), measured in
// the font each key is drawn with once its style sheet applies, with
// K4Styles::Dimensions::ButtonTextClearance inside a border of `borderWidth` on each side. Keys that
// must look alike take one width from all their texts together.
int keyWidthFor(const QList<QPushButton *> &keys, int borderWidth = K4Styles::Dimensions::BorderWidth);

// Adds a gap of exactly `gap` px between the items on either side of it. WHY: QBoxLayout still puts its
// spacing() between two items a spacer separates, so addSpacing(gap) alone draws gap + spacing
// (measured: addSpacing(16) in a 4 px layout drew 20).
void addGap(QBoxLayout *layout, int gap);

// Two equal columns of function buttons with the standard column and row gaps, no margins. Add it to
// its parent layout aligned Qt::AlignHCenter so it keeps its own width in a wider panel.
QGridLayout *createGrid();

} // namespace SidePanelButton

#endif // SIDEPANELBUTTON_H
