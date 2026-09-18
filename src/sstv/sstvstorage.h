#ifndef SSTVSTORAGE_H
#define SSTVSTORAGE_H

#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct SstvRxRecord {
    QString id;
    QString imagePath;
    QString metadataPath;
    QDateTime receivedUtc;
    qint64 frequencyHz = 0;
    int modeId = -1;
    QString modeName;
    QString slantStatus;
    QString callsign;
    QString callsignSource;
    int callsignConfidence = 0;
    QString fskIdCandidate;
    QString cwIdCandidate;
    QString note;
    bool starred = false;
};

// App-private SSTV persistence. The optional root is used by deterministic
// tests; production uses QStandardPaths::AppDataLocation/sstv.
class SstvStorage {
public:
    explicit SstvStorage(const QString &rootPath = QString());

    QString rootPath() const { return m_rootPath; }
    void setRetentionLimit(int limit) { m_retentionLimit = limit; }
    int retentionLimit() const { return m_retentionLimit; }
    bool applyRetentionLimit(int limit, QString *error = nullptr);

    bool saveReceived(const QImage &image, int modeId, const QString &modeName, const QString &slantStatus,
                      qint64 frequencyHz, SstvRxRecord *savedRecord = nullptr, QString *error = nullptr);
    QVector<SstvRxRecord> received(QString *error = nullptr) const;
    bool setStarred(const QString &id, bool starred, QString *error = nullptr);
    bool setCallsign(const QString &id, const QString &callsign, const QString &source, int confidence,
                     QString *error = nullptr);
    bool removeReceived(const QString &id, QString *error = nullptr);
    bool clearUnstarred(QString *error = nullptr);

    bool saveDraft(const QImage &sourceImage, const QJsonObject &state, QString *error = nullptr,
                   bool sourceChanged = true);
    bool loadDraft(QImage *sourceImage, QJsonObject *state, QString *error = nullptr) const;
    bool clearDraft(QString *error = nullptr);

    QStringList userTemplateNames(QString *error = nullptr) const;
    bool saveUserTemplate(const QString &name, const QJsonObject &state, QString *error = nullptr);
    bool saveUserTemplate(const QString &name, const QJsonObject &state, const QImage &sourceImage,
                          QString *error = nullptr);
    bool loadUserTemplate(const QString &name, QJsonObject *state, QString *error = nullptr) const;
    bool loadUserTemplate(const QString &name, QJsonObject *state, QImage *sourceImage, QString *error = nullptr) const;
    bool removeUserTemplate(const QString &name, QString *error = nullptr);
    bool resetUserTemplates(QString *error = nullptr);

    QStringList imageTemplateNames(QString *error = nullptr) const;
    bool saveImageTemplate(const QString &name, const QImage &sourceImage, const QImage &previewImage,
                           const QJsonObject &state, QString *error = nullptr);
    bool loadImageTemplate(const QString &name, QImage *sourceImage, QImage *previewImage, QJsonObject *state,
                           QString *error = nullptr) const;
    bool removeImageTemplate(const QString &name, QString *error = nullptr);

private:
    QString rxPath() const;
    QString draftPath() const;
    QString templatesPath() const;
    QString templatePath(const QString &name) const;
    QString templateImagePath(const QString &name) const;
    QString imageTemplatesPath() const;
    QString imageTemplatePath(const QString &name) const;
    bool ensureDirectory(const QString &path, QString *error) const;
    bool writeJson(const QString &path, const QJsonObject &object, QString *error) const;
    bool readJson(const QString &path, QJsonObject *object, QString *error) const;
    bool writeImage(const QString &path, const QImage &image, QString *error) const;
    bool writeRecordMetadata(const SstvRxRecord &record, QString *error) const;
    bool enforceRetention(QString *error);
    static SstvRxRecord recordFromJson(const QJsonObject &object, const QString &metadataPath);
    static QJsonObject recordToJson(const SstvRxRecord &record);
    static QString safeModeName(const QString &modeName);

    QString m_rootPath;
    int m_retentionLimit = 50;
};

#endif // SSTVSTORAGE_H
