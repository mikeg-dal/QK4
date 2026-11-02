#include "menumodel.h"

#include "models/radiostate/spectrumdisplaystate.h"
#include <QUrl>
#include <QtMath>
#include <algorithm>
#include <utility>

MenuModel::MenuModel(QObject *parent) : QObject(parent) {}

QString MenuModel::resolvedName(const MenuItem &item) const {
    static const QString kPlaceholder = QStringLiteral("<n>");
    if (!item.name.contains(kPlaceholder)) {
        return item.name;
    }
    auto it = m_items.constFind(XVTR_BAND_SELECT_ID);
    const int band = (it != m_items.constEnd()) ? it->currentValue : 1;
    QString resolved = item.name;
    return resolved.replace(kPlaceholder, QString::number(band));
}

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
    item.minValue = SpectrumDisplayLimits::FpsMin;
    item.maxValue = SpectrumDisplayLimits::FpsMax;
    item.defaultValue = currentValue; // the K4 states no default; the operator's saved rate
    item.currentValue = currentValue;
    item.step = 1;
    addMenuItem(item);
}

const MenuItem *MenuModel::getMenuItem(int menuId) const {
    auto it = m_items.constFind(menuId);
    return it != m_items.constEnd() ? &it.value() : nullptr;
}

QList<int> MenuModel::itemIds(const QString &nameFilter) const {
    QList<const MenuItem *> matches;
    for (const MenuItem &item : m_items) {
        if (nameFilter.isEmpty() || item.name.contains(nameFilter, Qt::CaseInsensitive)) {
            matches.append(&item);
        }
    }
    std::sort(matches.begin(), matches.end(),
              [](const MenuItem *a, const MenuItem *b) { return a->name.compare(b->name, Qt::CaseInsensitive) < 0; });
    QList<int> ids;
    ids.reserve(matches.size());
    for (const MenuItem *item : std::as_const(matches)) {
        ids.append(item->id);
    }
    return ids;
}

void MenuModel::clear() {
    m_items.clear();
    emit modelCleared();
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

namespace MenuValueEntry {

bool acceptsTyping(const MenuItem &item) {
    return !item.isReadOnly() && item.options.isEmpty() && item.type != QLatin1String("BIN") &&
           item.maxValue > item.minValue;
}

bool allowsNegative(const MenuItem &item) {
    return item.minValue < 0;
}

bool allowsDecimal(const MenuItem &item) {
    return item.type == QLatin1String("D10mW");
}

QString unitSuffix(const MenuItem &item) {
    if (item.type == QLatin1String("MHz"))
        return QStringLiteral(" MHz");
    if (item.type == QLatin1String("Hz") || item.type == QLatin1String("M50Hz"))
        return QStringLiteral(" Hz");
    if (item.type == QLatin1String("ms"))
        return QStringLiteral(" ms");
    if (item.type == QLatin1String("D10mW"))
        return QStringLiteral(" mW");
    return QString();
}

std::optional<int> rawFromTyped(const MenuItem &item, const QString &typed) {
    bool ok = false;
    int raw = 0;
    if (allowsDecimal(item)) {
        const double mw = typed.toDouble(&ok); // D10mW: tenths of a milliwatt
        raw = qRound(mw * 10.0);
    } else if (item.type == QLatin1String("M50Hz")) {
        const int hz = typed.toInt(&ok); // FM deviation, stored as 50 Hz steps
        raw = qRound(hz / 50.0);
    } else {
        raw = typed.toInt(&ok);
    }
    if (!ok)
        return std::nullopt;
    if (item.step > 1) // onto the item's own grid, counted from its minimum
        raw = item.minValue + qRound(double(raw - item.minValue) / item.step) * item.step;
    return std::clamp(raw, item.minValue, item.maxValue);
}

} // namespace MenuValueEntry
