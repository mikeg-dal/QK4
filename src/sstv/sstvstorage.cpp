#include "sstvstorage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {
void setError(QString *error, const QString &message) {
    if (error)
        *error = message;
}
} // namespace

SstvStorage::SstvStorage(const QString &rootPath)
    : m_rootPath(
          rootPath.isEmpty()
              ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("sstv"))
              : QDir::cleanPath(rootPath)) {}

bool SstvStorage::applyRetentionLimit(int limit, QString *error) {
    m_retentionLimit = limit;
    return enforceRetention(error);
}

QString SstvStorage::rxPath() const {
    return QDir(m_rootPath).filePath(QStringLiteral("rx"));
}

QString SstvStorage::draftPath() const {
    return QDir(m_rootPath).filePath(QStringLiteral("tx-draft"));
}

QString SstvStorage::templatesPath() const {
    return QDir(m_rootPath).filePath(QStringLiteral("templates"));
}

QString SstvStorage::templatePath(const QString &name) const {
    const QByteArray digest = QCryptographicHash::hash(name.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(templatesPath()).filePath(QString::fromLatin1(digest) + QStringLiteral(".json"));
}

QString SstvStorage::templateImagePath(const QString &name) const {
    const QByteArray digest = QCryptographicHash::hash(name.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(templatesPath()).filePath(QString::fromLatin1(digest) + QStringLiteral(".png"));
}

QString SstvStorage::imageTemplatesPath() const {
    return QDir(m_rootPath).filePath(QStringLiteral("image-templates"));
}

QString SstvStorage::imageTemplatePath(const QString &name) const {
    const QByteArray digest =
        QCryptographicHash::hash(name.trimmed().toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(imageTemplatesPath()).filePath(QString::fromLatin1(digest));
}

bool SstvStorage::ensureDirectory(const QString &path, QString *error) const {
    if (QDir().mkpath(path))
        return true;
    setError(error, QStringLiteral("Could not create SSTV storage at %1.").arg(path));
    return false;
}

bool SstvStorage::writeJson(const QString &path, const QJsonObject &object, QString *error) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

bool SstvStorage::readJson(const QString &path, QJsonObject *object, QString *error) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Invalid SSTV metadata: %1").arg(parseError.errorString()));
        return false;
    }
    if (object)
        *object = document.object();
    return true;
}

bool SstvStorage::writeImage(const QString &path, const QImage &image, QString *error) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    QImageWriter writer(&file, "png");
    writer.setCompression(6);
    if (!writer.write(image) || !file.commit()) {
        setError(error, writer.errorString().isEmpty() ? file.errorString() : writer.errorString());
        return false;
    }
    return true;
}

QString SstvStorage::safeModeName(const QString &modeName) {
    QString result = modeName.toUpper();
    for (QChar &character : result) {
        if (!character.isLetterOrNumber())
            character = QLatin1Char('_');
    }
    while (result.contains(QStringLiteral("__")))
        result.replace(QStringLiteral("__"), QStringLiteral("_"));
    return result.isEmpty() ? QStringLiteral("AUTO") : result.left(24);
}

QJsonObject SstvStorage::recordToJson(const SstvRxRecord &record) {
    return {{QStringLiteral("version"), 2},
            {QStringLiteral("id"), record.id},
            {QStringLiteral("imageFile"), QFileInfo(record.imagePath).fileName()},
            {QStringLiteral("receivedUtc"), record.receivedUtc.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("frequencyHz"), QString::number(record.frequencyHz)},
            {QStringLiteral("modeId"), record.modeId},
            {QStringLiteral("modeName"), record.modeName},
            {QStringLiteral("slantStatus"), record.slantStatus},
            {QStringLiteral("callsign"), record.callsign},
            {QStringLiteral("callsignSource"), record.callsignSource},
            {QStringLiteral("callsignConfidence"), record.callsignConfidence},
            {QStringLiteral("fskIdCandidate"), record.fskIdCandidate},
            {QStringLiteral("cwIdCandidate"), record.cwIdCandidate},
            {QStringLiteral("note"), record.note},
            {QStringLiteral("starred"), record.starred}};
}

