#ifndef DISPLAYPOPUPWIDGET_H
#define DISPLAYPOPUPWIDGET_H

#include "controlgroupwidget.h"
#include "displaymenubutton.h"
#include "k4popupbase.h"
#include "togglegroupwidget.h"
#include <QList>
#include <QStackedWidget>

class DisplayPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    enum MenuItem { PanWaterfall = 0, NbWtrClrs, RefLvlScale, SpanCenter, AveragePeak, FixedFreeze, CursAB };

    explicit DisplayPopupWidget(QWidget *parent = nullptr);

    MenuItem selectedItem() const { return m_selectedItem; }
    bool isLcdEnabled() const { return m_lcdEnabled; }
    bool isExtEnabled() const { return m_extEnabled; }
    bool isVfoAEnabled() const { return m_vfoAEnabled; }
    bool isVfoBEnabled() const { return m_vfoBEnabled; }

public slots:
    // Span VALUES are per-VFO (A and B can have different spans)
    void setSpanValueA(double spanKHz);
    void setSpanValueB(double spanKHz);
    void setRefLevelValue(int dB);      // Legacy: sets A
    void setAutoRefLevel(bool enabled); // GLOBAL - affects both VFOs
    void setScale(int scale);           // GLOBAL - affects both panadapters (10-150)
    // Ref level VALUES are per-VFO (A and B can have different levels)
    void setRefLevelValueA(int dB);
    void setRefLevelValueB(int dB);

    // State setters for RadioState updates (separate LCD and EXT)
    void setDualPanModeLcd(int mode);
    void setDualPanModeExt(int mode);
    void setDisplayModeLcd(int mode);
    void setDisplayModeExt(int mode);
    void setWaterfallColor(int color);
    void setAveraging(int value);
    void setPeakMode(bool enabled);
    void setFixedTuneMode(int fxt, int fxa);
    void setFreeze(bool enabled);
    void setVfoACursor(int mode);
    void setVfoBCursor(int mode);
    void setDdcNbMode(int mode);   // 0=OFF, 1=ON, 2=AUTO
    void setDdcNbLevel(int level); // 0-14

    // Waterfall height percentage (0-100, default 50 = 50/50 split)
    void setWaterfallHeight(int percent);    // LCD: #WFHxx;
    void setWaterfallHeightExt(int percent); // EXT: #HWFHxx;

signals:
    // Span control signals
    void spanIncrementRequested();
    void spanDecrementRequested();

    // Ref level control signals
    void refLevelIncrementRequested();
    void refLevelDecrementRequested();
    // Averaging control signals
    void averagingIncrementRequested();
    void averagingDecrementRequested();

    // Scale control signals (GLOBAL - applies to both panadapters)
    void scaleIncrementRequested();
    void scaleDecrementRequested();

    // DDC NB control signals
    void nbLevelIncrementRequested();
    void nbLevelDecrementRequested();

    // Waterfall height control signals
    void waterfallHeightIncrementRequested();
    void waterfallHeightDecrementRequested();

    // CAT command signal (for MainWindow to forward to TcpClient)
    void catCommandRequested(const QString &cmd);

    // Pan mode changed (for MainWindow to update panadapter display)
    // K4 doesn't echo #DPM commands, so we notify directly
    void dualPanModeChanged(int mode);

protected:
    QSize contentSize() const override;

