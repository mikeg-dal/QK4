#ifndef FEATUREMENUBAR_H
#define FEATUREMENUBAR_H

#include <QWidget>
#include <QPushButton>
#include "wheelaccumulator.h"

class FeatureMenuBar : public QWidget {
    Q_OBJECT

public:
    enum Feature { Attenuator, NbLevel, NrAdjust, ManualNotch };

    explicit FeatureMenuBar(QWidget *parent = nullptr);

    void showForFeature(Feature feature);
    void showAboveWidget(QWidget *referenceWidget); // Position popup above a reference widget
    void hideMenu();
    Feature currentFeature() const { return m_currentFeature; }
    bool isMenuVisible() const { return isVisible(); }

    // State updates (for Phase 2 RadioState integration)
    void setFeatureEnabled(bool enabled);
    void setValue(int value);
    void setValueUnit(const QString &unit);
    void setNbFilter(int filter); // 0=NONE, 1=NARROW, 2=WIDE

signals:
    void toggleRequested();
    void incrementRequested();
    void decrementRequested();
    void extraButtonClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateForFeature();

    QPushButton *m_titleLabel;
    QPushButton *m_toggleBtn;
    QPushButton *m_extraBtn;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;

    Feature m_currentFeature = Attenuator;
    bool m_featureEnabled = false;
    int m_value = 0;
    QString m_valueUnit;
    int m_nbFilter = 0;                   // 0=NONE, 1=NARROW, 2=WIDE
    QWidget *m_referenceWidget = nullptr; // Widget to position relative to
    WheelAccumulator m_wheelAccumulator;
};

#endif // FEATUREMENUBAR_H