SstvRxRecord SstvStorage::recordFromJson(const QJsonObject &object, const QString &metadataPath) {
    SstvRxRecord record;
    record.id = object.value(QStringLiteral("id")).toString();
    record.metadataPath = metadataPath;
    record.imagePath =
        QDir(QFileInfo(metadataPath).absolutePath()).filePath(object.value(QStringLiteral("imageFile")).toString());
    record.receivedUtc =
        QDateTime::fromString(object.value(QStringLiteral("receivedUtc")).toString(), Qt::ISODateWithMs);
    record.frequencyHz = object.value(QStringLiteral("frequencyHz")).toString().toLongLong();
    record.modeId = object.value(QStringLiteral("modeId")).toInt(-1);
    record.modeName = object.value(QStringLiteral("modeName")).toString();
    record.slantStatus = object.value(QStringLiteral("slantStatus")).toString();
    record.callsign = object.value(QStringLiteral("callsign")).toString();
    record.callsignSource = object.value(QStringLiteral("callsignSource")).toString();
    record.callsignConfidence = object.value(QStringLiteral("callsignConfidence")).toInt();
    record.fskIdCandidate = object.value(QStringLiteral("fskIdCandidate")).toString();
    record.cwIdCandidate = object.value(QStringLiteral("cwIdCandidate")).toString();
    record.note = object.value(QStringLiteral("note")).toString();
    record.starred = object.value(QStringLiteral("starred")).toBool(false);
    return record;
}

bool SstvStorage::writeRecordMetadata(const SstvRxRecord &record, QString *error) const {
    return writeJson(record.metadataPath, recordToJson(record), error);
}

bool SstvStorage::saveReceived(const QImage &image, int modeId, const QString &modeName, const QString &slantStatus,
                               qint64 frequencyHz, SstvRxRecord *savedRecord, QString *error) {
    if (image.isNull()) {
        setError(error, QStringLiteral("The completed SSTV image is empty."));
        return false;
    }
    if (!ensureDirectory(rxPath(), error))
        return false;

    SstvRxRecord record;
    record.receivedUtc = QDateTime::currentDateTimeUtc();
    const QString timestamp = record.receivedUtc.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString modeToken = safeModeName(modeName);
    record.id = timestamp + QStringLiteral("_") + modeToken + QStringLiteral("_") +
                QUuid::createUuid().toString(QUuid::Id128).left(8);
    record.modeId = modeId;
    record.modeName = modeName;
    record.slantStatus = slantStatus;
    record.frequencyHz = frequencyHz;
    const QString exportBase =
        QStringLiteral("SSTV_%1_%2").arg(record.receivedUtc.toString(QStringLiteral("yyyyMMdd_HHmmss")), modeToken);
    record.imagePath = QDir(rxPath()).filePath(exportBase + QStringLiteral(".png"));
    for (int suffix = 2; QFileInfo::exists(record.imagePath); ++suffix)
        record.imagePath = QDir(rxPath()).filePath(exportBase + QStringLiteral("_%1.png").arg(suffix));
    record.metadataPath = QDir(rxPath()).filePath(QStringLiteral("SSTV_%1.json").arg(record.id));

    if (!writeImage(record.imagePath, image, error))
        return false;
    if (!writeRecordMetadata(record, error)) {
        QFile::remove(record.imagePath);
        return false;
    }
    if (!enforceRetention(error))
        return false;
    if (savedRecord)
        *savedRecord = record;
    return true;
}