private:
    void setupUi();
    void setupTopRow();
    void setupBottomRow();

    QWidget *createSpanControlPage();
    QWidget *createRefLevelControlPage();
    QWidget *createScaleControlPage();
    QWidget *createAverageControlPage();
    QWidget *createNbControlPage();
    QWidget *createWaterfallControlPage();
    QWidget *createDefaultControlPage();
    void updateNbControlGroupValue();
    void updateWaterfallControlGroup();

    void updateToggleStyles();
    void updateMenuButtonStyles();
    void updateMenuButtonLabels();
    void onMenuItemClicked(MenuItem item);
    void onMenuItemRightClicked(MenuItem item);

    // Helper to get command prefix based on LCD/EXT selection
    QString getCommandPrefix() const;

    // Target toggle groups (bordered containers with triangle)
    ToggleGroupWidget *m_lcdExtGroup;
    ToggleGroupWidget *m_vfoAbGroup;
    bool m_lcdEnabled = true;
    bool m_extEnabled = false;
    bool m_vfoAEnabled = true;
    bool m_vfoBEnabled = false;
    bool m_vfoBAvailable = true;

    // Context-dependent controls
    QStackedWidget *m_controlStack;
    QWidget *m_spanControlPage;
    QWidget *m_refLevelControlPage;
    QWidget *m_averageControlPage;
    QWidget *m_nbControlPage;
    QWidget *m_defaultControlPage;

    // Span controls
    ControlGroupWidget *m_spanControlGroup;

    // Ref level controls
    ControlGroupWidget *m_refLevelControlGroup;

    // Scale controls
    QWidget *m_scaleControlPage;
    ControlGroupWidget *m_scaleControlGroup;
    int m_scale = 75; // 10-150, default 75
    void updateScaleControlGroup();

    // Average controls
    ControlGroupWidget *m_averageControlGroup;

    // DDC NB controls
    ControlGroupWidget *m_nbControlGroup;

    // Waterfall height controls
    QWidget *m_waterfallControlPage;
    ControlGroupWidget *m_waterfallControlGroup;

    // Menu buttons
    QList<DisplayMenuButton *> m_menuButtons;
    MenuItem m_selectedItem = SpanCenter;

    // Span state - values are per-VFO (A and B can have different spans)
    double m_spanA = 100.0;
    double m_spanB = 100.0;

    // Helper to update span control group based on A/B selection
    void updateSpanControlGroup();

    // Ref level state
    // Values are per-VFO (A and B can have different levels)
    int m_refLevelA = -108;
    int m_refLevelB = -108;
    // Auto-ref mode is GLOBAL (affects both VFOs simultaneously)
    bool m_autoRef = true; // Default to auto

    // Helper to update ref level control group based on A/B selection
    void updateRefLevelControlGroup();

    // Display state tracking (for menu button labels and CAT commands)
    // Initial values are -1 to ensure first update from radio triggers label refresh
    // Separate LCD and EXT state
    int m_dualPanModeLcd = -1; // #DPM: LCD 0=A, 1=B, 2=Dual
    int m_dualPanModeExt = -1; // #HDPM: EXT 0=A, 1=B, 2=Dual
    int m_displayModeLcd = -1; // #DSM: LCD 0=spectrum, 1=spectrum+waterfall
    int m_displayModeExt = -1; // #HDSM: EXT 0=spectrum, 1=spectrum+waterfall
    int m_waterfallColor = -1; // 0-4
    int m_averaging = -1;      // 1-20
    int m_peakMode = -1;       // 0=off, 1=on (int for -1 init)
    int m_fixedTuneMode = -1;  // Combined FXT+FXA state (0-5: SLIDE1, SLIDE2, FIXED1, FIXED2, STATIC, TRACK)
    int m_freeze = -1;         // 0=run, 1=freeze (int for -1 init)
    int m_vfaMode = -1;        // 0=OFF, 1=ON, 2=AUTO, 3=HIDE
    int m_vfbMode = -1;

    // DDC NB state (#NB$ and #NBL$ commands)
    int m_ddcNbMode = -1;  // 0=OFF, 1=ON, 2=AUTO
    int m_ddcNbLevel = -1; // 0-14

    // Waterfall height state (#WFHxx and #HWFHxx commands)
    // Global setting - applies to both VFO A and B
    int m_waterfallHeight = 50;    // LCD: 0-100% (default 50%)
    int m_waterfallHeightExt = 50; // EXT: 0-100% (default 50%)
};

#endif // DISPLAYPOPUPWIDGET_H
