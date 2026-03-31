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
        return QString::number(currentValue);
    }
};

class MenuModel : public QObject {
    Q_OBJECT

public:
    // Synthetic menu IDs (negative to distinguish from real K4 menu IDs)
    static constexpr int SYNTHETIC_DISPLAY_FPS_ID = -1;

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

    // Get count
    int count() const { return m_items.size(); }

    // Clear all
    void clear();

    // Parse MEDF line from RDY response
    // Format: MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;
    bool parseMEDF(const QString &medfLine);

    // Parse ME value update
    // Format: ME0007.0123;
    bool parseME(const QString &meLine);

signals:
    void menuItemAdded(int menuId);
    void menuValueChanged(int menuId, int newValue);

private:
    // Add/update menu items (internal — called from parseMEDF)
    void addMenuItem(const MenuItem &item);

    QMap<int, MenuItem> m_items; // menuId -> MenuItem

    // URL decode helper (%2C -> ,)
    static QString urlDecode(const QString &str);
};

#endif // MENUMODEL_H
