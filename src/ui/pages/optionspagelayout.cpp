#include "ui/pages/optionspagelayout.h"
#include "ui/styling/k4styles.h"
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace OptionsPageLayout {

namespace {

namespace D = K4Styles::Dimensions;

// WHY repaint the whole list: the operator saw thin amber edges left behind on items that had been
// selected or hovered. On macOS a selection or hover change repaints only the old and new item
// rectangles (measured: two 40 px strips of a 598 px list), so anything the style draws past an
// item's edge - a focus ring, an antialiased edge at a fractional interface scale - stays on screen.
// Repainting the viewport covers every pixel an item can reach. A dozen rows; the cost is nothing.
class NavRepaintFilter : public QObject {
public:
    explicit NavRepaintFilter(QListWidget *list) : QObject(list), m_list(list) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        switch (event->type()) {
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
        case QEvent::HoverLeave:
        case QEvent::Leave:
        case QEvent::MouseButtonRelease:
            m_list->viewport()->update();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QListWidget *m_list;
};

} // namespace

QVBoxLayout *createPage(QWidget *page) {
    page->setStyleSheet(K4Styles::Dialog::pageBackground());
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(D::OptionsPageMargin, D::OptionsPageMargin, D::OptionsPageMargin, D::OptionsPageMargin);
    layout->setSpacing(D::OptionsRowSpacing);
    return layout;
}

QLabel *addTitle(QVBoxLayout *layout, QWidget *page, const QString &text) {
    auto *title = new QLabel(text, page);
    title->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(title);

    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(D::SeparatorHeight);
    layout->addWidget(line);
    return title;
}

QLabel *addDescription(QVBoxLayout *layout, QWidget *page, const QString &text) {
    auto *label = new QLabel(text, page);
    label->setStyleSheet(K4Styles::Dialog::labelText(K4Styles::Colors::TextSecondary, D::FontSizeReadout));
    label->setWordWrap(true);
    layout->addWidget(label);
    return label;
}

QLabel *addSection(QVBoxLayout *layout, QWidget *page, const QString &text) {
    layout->addSpacing(D::OptionsSectionGap);
    auto *label = new QLabel(text, page);
    label->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(label);
    return label;
}

QLabel *addSection(QFormLayout *form, QWidget *page, const QString &text) {
    auto *label = new QLabel(text, page);
    label->setStyleSheet(K4Styles::Dialog::sectionHeader());
    label->setContentsMargins(0, D::OptionsSectionGap, 0, 0);
    form->addRow(label);
    return label;
}

QLabel *addFieldHelp(QFormLayout *form, QWidget *page, const QString &text) {
    auto *label = helpText(text, page);
    form->addRow(QString(), label);
    return label;
}

void configureForm(QFormLayout *form) {
    // WHY every property is set: QFormLayout takes its defaults from the style. macOS right-aligns
    // the labels, keeps fields at their size hint and centres the form; Fusion and the Windows styles
    // left-align and grow the fields. Set explicitly, the page looks the same everywhere.
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(D::OptionsLabelGap);
    form->setVerticalSpacing(D::OptionsRowSpacing);
    form->setContentsMargins(0, 0, 0, 0);
}

QFormLayout *addForm(QVBoxLayout *layout) {
    auto *form = new QFormLayout();
    configureForm(form);
    layout->addLayout(form);
    return form;
}

QLabel *addRow(QFormLayout *form, QWidget *page, const QString &label, QWidget *field) {
    auto *l = new QLabel(label, page);
    l->setStyleSheet(K4Styles::Dialog::formLabel());
    l->setBuddy(field);
    form->addRow(l, field);
    return l;
}

QLabel *helpText(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(K4Styles::Dialog::helpText());
    // WHY: an unwrapped label's minimum width is its whole sentence. One of these on the Audio Input
    // page made the page wider than the dialog, clipping the text and the device list beside it.
    label->setWordWrap(true);
    // Expanding so that inside a form it takes the whole field column rather than the narrow width a
    // wrapped label prefers; the existing policy keeps the height-for-width that wrapping set.
    QSizePolicy policy = label->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    label->setSizePolicy(policy);
    return label;
}

QLabel *addHelp(QVBoxLayout *layout, QWidget *page, const QString &text) {
    auto *label = helpText(text, page);
    layout->addWidget(label);
    return label;
}

QLabel *sliderValue(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(K4Styles::Dialog::labelText(K4Styles::Colors::TextPrimary, D::FontSizeHeading));
    label->ensurePolished();
    label->setMinimumWidth(label->fontMetrics().horizontalAdvance(QStringLiteral("1000 ms")));
    return label;
}

QWidget *sliderField(QSlider *slider, QLabel *value, QWidget *parent) {
    auto *field = new QWidget(parent);
    auto *row = new QHBoxLayout(field);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(D::OptionsLabelGap);
    row->addWidget(slider, 1);
    row->addWidget(value);
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    capWidth(field, D::OptionsFieldMaxWidth);
    return field;
}

void capWidth(QWidget *field, int maxWidth) {
    field->setSizePolicy(QSizePolicy::Expanding, field->sizePolicy().verticalPolicy());
    field->setMaximumWidth(maxWidth);
}

void fitDeviceCombo(QComboBox *combo) {
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(D::OptionsDeviceComboChars);
    capWidth(combo, D::OptionsFieldMaxWidth);
}

QScrollArea *wrapInScrollArea(QWidget *page, QWidget *parent) {
    // WHY every page scrolls: a page taller than the dialog would otherwise be squeezed, its rows
    // crushed and labels clipped. A page that fits shows no scrollbar.
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // WHY a horizontal bar as needed rather than never: widgetResizable never makes the page
    // narrower than its minimum size, so with the bar switched off a page wider than the dialog was
    // silently cut off at the right. Pages are built to fit (test_optionspagelayout checks), and if
    // one ever does not, its right edge stays reachable instead of disappearing.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet(
        QString("QScrollArea { border: none; background: transparent; }"
                "QScrollBar:vertical { background: %1; width: 8px; }"
                "QScrollBar::handle:vertical { background: %2; border-radius: 4px; min-height: 20px; }"
                "QScrollBar:horizontal { background: %1; height: 8px; }"
                "QScrollBar::handle:horizontal { background: %2; border-radius: 4px; min-width: 20px; }"
                "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }")
            .arg(K4Styles::Colors::DarkBackground)
            .arg(K4Styles::Colors::BorderNormal));
    scroll->setWidget(page);
    return scroll;
}

void configureNavList(QListWidget *list) {
    // WHY outline none on the item as well as the view: the native styles draw a focus rectangle
    // around the current item, outside the rules below, and Windows adds its own hover fill. Every
    // state the list can be in is spelled out so no platform paints anything of its own here.
    list->setStyleSheet(
        QString("QListWidget { background-color: %1; color: %2; border: 1px solid %3; "
                "             font-size: %4px; outline: none; show-decoration-selected: 1; }"
                "QListWidget::item { padding: 10px 15px; border: none; border-bottom: 1px solid %3; "
                "                   background-color: %1; color: %2; outline: none; }"
                "QListWidget::item:hover { background-color: %5; }"
                "QListWidget::item:selected, QListWidget::item:selected:hover, "
                "QListWidget::item:selected:!active { background-color: %6; color: %1; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextPrimary, K4Styles::Colors::DialogBorder)
            .arg(D::FontSizeHeading)
            .arg(K4Styles::Colors::GradientBottom, K4Styles::Colors::AccentAmber));
    list->setFocusPolicy(Qt::StrongFocus);
    list->setUniformItemSizes(true);
    list->viewport()->setAttribute(Qt::WA_Hover);
    list->viewport()->installEventFilter(new NavRepaintFilter(list));
    QObject::connect(list, &QListWidget::currentRowChanged, list, [list]() { list->viewport()->update(); });
}

} // namespace OptionsPageLayout
