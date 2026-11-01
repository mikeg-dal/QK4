#include "tlsbackend.h"

#include "logging/logcategories.h"

#include <QByteArray>
#include <QFileInfo>
#include <QLatin1String>
#include <QLibraryInfo>
#include <QList>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QString>
#include <QStringList>
#include <QTextStream>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <link.h>
#endif

namespace {

const QLatin1String OpenSsl("openssl");

#if defined(Q_OS_MACOS)
QString bundleFrameworksPath() {
    const CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle)
        return {};
    const CFURLRef url = CFBundleCopyPrivateFrameworksURL(bundle);
    if (!url)
        return {};
    char path[PATH_MAX];
    const bool ok = CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8 *>(path), sizeof(path));
    CFRelease(url);
    return ok ? QString::fromUtf8(path) : QString();
}
#elif defined(Q_OS_LINUX)
int findLibSsl(dl_phdr_info *info, size_t, void *data) {
    const QString name = QString::fromLocal8Bit(info->dlpi_name);
    if (QFileInfo(name).fileName().startsWith(QLatin1String("libssl.so.3"))) {
        *static_cast<QString *>(data) = name;
        return 1;
    }
    return 0;
}
#endif

// The file the OpenSSL backend actually loaded, so a packaging check can tell a bundled OpenSSL
// from one that happens to be installed on the build machine. Matched on the OpenSSL 3 name: macOS
// system frameworks load their own LibreSSL as /usr/lib/libssl.48.dylib into the same process.
QString loadedLibSslPath() {
#if defined(Q_OS_MACOS)
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const QString name = QString::fromUtf8(_dyld_get_image_name(i));
        if (QFileInfo(name).fileName() == QLatin1String("libssl.3.dylib"))
            return name;
    }
#elif defined(Q_OS_WIN)
    for (const wchar_t *module : {L"libssl-3-x64.dll", L"libssl-3-arm64.dll", L"libssl-3.dll"}) {
        if (const HMODULE handle = GetModuleHandleW(module)) {
            wchar_t path[MAX_PATH];
            const DWORD length = GetModuleFileNameW(handle, path, MAX_PATH);
            if (length > 0 && length < MAX_PATH)
                return QString::fromWCharArray(path, static_cast<int>(length));
        }
    }
#elif defined(Q_OS_LINUX)
    QString found;
    dl_iterate_phdr(findLibSsl, &found);
    return found;
#endif
    return {};
}

void addDevelopmentOpenSslSearchPath() {
#if defined(Q_OS_MACOS)
    // WHY: Qt's binary OpenSSL plugin loads libssl/libcrypto at run time and, on macOS, looks for
    // them in DYLD_LIBRARY_PATH as read with qgetenv() when the first TLS call is made, then in
    // the bundle's Contents/Frameworks, then /usr/lib and /usr/local/lib
    // (qtbase src/plugins/tls/openssl/qsslsocket_openssl_symbols.cpp, libraryPathList()). dyld
    // itself reads DYLD_* only at launch, so setting it here changes nothing dyld does; it only
    // widens Qt's own scan. A deployed bundle carries its OpenSSL in Contents/Frameworks, and
    // there this does nothing, so a packaged app can never quietly run on a Homebrew OpenSSL. A
    // build tree has none, and Homebrew's arm64 prefix is not on Qt's list.
    const QString frameworks = bundleFrameworksPath();
    if (!frameworks.isEmpty() && QFileInfo::exists(frameworks + QLatin1String("/libssl.3.dylib")))
        return;

    for (const char *dir : {"/opt/homebrew/opt/openssl@3/lib", "/usr/local/opt/openssl@3/lib"}) {
        const QString path = QString::fromLatin1(dir);
        if (!QFileInfo::exists(path + QLatin1String("/libssl.3.dylib")))
            continue;
        QByteArray value = qgetenv("DYLD_LIBRARY_PATH");
        value = value.isEmpty() ? path.toLocal8Bit() : path.toLocal8Bit() + ':' + value;
        qputenv("DYLD_LIBRARY_PATH", value);
        return;
    }
#endif
}

} // namespace

namespace TlsBackend {

void selectOpenSsl() {
    addDevelopmentOpenSslSearchPath();

    // WHY: Windows ships both the Schannel and OpenSSL TLS backends and macOS both Secure
    // Transport and OpenSSL, and Qt may activate one with no TLS-PSK support at all, so the
    // K4's port-9204 PSK handshake could never complete under it.
    if (QSslSocket::activeBackend() == OpenSsl)
        return;

    if (QSslSocket::availableBackends().contains(OpenSsl)) {
        if (!QSslSocket::setActiveBackend(OpenSsl))
            qCWarning(lcLinkTcp) << "Failed to activate the OpenSSL TLS backend - TLS/PSK unavailable";
    } else {
        qCWarning(lcLinkTcp) << "OpenSSL TLS backend unavailable (active:" << QSslSocket::activeBackend()
                             << ") - TLS/PSK connections will fail";
    }
}

int runSelfCheck() {
    QTextStream out(stdout);
    const QString active = QSslSocket::activeBackend();
    const bool supportsSsl = QSslSocket::supportsSsl();

    QStringList psk;
    if (supportsSsl) {
        for (const QSslCipher &cipher : tlsPskCiphers())
            psk << cipher.name();
    }
    const QString libssl = loadedLibSslPath();
    const bool pass = active == OpenSsl && supportsSsl && !psk.isEmpty();

    out << "Qt: " << QLibraryInfo::version().toString() << "\n"
        << "Available backends: " << QSslSocket::availableBackends().join(QLatin1String(", ")) << "\n"
        << "Active backend: " << (active.isEmpty() ? QStringLiteral("(none)") : active) << "\n"
        << "supportsSsl: " << (supportsSsl ? "yes" : "no") << "\n"
        << "TLS library: " << QSslSocket::sslLibraryVersionString() << "\n"
        << "TLS library built against: " << QSslSocket::sslLibraryBuildVersionString() << "\n"
        << "libssl loaded from: " << (libssl.isEmpty() ? QStringLiteral("(not loaded)") : libssl) << "\n"
        << "TLS 1.2+ PSK cipher suites: " << psk.size();
    if (!psk.isEmpty())
        out << " (" << psk.join(QLatin1String(", ")) << ")";
    out << "\n"
        << "TLS self-check: " << (pass ? "PASS" : "FAIL") << "\n";
    out.flush();
    return pass ? 0 : 1;
}

} // namespace TlsBackend
