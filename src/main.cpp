#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QSysInfo>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QSslSocket>
#include <rhi/qrhi.h>
#ifdef Q_OS_MACOS
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformintegration.h>
#include <cstdlib>
#include <QDir>
#include <QFileInfo>
#endif
#include "mainwindow.h"
#include "ui/styling/k4styles.h"

// Filter out known benign Qt warnings on macOS
// QSocketNotifier::Exception is not supported by kqueue (macOS's event system)
// This warning comes from Qt's internal socket code and doesn't affect functionality
static QtMessageHandler originalHandler = nullptr;
void messageFilter(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
#ifdef Q_OS_MACOS
    if (msg.contains("QSocketNotifier::Exception is not supported")) {
        return; // Suppress this known benign warning
    }
#endif
    if (originalHandler) {
        originalHandler(type, context, msg);
    }
}

// Load embedded fonts and set application defaults
void setupFonts() {
    // Load Inter font family (screen-optimized sans-serif for all UI)
    int interRegular = QFontDatabase::addApplicationFont(":/fonts/Inter-Regular.ttf");
    int interMedium = QFontDatabase::addApplicationFont(":/fonts/Inter-Medium.ttf");
    int interSemiBold = QFontDatabase::addApplicationFont(":/fonts/Inter-SemiBold.ttf");
    int interBold = QFontDatabase::addApplicationFont(":/fonts/Inter-Bold.ttf");

    // Verify fonts loaded (only warn on failure)
    if (interRegular < 0 || interMedium < 0) {
        qWarning() << "Failed to load Inter font - using system default";
    }

    // Set Inter Medium as the default application font (crisper than Regular)
    // Use setPixelSize() for consistent sizing across macOS (72 PPI) and Windows (96 PPI)
    QFont defaultFont(K4Styles::Fonts::Primary);
    defaultFont.setPixelSize(K4Styles::Dimensions::FontSizeLarge);
    defaultFont.setWeight(QFont::Medium);
    defaultFont.setHintingPreference(QFont::PreferFullHinting);
    defaultFont.setStyleStrategy(QFont::PreferAntialias);
    QApplication::setFont(defaultFont);
}

// WHY: Windows ships both the Schannel and OpenSSL TLS backends, and Qt may activate
// Schannel — which has no TLS-PSK support at all, so the K4's port-9204 PSK handshake can
// never complete under it. Must run before the first QSslSocket is constructed (TcpClient
// creates one in its constructor). No-op on macOS and Linux, where OpenSSL is already active.
void selectTlsBackend() {
    if (QSslSocket::activeBackend() == QLatin1String("openssl"))
        return;

    if (QSslSocket::availableBackends().contains(QLatin1String("openssl"))) {
        if (!QSslSocket::setActiveBackend(QStringLiteral("openssl")))
            qWarning() << "Failed to activate the OpenSSL TLS backend - TLS/PSK unavailable";
    } else {
        qWarning() << "OpenSSL TLS backend unavailable (active:" << QSslSocket::activeBackend()
                   << ") - TLS/PSK connections will fail";
    }
}