QVector<SstvRxRecord> SstvStorage::received(QString *error) const {
    QVector<SstvRxRecord> result;
    const QDir directory(rxPath());
    if (!directory.exists())
        return result;
    const QFileInfoList files = directory.entryInfoList({QStringLiteral("SSTV_*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        QJsonObject object;
        QString readError;
        if (!readJson(file.absoluteFilePath(), &object, &readError)) {
            if (error && error->isEmpty())
                *error = readError;
            continue;
        }
        SstvRxRecord record = recordFromJson(object, file.absoluteFilePath());
        if (!record.id.isEmpty() && QFileInfo::exists(record.imagePath))
            result.append(record);
    }
    std::sort(result.begin(), result.end(),
              [](const SstvRxRecord &a, const SstvRxRecord &b) { return a.receivedUtc > b.receivedUtc; });
    return result;
}

bool SstvStorage::setStarred(const QString &id, bool starred, QString *error) {
    QVector<SstvRxRecord> records = received(error);
    for (SstvRxRecord &record : records) {
        if (record.id == id) {
            record.starred = starred;
            return writeRecordMetadata(record, error);
        }
    }
    setError(error, QStringLiteral("The selected SSTV image is no longer available."));
    return false;
}

bool SstvStorage::setCallsign(const QString &id, const QString &callsign, const QString &source, int confidence,
                              QString *error) {
    QVector<SstvRxRecord> records = received(error);
    for (SstvRxRecord &record : records) {
        if (record.id == id) {
            record.callsign = callsign.trimmed().toUpper();
            record.callsignSource = source.trimmed().toUpper();
            record.callsignConfidence = qBound(0, confidence, 100);
            if (record.callsignSource == QStringLiteral("FSK ID"))
                record.fskIdCandidate = record.callsign;
            else if (record.callsignSource == QStringLiteral("CW ID"))
                record.cwIdCandidate = record.callsign;
            return writeRecordMetadata(record, error);
        }
    }
    setError(error, QStringLiteral("The selected SSTV image is no longer available."));
    return false;
}

bool SstvStorage::removeReceived(const QString &id, QString *error) {
    const QVector<SstvRxRecord> records = received(error);
    for (const SstvRxRecord &record : records) {
        if (record.id != id)
            continue;
        const bool imageRemoved = !QFileInfo::exists(record.imagePath) || QFile::remove(record.imagePath);
        const bool metadataRemoved = !QFileInfo::exists(record.metadataPath) || QFile::remove(record.metadataPath);
        if (imageRemoved && metadataRemoved)
            return true;
        setError(error, QStringLiteral("Could not remove the selected SSTV image."));
        return false;
    }
    return true;
}

bool SstvStorage::clearUnstarred(QString *error) {
    const QVector<SstvRxRecord> records = received(error);
    for (const SstvRxRecord &record : records) {
        if (!record.starred && !removeReceived(record.id, error))
            return false;
    }
    return true;
}

bool SstvStorage::enforceRetention(QString *error) {
    if (m_retentionLimit <= 0)
        return true;
    QVector<SstvRxRecord> records = received(error);
    while (records.size() > m_retentionLimit) {
        int candidate = -1;
        for (int i = records.size() - 1; i >= 0; --i) {
            if (!records.at(i).starred) {
                candidate = i;
                break;
            }
        }
        if (candidate < 0)
            break;
        if (!removeReceived(records.at(candidate).id, error))
            return false;
        records.removeAt(candidate);
    }
    return true;
}

bool SstvStorage::saveDraft(const QImage &sourceImage, const QJsonObject &state, QString *error, bool sourceChanged) {
    if (sourceImage.isNull()) {
        setError(error, QStringLiteral("The SSTV draft has no source image."));
        return false;
    }
    if (!ensureDirectory(draftPath(), error))
        return false;
    const QString imagePath = QDir(draftPath()).filePath(QStringLiteral("source.png"));
    if ((sourceChanged || !QFileInfo::exists(imagePath)) && !writeImage(imagePath, sourceImage, error))
        return false;
    QJsonObject document = state;
    document.insert(QStringLiteral("version"), 1);
    document.insert(QStringLiteral("sourceFile"), QStringLiteral("source.png"));
    document.insert(QStringLiteral("savedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return writeJson(QDir(draftPath()).filePath(QStringLiteral("draft.json")), document, error);
}

bool SstvStorage::loadDraft(QImage *sourceImage, QJsonObject *state, QString *error) const {
    const QString metadataPath = QDir(draftPath()).filePath(QStringLiteral("draft.json"));
    if (!QFileInfo::exists(metadataPath))
        return false;
    QJsonObject document;
    if (!readJson(metadataPath, &document, error))
        return false;
    const QString imagePath = QDir(draftPath()).filePath(document.value(QStringLiteral("sourceFile")).toString());
    QImageReader reader(imagePath);
    const QImage image = reader.read();
    if (image.isNull()) {
        setError(error, reader.errorString());
        return false;
    }
    if (sourceImage)
        *sourceImage = image;
    if (state)
        *state = document;
    return true;
}

bool SstvStorage::clearDraft(QString *error) {
    const QDir directory(draftPath());
    if (!directory.exists())
        return true;
    for (const QFileInfo &file : directory.entryInfoList(QDir::Files)) {
        if (!QFile::remove(file.absoluteFilePath())) {
            setError(error, QStringLiteral("Could not remove the saved SSTV draft."));
            return false;
        }
    }
    return true;
}

QStringList SstvStorage::userTemplateNames(QString *error) const {
    QStringList result;
    const QDir directory(templatesPath());
    if (!directory.exists())
        return result;
    for (const QFileInfo &file : directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QJsonObject object;
        if (readJson(file.absoluteFilePath(), &object, error)) {
            const QString name = object.value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                result.append(name);
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool SstvStorage::saveUserTemplate(const QString &name, const QJsonObject &state, QString *error) {
    return saveUserTemplate(name, state, QImage(), error);
}

bool SstvStorage::saveUserTemplate(const QString &name, const QJsonObject &state, const QImage &sourceImage,
                                   QString *error) {
    const QString cleanName = name.trimmed().left(40);
    if (cleanName.isEmpty()) {
        setError(error, QStringLiteral("Enter a template name."));
        return false;
    }
    if (!ensureDirectory(templatesPath(), error))
        return false;
    const QString imagePath = templateImagePath(cleanName);
    if (!sourceImage.isNull() && !writeImage(imagePath, sourceImage, error))
        return false;
    QJsonObject document = state;
    document.insert(QStringLiteral("version"), 2);
    document.insert(QStringLiteral("name"), cleanName);
    if (!sourceImage.isNull())
        document.insert(QStringLiteral("sourceFile"), QFileInfo(imagePath).fileName());
    else
        document.remove(QStringLiteral("sourceFile"));
    if (!writeJson(templatePath(cleanName), document, error))
        return false;
    if (sourceImage.isNull() && QFileInfo::exists(imagePath) && !QFile::remove(imagePath)) {
        setError(error, QStringLiteral("Could not remove the image from the SSTV template."));
        return false;
    }
    return true;
}

bool SstvStorage::loadUserTemplate(const QString &name, QJsonObject *state, QString *error) const {
    return loadUserTemplate(name, state, nullptr, error);
}

bool SstvStorage::loadUserTemplate(const QString &name, QJsonObject *state, QImage *sourceImage, QString *error) const {
    QJsonObject document;
    if (!readJson(templatePath(name), &document, error))
        return false;
    if (sourceImage) {
        *sourceImage = QImage();
        const QString fileName = document.value(QStringLiteral("sourceFile")).toString();
        if (!fileName.isEmpty()) {
            QImageReader reader(QDir(templatesPath()).filePath(QFileInfo(fileName).fileName()));
            const QImage image = reader.read();
            if (image.isNull()) {
                setError(error, reader.errorString());
                return false;
            }
            *sourceImage = image;
        }
    }
    if (state)
        *state = document;
    return true;
}

bool SstvStorage::removeUserTemplate(const QString &name, QString *error) {
    for (const QString &path : {templatePath(name), templateImagePath(name)}) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            setError(error, QStringLiteral("Could not remove the SSTV template."));
            return false;
        }
    }
    return true;
}

bool SstvStorage::resetUserTemplates(QString *error) {
    const QDir directory(templatesPath());
    if (!directory.exists())
        return true;
    for (const QFileInfo &file :
         directory.entryInfoList({QStringLiteral("*.json"), QStringLiteral("*.png")}, QDir::Files)) {
        if (!QFile::remove(file.absoluteFilePath())) {
            setError(error, QStringLiteral("Could not reset the SSTV templates."));
            return false;
        }
    }
    return true;
}

QStringList SstvStorage::imageTemplateNames(QString *error) const {
    QStringList result;
    const QDir root(imageTemplatesPath());
    if (!root.exists())
        return result;
    for (const QFileInfo &entry : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QJsonObject object;
        if (readJson(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("template.json")), &object, error)) {
            const QString name = object.value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                result.append(name);
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool SstvStorage::saveImageTemplate(const QString &name, const QImage &sourceImage, const QImage &previewImage,
                                    const QJsonObject &state, QString *error) {
    const QString cleanName = name.trimmed().left(40);
    if (cleanName.isEmpty()) {
        setError(error, QStringLiteral("Enter an image template name."));
        return false;
    }
    if (sourceImage.isNull() || previewImage.isNull()) {
        setError(error, QStringLiteral("The image template has no picture."));
        return false;
    }
    const QString directoryPath = imageTemplatePath(cleanName);
    if (!ensureDirectory(directoryPath, error))
        return false;
    const QDir directory(directoryPath);
    if (!writeImage(directory.filePath(QStringLiteral("source.png")), sourceImage, error) ||
        !writeImage(directory.filePath(QStringLiteral("preview.png")), previewImage, error))
        return false;
    QJsonObject document = state;
    document.insert(QStringLiteral("version"), 1);
    document.insert(QStringLiteral("name"), cleanName);
    document.insert(QStringLiteral("sourceFile"), QStringLiteral("source.png"));
    document.insert(QStringLiteral("previewFile"), QStringLiteral("preview.png"));
    document.insert(QStringLiteral("savedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return writeJson(directory.filePath(QStringLiteral("template.json")), document, error);
}

bool SstvStorage::loadImageTemplate(const QString &name, QImage *sourceImage, QImage *previewImage, QJsonObject *state,
                                    QString *error) const {
    const QDir directory(imageTemplatePath(name));
    QJsonObject document;
    if (!readJson(directory.filePath(QStringLiteral("template.json")), &document, error))
        return false;
    const auto readTemplateImage = [&directory, error](const QString &fileName, QImage *target) {
        if (!target)
            return true;
        QImageReader reader(directory.filePath(fileName));
        const QImage image = reader.read();
        if (image.isNull()) {
            setError(error, reader.errorString());
            return false;
        }
        *target = image;
        return true;
    };
    if (!readTemplateImage(document.value(QStringLiteral("sourceFile")).toString(), sourceImage) ||
        !readTemplateImage(document.value(QStringLiteral("previewFile")).toString(), previewImage))
        return false;
    if (state)
        *state = document;
    return true;
}

bool SstvStorage::removeImageTemplate(const QString &name, QString *error) {
    const QDir directory(imageTemplatePath(name));
    if (!directory.exists())
        return true;
    for (const QString &fileName :
         {QStringLiteral("template.json"), QStringLiteral("source.png"), QStringLiteral("preview.png")}) {
        const QString path = directory.filePath(fileName);
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            setError(error, QStringLiteral("Could not remove image template file %1.").arg(fileName));
            return false;
        }
    }
    if (QDir().rmdir(directory.absolutePath()))
        return true;
    setError(error, QStringLiteral("Could not remove the SSTV image template."));
    return false;
}
