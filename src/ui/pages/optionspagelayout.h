#ifndef OPTIONSPAGELAYOUT_H
#define OPTIONSPAGELAYOUT_H

#include <QString>

class QComboBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QScrollArea;
class QSlider;
class QVBoxLayout;
class QWidget;

/**
 * @brief The one layout standard every Options page is built with: margins, spacing, the page
 *        title, sections, label/field forms, help text, field widths and the scroll area that holds
 *        the page. Geometry comes from the `Options*` tokens in K4Styles::Dimensions, text styles
 *        from the type roles, so the pages cannot drift apart.
 */
namespace OptionsPageLayout {

/// Page background plus the standard top-level layout: margins and row spacing.
QVBoxLayout *createPage(QWidget *page);

/// Page title (Indicator role, amber) with the rule under it.
QLabel *addTitle(QVBoxLayout *layout, QWidget *page, const QString &text);

/// One-paragraph description of the page under the title (Readout role, secondary, wrapped).
QLabel *addDescription(QVBoxLayout *layout, QWidget *page, const QString &text);

/// Section header (Heading role, bold) with the section gap above it.
QLabel *addSection(QVBoxLayout *layout, QWidget *page, const QString &text);

/// Section header as a spanning form row, so the sections of one page share one label column.
QLabel *addSection(QFormLayout *form, QWidget *page, const QString &text);

/// Help text under the field above it, in the field column.
QLabel *addFieldHelp(QFormLayout *form, QWidget *page, const QString &text);

/// A label/field form with the same geometry on every platform, added to @p layout.
QFormLayout *addForm(QVBoxLayout *layout);

/// Applies the form standard to an existing QFormLayout.
void configureForm(QFormLayout *form);

/// Adds a row with a styled form label; returns the label.
QLabel *addRow(QFormLayout *form, QWidget *page, const QString &label, QWidget *field);

/// Wrapped help text (Body role, italic, secondary).
QLabel *helpText(const QString &text, QWidget *parent);
QLabel *addHelp(QVBoxLayout *layout, QWidget *page, const QString &text);

/// Slider readout, wide enough for "1000 ms" so the slider does not move as the value changes.
QLabel *sliderValue(const QString &text, QWidget *parent);

/// A slider and its readout as one form field.
QWidget *sliderField(QSlider *slider, QLabel *value, QWidget *parent);

/// Lets a growable field use the column up to OptionsFieldMaxWidth, never past it.
void capWidth(QWidget *field, int maxWidth);

/// For lists of device names: sized by a character count instead of the longest name, so one long
/// device name cannot widen the page.
void fitDeviceCombo(QComboBox *combo);

/// The scroll area every page sits in.
QScrollArea *wrapInScrollArea(QWidget *page, QWidget *parent);

/// Style and repaint behaviour of the dialog's page list.
void configureNavList(QListWidget *list);

} // namespace OptionsPageLayout

#endif // OPTIONSPAGELAYOUT_H
