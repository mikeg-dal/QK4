#include "ui/pages/stationpage.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
#include <QVBoxLayout>

StationPage::StationPage(QWidget *parent) : QWidget(parent) {
    namespace D = K4Styles::Dimensions;
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "Station");

    RadioSettings *settings = RadioSettings::instance();
    auto *form = OptionsPageLayout::addForm(layout);

    auto addField = [&](const QString &labelText, QLineEdit *edit, const QString &placeholder, int maxWidth) {
        edit->setStyleSheet(K4Styles::Dialog::lineEdit());
        edit->setPlaceholderText(placeholder);
        OptionsPageLayout::capWidth(edit, maxWidth);
        OptionsPageLayout::addRow(form, this, labelText, edit);
    };

    m_callSignEdit = new QLineEdit(this);
    m_callSignEdit->setText(settings->callSign());
    addField("Call Sign", m_callSignEdit, "e.g. KF5O", D::OptionsShortFieldWidth);

    m_operatorNameEdit = new QLineEdit(this);
    m_operatorNameEdit->setText(settings->operatorName());
    addField("Operator Name", m_operatorNameEdit, "Your name", D::OptionsFieldMaxWidth);

    m_gridSquareEdit = new QLineEdit(this);
    m_gridSquareEdit->setText(settings->gridSquare());
    addField("Grid Square", m_gridSquareEdit, "e.g. EM12", D::OptionsShortFieldWidth);

    m_qthEdit = new QLineEdit(this);
    m_qthEdit->setText(settings->qth());
    addField("QTH / Location", m_qthEdit, "City / location", D::OptionsFieldMaxWidth);

    // IARU Region dropdown — drives the band-plan overlay.
    m_regionCombo = new QComboBox(this);
    m_regionCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_regionCombo->addItem("Region 1 — Europe / Africa", 1);
    m_regionCombo->addItem("Region 2 — The Americas", 2);
    m_regionCombo->addItem("Region 3 — Asia-Pacific", 3);
    m_regionCombo->addItem("United States (FCC)", 4);
    int regionIdx = m_regionCombo->findData(settings->iaruRegion());
    m_regionCombo->setCurrentIndex(regionIdx >= 0 ? regionIdx : 1);
    OptionsPageLayout::addRow(form, this, "IARU Region", m_regionCombo);

    // Band-plan overlay toggle.
    m_bandPlanCheck = new QCheckBox("Show band-plan overlay on spectrum", this);
    m_bandPlanCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    m_bandPlanCheck->setChecked(settings->bandPlanOverlayEnabled());
    form->addRow(QString(), m_bandPlanCheck);

    OptionsPageLayout::addHelp(
        layout, this,
        "Sets the band-plan strip at the top of the spectrum. IARU regions are voluntary "
        "recommendations; \"United States (FCC)\" uses practical Extra-class mode boundaries "
        "plus FT8/FT4/WSPR markers. Your national regulations and license class take precedence.");

    layout->addStretch();

    // Persist on edit. Text fields commit on editingFinished (focus-out / Enter) to avoid
    // re-saving on every keystroke.
    connect(m_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &StationPage::onIaruRegionChanged);
    connect(m_callSignEdit, &QLineEdit::editingFinished, this, &StationPage::onCallSignEdited);
    connect(m_gridSquareEdit, &QLineEdit::editingFinished, this, &StationPage::onGridSquareEdited);
    connect(m_operatorNameEdit, &QLineEdit::editingFinished, this, &StationPage::onOperatorNameEdited);
    connect(m_qthEdit, &QLineEdit::editingFinished, this, &StationPage::onQthEdited);
    connect(m_bandPlanCheck, &QCheckBox::toggled, this, &StationPage::onBandPlanOverlayToggled);
}

void StationPage::onIaruRegionChanged(int index) {
    if (index < 0)
        return;
    RadioSettings::instance()->setIaruRegion(m_regionCombo->itemData(index).toInt());
}

void StationPage::onCallSignEdited() {
    RadioSettings::instance()->setCallSign(m_callSignEdit->text());
    // Reflect the normalized (upper-cased/trimmed) value back into the field.
    m_callSignEdit->setText(RadioSettings::instance()->callSign());
}

void StationPage::onGridSquareEdited() {
    RadioSettings::instance()->setGridSquare(m_gridSquareEdit->text());
}

void StationPage::onOperatorNameEdited() {
    RadioSettings::instance()->setOperatorName(m_operatorNameEdit->text());
}

void StationPage::onQthEdited() {
    RadioSettings::instance()->setQth(m_qthEdit->text());
}

void StationPage::onBandPlanOverlayToggled(bool checked) {
    RadioSettings::instance()->setBandPlanOverlayEnabled(checked);
}
