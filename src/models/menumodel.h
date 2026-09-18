#ifndef MENUMODEL_H
#define MENUMODEL_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

// Single menu item from MEDF response
struct MenuItem {
    int id = 0;       // Menu ID (e.g., 7)
    QString name;     // "AGC Hold Time" (URL decoded)
    QString category; // "RX AGC"
    QString type;     // "BIN", "DEC", "SN", etc.
    int flag = 0;     // 0=normal, 1=enabled, 2=read-only
    int minValue = 0;
    int maxValue = 0;
    int defaultValue = 0;
    int currentValue = 0;
    int step = 1;
    QStringList options; // For selection types: ["OFF", "ON"]

    // Helper methods
    bool isReadOnly() const { return flag == 2; }

    QString displayValue() const {
        if (!options.isEmpty() && currentValue >= 0 && currentValue < options.size()) {
            return options.at(currentValue);
        }
        // K4 encodes the unit in the type field. Format accordingly so external
        // displays show "+0 Hz" / "144 MHz" / "1.0 mW" instead of raw integers.
        if (type == "Hz") {
            const QString sign = currentValue >= 0 ? "+" : "";
            return QString("%1%2 Hz").arg(sign).arg(currentValue);
        }
        if (type == "MHz") {
            return QString("%1 MHz").arg(currentValue);
        }
        if (type == "ms") {
            return QString("%1 ms").arg(currentValue);
        }
        if (type == "D10mW") {
            return QString("%1 mW").arg(currentValue / 10.0, 0, 'f', 1);
        }
        if (type == "M50Hz") {
            // K4 stores FM deviation as a count of 50 Hz steps.
            return QString("%1 Hz").arg(currentValue * 50);
        }
        if (type == "ZMON" || type == "ZOFF") {
            // K4 "Z" types: 0 means OFF; non-zero is the raw integer level.
            return currentValue == 0 ? QStringLiteral("OFF") : QString::number(currentValue);
        }
        return QString::number(currentValue);
    }
};

class MenuModel : public QObject {
    Q_OBJECT

public:
    // Synthetic menu IDs (negative to distinguish from real K4 menu IDs)
    static constexpr int SYNTHETIC_DISPLAY_FPS_ID = -1;

    // K4 menu ID whose current value substitutes for the "<n>" placeholder in
    // XVTR Band labels (IDs 76, 77, 78, 79, 98). Range 1..12.
    static constexpr int XVTR_BAND_SELECT_ID = 86;

    explicit MenuModel(QObject *parent = nullptr);

    // Add synthetic "Display FPS" menu item (app-specific, not from K4 MEDF)
    void addSyntheticDisplayFpsItem(int currentValue);
    void updateValue(int menuId, int value);

    // Access menu items
    MenuItem *getMenuItem(int menuId);
    const MenuItem *getMenuItem(int menuId) const;
    MenuItem *getMenuItemByName(const QString &name);
    const MenuItem *getMenuItemByName(const QString &name) const;
    QVector<MenuItem *> getAllItems();
    QVector<MenuItem *> getItemsByCategory(const QString &category);
    QVector<MenuItem *> filterByName(const QString &pattern);
    QStringList getCategories() const;

    int count() const { return m_items.size(); }
    void clear();

    // MEDF line format: MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;
    bool parseMEDF(const QString &medfLine);

    // Parse ME value update
    // Format: ME0007.0123;
    bool parseME(const QString &meLine);

    // Substitute "<n>" in item.name with the current XVTR Band Select value (1..12).
    // Items without "<n>" return their raw name unchanged. Defaults to "1" if the
    // selector hasn't been received from the K4 yet.
    QString resolvedName(const MenuItem &item) const;

signals:
    void menuItemAdded(int menuId);
    void menuValueChanged(int menuId, int newValue);

    // Every MenuItem this model handed out is about to be destroyed. Anything holding a
    // MenuItem * — the menu overlay's row widgets do — must drop it on this signal, because the
    // pointers dangle the moment clear() returns.
    void modelCleared();

private:
    // Add/update menu items (internal — called from parseMEDF)
    void addMenuItem(const MenuItem &item);

    QMap<int, MenuItem> m_items; // menuId -> MenuItem

    // URL decode helper (%2C -> ,)
    static QString urlDecode(const QString &str);
};

#endif // MENUMODEL_H
