#pragma once
#include <QMap>
#include <QVector>
#include <QString>
#include <functional>

using AdifRecord = QMap<QString, QString>;
struct AdifImport {
    QVector<AdifRecord> records;
    QStringList errors;
    int duplicates = 0;
};
class Ft8Logbook {
public:
    // Application installs this hook; in-memory/practice logs never upload.
    static std::function<void(const QString &, int)> contactAdded;
    explicit Ft8Logbook(QString path = {});
    bool load(QString *error = nullptr);
    bool append(const AdifRecord &record, QString *error = nullptr);
    bool replace(int index, const AdifRecord &record, QString *error = nullptr);
    AdifImport preview(const QString &text) const;
    bool importRecords(const AdifImport &preview, QString *error = nullptr);
    QString exportAdif(QString *error = nullptr) const;
    const QVector<AdifRecord> &records() const { return m_records; }
    bool worked(const QString &call, const QString &band, const QString &mode) const;
    static AdifImport parse(const QString &text);
    static QString encode(const QVector<AdifRecord> &records, QString *error = nullptr);
    static QString identity(const AdifRecord &record);
    static QString canonicalMode(const AdifRecord &record);
    static QString validate(const AdifRecord &record);

private:
    friend class QrzLogbook;
    bool patchUpload(int index, const AdifRecord &fields, QString *error);
    bool save(const QVector<AdifRecord> &records, QString *error);
    QString m_path;
    bool m_loaded = false;
    QVector<AdifRecord> m_records;
};
