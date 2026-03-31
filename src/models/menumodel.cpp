#include "menumodel.h"
#include <QSet>
#include <QUrl>
#include <algorithm>

MenuModel::MenuModel(QObject *parent) : QObject(parent) {}

void MenuModel::addMenuItem(const MenuItem &item) {
    m_items[item.id] = item;
    emit menuItemAdded(item.id);
}

void MenuModel::updateValue(int menuId, int value) {
    if (m_items.contains(menuId)) {
        if (m_items[menuId].currentValue != value) {
            m_items[menuId].currentValue = value;
            emit menuValueChanged(menuId, value);
        }
    }
}

void MenuModel::addSyntheticDisplayFpsItem(int currentValue) {
    MenuItem item;
    item.id = SYNTHETIC_DISPLAY_FPS_ID;
    item.name = "Display FPS";
    item.category = "APP SETTINGS";
    item.type = "DEC";
    item.flag = 0;
    item.minValue = 12;
    item.maxValue = 30;
    item.defaultValue = 30;
    item.currentValue = currentValue;
    item.step = 1;
    addMenuItem(item);
}

MenuItem *MenuModel::getMenuItem(int menuId) {
    if (m_items.contains(menuId)) {
        return &m_items[menuId];
    }
    return nullptr;
}

const MenuItem *MenuModel::getMenuItem(int menuId) const {
    auto it = m_items.find(menuId);
    if (it != m_items.end()) {
        return &it.value();
    }
    return nullptr;
}

MenuItem *MenuModel::getMenuItemByName(const QString &name) {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it.value().name == name) {
            return &it.value();
        }
    }
    return nullptr;
}

const MenuItem *MenuModel::getMenuItemByName(const QString &name) const {
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        if (it.value().name == name) {
            return &it.value();
        }
    }
    return nullptr;
}

QVector<MenuItem *> MenuModel::getAllItems() {
    QVector<MenuItem *> result;
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        result.append(&it.value());
    }
    // Sort alphabetically by name
    std::sort(result.begin(), result.end(),
              [](MenuItem *a, MenuItem *b) { return a->name.toLower() < b->name.toLower(); });
    return result;
}

QVector<MenuItem *> MenuModel::getItemsByCategory(const QString &category) {
    QVector<MenuItem *> result;
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it.value().category == category) {
            result.append(&it.value());
        }
    }
    // Sort alphabetically by name
    std::sort(result.begin(), result.end(),
              [](MenuItem *a, MenuItem *b) { return a->name.toLower() < b->name.toLower(); });
    return result;
}

QVector<MenuItem *> MenuModel::filterByName(const QString &pattern) {
    if (pattern.isEmpty()) {
        return getAllItems();
    }
    QVector<MenuItem *> result;
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it.value().name.contains(pattern, Qt::CaseInsensitive)) {
            result.append(&it.value());
        }
    }
    // Sort alphabetically by name
    std::sort(result.begin(), result.end(),
              [](MenuItem *a, MenuItem *b) { return a->name.toLower() < b->name.toLower(); });
    return result;
}

QStringList MenuModel::getCategories() const {
    QSet<QString> categories;
    for (const auto &item : m_items) {
        categories.insert(item.category);
    }
    QStringList result = categories.values();
    result.sort();
    return result;
}

void MenuModel::clear() {
    m_items.clear();
}

QString MenuModel::urlDecode(const QString &str) {
    return QUrl::fromPercentEncoding(str.toUtf8());
}

bool MenuModel::parseMEDF(const QString &medfLine) {
    // Format: MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1[,option1,option2,...];
    QString line = medfLine.trimmed();
    if (!line.startsWith("MEDF")) {
        return false;
    }

    // Remove trailing semicolon
    if (line.endsWith(";")) {
        line.chop(1);
    }

    // Split by comma
    QStringList parts = line.split(',');
    if (parts.size() < 10) {
        return false;
    }

    MenuItem item;

    // Parse menu ID from MEDF0007
    QString idStr = parts[0].mid(4); // Remove "MEDF"
    bool ok;
    item.id = idStr.toInt(&ok);
    if (!ok) {
        return false;
    }

    // Parse name (URL encoded)
    item.name = urlDecode(parts[1]);

    // Parse category
    item.category = parts[2];

    // Parse type
    item.type = parts[3];

    // Parse flag
    item.flag = parts[4].toInt();

    // Parse min
    item.minValue = parts[5].toInt();

    // Parse max
    item.maxValue = parts[6].toInt();

    // Parse default
    item.defaultValue = parts[7].toInt();

    // Parse current value
    item.currentValue = parts[8].toInt();

    // Parse step
    item.step = parts[9].toInt();

    // Parse options (if any)
    for (int i = 10; i < parts.size(); ++i) {
        item.options.append(urlDecode(parts[i]));
    }

    // Add to model
    addMenuItem(item);

    return true;
}

bool MenuModel::parseME(const QString &meLine) {
    // Format: ME0007.0123;
    QString line = meLine.trimmed();
    if (!line.startsWith("ME") || line.startsWith("MEDF")) {
        return false;
    }

    // Remove trailing semicolon
    if (line.endsWith(";")) {
        line.chop(1);
    }

    // Find the dot separator
    int dotPos = line.indexOf('.');
    if (dotPos == -1) {
        return false;
    }

    // Parse menu ID
    QString idStr = line.mid(2, dotPos - 2); // Skip "ME", up to dot
    bool ok;
    int menuId = idStr.toInt(&ok);
    if (!ok) {
        return false;
    }

    // Parse value
    QString valueStr = line.mid(dotPos + 1);
    int value = valueStr.toInt(&ok);
    if (!ok) {
        return false;
    }

    // Update the value
    updateValue(menuId, value);

    return true;
}
