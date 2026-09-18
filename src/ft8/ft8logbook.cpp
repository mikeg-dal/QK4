#include "ft8logbook.h"
#include "ft8types.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
bool isQrzManagedField(const QString &field) {
    return field.startsWith("_QK4_") || field.startsWith("QRZCOM_QSO_UPLOAD_") || field == "APP_QRZLOG_LOGID";
}

bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
AdifRecord normalize(AdifRecord r) {
    for (const auto &key : {"CALL", "STATION_CALLSIGN", "MODE", "SUBMODE", "GRIDSQUARE", "MY_GRIDSQUARE"})
        if (r.contains(key))
            r[key] = r[key].trimmed().toUpper();
    if (r.value("MODE") == "FT4") {
        r["MODE"] = "MFSK";
        r["SUBMODE"] = "FT4";
    }
    if (r.value("MODE") == "FT8")
        r.remove("SUBMODE");
    if (r.value("TIME_ON").size() == 4)
        r["TIME_ON"] += "00";
    if (r.value("BAND").isEmpty() && r.contains("FREQ")) {
        const double hz = r["FREQ"].toDouble() * 1e6;
        if (std::isfinite(hz) && hz > 0 && hz < double(std::numeric_limits<qint64>::max()))
            r["BAND"] = Ft8::bandFor(qRound64(hz));
    }
    return r;
}
} // namespace
std::function<void(const QString &, int)> Ft8Logbook::contactAdded;
Ft8Logbook::Ft8Logbook(QString path) : m_path(std::move(path)) {
    m_loaded = m_path.isEmpty() || !QFile::exists(m_path);
}
QString Ft8Logbook::canonicalMode(const AdifRecord &r) {
    return r.value("SUBMODE").toUpper() == "FT4" ? "FT4" : r.value("MODE").toUpper();
}
QString Ft8Logbook::identity(const AdifRecord &record) {
    auto r = normalize(record);
    return r.value("CALL") + '|' + r.value("STATION_CALLSIGN") + '|' + r.value("QSO_DATE") + '|' + r.value("TIME_ON") +
           '|' + r.value("BAND").toLower() + '|' + canonicalMode(r);
}
QString Ft8Logbook::validate(const AdifRecord &record) {
    const auto r = normalize(record);
    // A general log can contain portable/prefixed calls and modes outside the
    // FT message packer's restricted callsign representation.
    static const QRegularExpression callPattern("^[A-Z0-9]+(?:/[A-Z0-9]+)*$");
    if (r.value("CALL").size() > 64 || !callPattern.match(r.value("CALL")).hasMatch())
        return "A valid station callsign is required.";
    if (r.value("QSO_DATE").size() != 8 || !QDate::fromString(r.value("QSO_DATE"), "yyyyMMdd").isValid())
        return "Invalid UTC QSO date.";
    if (r.value("TIME_ON").size() != 6 || !QTime::fromString(r.value("TIME_ON"), "HHmmss").isValid())
        return "Invalid UTC start time.";
    if (r.value("MODE").isEmpty())
        return "Mode is missing.";
    if (r.value("BAND").isEmpty() && r.value("FREQ").isEmpty())
        return "Band or frequency is required.";
    if (r.contains("FREQ")) {
        bool ok = false;
        double f = r.value("FREQ").toDouble(&ok);
        if (!ok || !std::isfinite(f) || f <= 0)
            return "Invalid frequency in MHz.";
    }
    return {};
}
bool Ft8Logbook::load(QString *error) {
    m_loaded = false;
    if (m_path.isEmpty() || !QFile::exists(m_path)) {
        m_loaded = true;
        return true;
    }
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, f.errorString());
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
        return fail(error, "The logbook could not be read; the original file was preserved.");
    QVector<AdifRecord> loaded;
    for (const auto &v : doc.array()) {
        if (!v.isObject())
            return fail(error, "Invalid logbook record.");
        AdifRecord r;
        const auto object = v.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            r[it.key()] = it.value().toString();
        if (!validate(r).isEmpty())
            return fail(error, "Invalid logbook record; original file preserved.");
        loaded.append(normalize(r));
    }
    m_records = loaded;
    m_loaded = true;
    return true;
}
bool Ft8Logbook::save(const QVector<AdifRecord> &records, QString *error) {
    if (!m_loaded)
        return fail(error, "The existing logbook must be read successfully before it can be changed.");
    if (!m_path.isEmpty()) {
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
            return fail(error, "Cannot create the logbook folder.");
        QJsonArray array;
        for (const auto &r : records) {
            QJsonObject o;
            for (auto it = r.begin(); it != r.end(); ++it)
                o[it.key()] = it.value();
            array.append(o);
        }
        QSaveFile file(m_path);
        const auto data = QJsonDocument(array).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            return fail(error, file.errorString());
    }
    m_records = records;
    return true;
}
bool Ft8Logbook::append(const AdifRecord &record, QString *error) {
    if (m_loaded && !m_path.isEmpty() && !load(error))
        return false;
    const auto r = normalize(record);
    const QString invalid = validate(r);
    if (!invalid.isEmpty())
        return fail(error, invalid);
    for (const auto &old : m_records)
        if (identity(old) == identity(r))
            return fail(error, "This contact is already logged.");
    auto next = m_records;
    next.append(r);
    if (!save(next, error))
        return false;
    if (!m_path.isEmpty() && r.value("APP_QK4_PRACTICE") != "Y" && contactAdded)
        contactAdded(m_path, next.size() - 1);
    return true;
}
bool Ft8Logbook::replace(int index, const AdifRecord &record, QString *error) {
    if (m_loaded && !m_path.isEmpty() && !load(error))
        return false;
    if (index < 0 || index >= m_records.size())
        return fail(error, "Contact no longer exists.");
    auto r = normalize(record);
    const auto &old = m_records[index];
    if (old.value("_QK4_QRZ_STATE") == "SENDING")
        return fail(error, "This contact is being sent to QRZ. Try editing after sending finishes.");
    // Upload status is managed by confirmed server responses, never the editor.
    for (auto it = r.begin(); it != r.end();) {
        if (isQrzManagedField(it.key()))
            it = r.erase(it);
        else
            ++it;
    }
    for (auto it = old.cbegin(); it != old.cend(); ++it)
        if (isQrzManagedField(it.key()))
            r[it.key()] = it.value();
    const auto qrzStatus = old.value("QRZCOM_QSO_UPLOAD_STATUS").toUpper();
    if ((qrzStatus == "Y" || qrzStatus == "M") && r != old)
        r["QRZCOM_QSO_UPLOAD_STATUS"] = "M";
    const auto invalid = validate(r);
    if (!invalid.isEmpty())
        return fail(error, invalid);
    for (int i = 0; i < m_records.size(); ++i)
        if (i != index && identity(m_records[i]) == identity(r))
            return fail(error, "This contact is already logged.");
    auto next = m_records;
    next[index] = r;
    return save(next, error);
}
bool Ft8Logbook::patchUpload(int index, const AdifRecord &fields, QString *error) {
    if (!load(error))
        return false;
    if (index < 0 || index >= m_records.size())
        return fail(error, "Contact no longer exists.");
    auto next = m_records;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (it.value().isEmpty())
            next[index].remove(it.key());
        else
            next[index][it.key()] = it.value();
    }
    return save(next, error);
}
bool Ft8Logbook::worked(const QString &call, const QString &band, const QString &mode) const {
    for (const auto &r : m_records)
        if (r.value("CALL").compare(call, Qt::CaseInsensitive) == 0 &&
            r.value("BAND").compare(band, Qt::CaseInsensitive) == 0 && canonicalMode(r) == mode)
            return true;
    return false;
}
AdifImport Ft8Logbook::parse(const QString &text) {
    AdifImport out;
    AdifRecord record;
    qsizetype pos = 0;
    int number = 1;
    const QRegularExpression tag("^([A-Za-z0-9_]+)(?::([0-9]+)(?::[A-Za-z])?)?$");
    while ((pos = text.indexOf('<', pos)) >= 0) {
        const auto end = text.indexOf('>', pos);
        if (end < 0) {
            out.errors << "Unterminated ADIF tag.";
            break;
        }
        auto match = tag.match(text.mid(pos + 1, end - pos - 1));
        pos = end + 1;
        if (!match.hasMatch()) {
            out.errors << "Invalid ADIF tag.";
            break;
        }
        const QString key = match.captured(1).toUpper();
        if (key == "EOH") {
            record.clear();
            continue;
        }
        if (key == "EOR") {
            const auto r = normalize(record);
            const auto invalid = validate(r);
            if (invalid.isEmpty())
                out.records << r;
            else
                out.errors << QString("Record %1: %2").arg(number).arg(invalid);
            ++number;
            record.clear();
            continue;
        }
        bool ok = false;
        const auto length = match.captured(2).toLongLong(&ok);
        if (!ok || length < 0 || length > text.size() - pos) {
            out.errors << "Invalid or truncated ADIF field length.";
            break;
        }
        // Imported ADIF cannot create private IDs or executable upload-queue state.
        if (!key.startsWith("_QK4_"))
            record[key] = text.mid(pos, length);
        pos += length;
    }
    if (!record.isEmpty())
        out.errors << "The final record has no EOR tag.";
    if (out.records.isEmpty() && out.errors.isEmpty())
        out.errors << "No ADIF contacts found.";
    return out;
}
AdifImport Ft8Logbook::preview(const QString &text) const {
    auto out = parse(text);
    QVector<AdifRecord> fresh;
    QSet<QString> seen;
    for (const auto &r : m_records)
        seen.insert(identity(r));
    for (const auto &r : out.records) {
        auto key = identity(r);
        if (seen.contains(key))
            ++out.duplicates;
        else {
            seen.insert(key);
            fresh.append(r);
        }
    }
    out.records = fresh;
    return out;
}
bool Ft8Logbook::importRecords(const AdifImport &preview, QString *error) {
    if (m_loaded && !m_path.isEmpty() && !load(error))
        return false;
    if (!preview.errors.isEmpty())
        return fail(error, "Resolve the import errors before importing.");
    auto next = m_records;
    QSet<QString> seen;
    for (const auto &r : next)
        seen.insert(identity(r));
    for (const auto &r : preview.records) {
        const auto invalid = validate(r);
        if (!invalid.isEmpty())
            return fail(error, invalid);
        if (!seen.contains(identity(r))) {
            auto imported = normalize(r);
            for (auto it = imported.begin(); it != imported.end();)
                if (it.key().startsWith("_QK4_"))
                    it = imported.erase(it);
                else
                    ++it;
            next.append(imported);
            seen.insert(identity(r));
        }
    }
    return save(next, error);
}
QString Ft8Logbook::encode(const QVector<AdifRecord> &records, QString *error) {
    QString text = "QK4 Mobile log\n<ADIF_VER:5>3.1.4<PROGRAMID:10>QK4 Mobile<EOH>\n";
    for (auto record : records) {
        record = normalize(record);
        for (auto it = record.begin(); it != record.end(); ++it) {
            if (it.key().startsWith("_QK4_"))
                continue; // Local queue metadata is not ADIF.
            for (QChar ch : it.value())
                if (ch.unicode() > 127) {
                    fail(error, "ADI export requires ASCII field values; edit the non-ASCII text first.");
                    return {};
                }
            if (!it.value().isEmpty())
                text += QString("<%1:%2>%3").arg(it.key()).arg(it.value().size()).arg(it.value());
        }
        text += "<EOR>\n";
    }
    return text;
}
QString Ft8Logbook::exportAdif(QString *error) const {
    return encode(m_records, error);
}