int main(int argc, char *argv[]) {
    // Install message filter to suppress known benign Qt warnings
    originalHandler = qInstallMessageHandler(messageFilter);

#ifdef Q_OS_MACOS
    // Enable OpenSSL for TLS/PSK support
    // Qt's OpenSSL backend dynamically loads libssl/libcrypto at runtime
    // Check bundled location first (inside .app bundle), then Homebrew locations

    // Get the path to the executable to find the Frameworks folder
    QString execPath = QString::fromLocal8Bit(argv[0]);
    QString bundledFrameworks;
    if (execPath.contains(".app/Contents/MacOS/")) {
        bundledFrameworks = QFileInfo(execPath).absolutePath() + "/../Frameworks";
    }

    QStringList opensslPaths;
    if (!bundledFrameworks.isEmpty()) {
        opensslPaths << bundledFrameworks; // Check bundled first
    }
    opensslPaths << "/opt/homebrew/opt/openssl@3/lib" // Homebrew on Apple Silicon
                 << "/usr/local/opt/openssl@3/lib"    // Homebrew on Intel Mac
                 << "/opt/homebrew/opt/openssl/lib"   // Homebrew openssl (latest)
                 << "/usr/local/opt/openssl/lib";     // Homebrew openssl on Intel

    QString currentPath = QString::fromLocal8Bit(qgetenv("DYLD_LIBRARY_PATH"));
    bool foundOpenSSL = false;

    for (const QString &opensslPath : opensslPaths) {
        // Check if libssl exists in this location
        if (QFileInfo::exists(opensslPath + "/libssl.3.dylib") || QFileInfo::exists(opensslPath + "/libssl.dylib")) {
            if (!currentPath.contains(opensslPath)) {
                QString newPath = currentPath.isEmpty() ? opensslPath : QString("%1:%2").arg(opensslPath, currentPath);
                qputenv("DYLD_LIBRARY_PATH", newPath.toLocal8Bit());
            }
            foundOpenSSL = true;
            break;
        }
    }
    Q_UNUSED(foundOpenSSL);
#endif

    // Enable HiDPI scaling for crisp rendering on Retina/4K displays
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("QK4");
    app.setApplicationVersion(QK4_VERSION);

    // WHY: call sign changed AI5QK->KF5O. Migrate existing QSettings forward once so saved
    // window geometry, station profiles, and radio config survive the identity rename.
    // Default-constructing QSettings under each identity reproduces Qt's per-platform path
    // logic (macOS keys on the domain, Windows/Linux on the org name). The empty-check keeps
    // it idempotent — no re-copy after the first launch.
    app.setOrganizationName("AI5QK");
    app.setOrganizationDomain("ai5qk.com");
    QSettings oldSettings;
    app.setOrganizationName("KF5O");
    app.setOrganizationDomain("kf5o.com");
    QSettings newSettings;
    if (newSettings.allKeys().isEmpty() && !oldSettings.allKeys().isEmpty()) {
        for (const QString &key : oldSettings.allKeys())
            newSettings.setValue(key, oldSettings.value(key));
        newSettings.sync();
    }

    // Load embedded Inter font family
    setupFonts();

    // Must precede MainWindow — its controllers construct the first QSslSocket
    selectTlsBackend();

    // --connect <name> opens one particular saved radio, overriding the list's auto-connect tick,
    // so one install can carry a desktop shortcut per K4.
    //
    // WHY parse() and not process(): process() prints an error and EXITS on an unrecognised
    // option, and a desktop- or Finder-launched app can be handed arguments it never declared
    // (macOS has historically passed -psn_...). Trading "the app opens when double-clicked" for a
    // command-line flag is not a trade worth making, so anything unrecognised is warned about and
    // ignored rather than fatal.
    QCommandLineParser parser;
    parser.setApplicationDescription("QK4 - Elecraft K4 remote control");
    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();
    const QCommandLineOption connectOption(
        QStringList{QStringLiteral("c"), QStringLiteral("connect")},
        QStringLiteral("Connect at startup to the saved radio named <name>, whatever the list is set to."),
        QStringLiteral("name"));
    parser.addOption(connectOption);
    if (!parser.parse(QCoreApplication::arguments())) {
        qWarning() << "Ignoring command line:" << parser.errorText();
    }
    if (parser.isSet(helpOption)) {
        parser.showHelp(0);
    }
    if (parser.isSet(versionOption)) {
        parser.showVersion();
    }

    MainWindow window;
    if (parser.isSet(connectOption)) {
        // Before exec(), because the startup connect runs on the first pass of the event loop.
        window.setStartupRadioOverride(parser.value(connectOption));
    }
    window.show();

    return app.exec();
}
