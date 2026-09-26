#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QSysInfo>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QMessageBox>
#include <QSettings>
#include <QSslSocket>
#include <rhi/qrhi.h>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#endif
#ifdef Q_OS_MACOS
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformintegration.h>
#include <cstdlib>
#include <QDir>
#include <QFileInfo>
#endif
#include <QTextStream>
#include "settings/radiosettings.h"
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

#ifdef Q_OS_WIN
// Re-attach to the console of whichever shell launched QK4, so command-line diagnostics can
// actually be read.
//
// WHY THIS IS NEEDED AT ALL: CMakeLists.txt builds QK4 as a WIN32-subsystem binary, which is what
// keeps a console window from flashing up behind the GUI on a double-click. The cost is that
// Windows gives the process NO console and does not hand it the invoking cmd.exe's one either, so
// stdout and stderr are invalid handles and everything main() prints is discarded in silence. The
// operator sees a bare `QK4 --connect` do nothing at all. macOS has no subsystem concept - the
// executable inherits Terminal's stdout when invoked by path - which is why the same code has
// always worked there and never here.
//
// Returns whether anything printed can now be seen by somebody.
static bool attachParentConsole() {
    // Sample the inherited handles BEFORE attaching. AttachConsole() may point the standard
    // handles at the new console, and doing that to a caller who wrote `QK4 --connect > out.txt`
    // would take their output away from the file they asked for and scatter it on the terminal.
    // Saving them here makes the outcome the same either way, so this does not rest on which
    // behaviour AttachConsole() happens to have.
    const auto usable = [](HANDLE handle) { return handle != nullptr && handle != INVALID_HANDLE_VALUE; };
    const HANDLE inheritedOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE inheritedErr = GetStdHandle(STD_ERROR_HANDLE);
    const bool outWasRedirected = usable(inheritedOut);
    const bool errWasRedirected = usable(inheritedErr);

    // ERROR_ACCESS_DENIED means this process already owns a console, which is a success for our
    // purposes. Any other failure - ERROR_INVALID_HANDLE for an Explorer, shortcut or Start-menu
    // launch - means there is no console to attach to and never will be.
    const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE || GetLastError() == ERROR_ACCESS_DENIED;

    if (attached) {
        FILE *reopened = nullptr;
        if (outWasRedirected) {
            SetStdHandle(STD_OUTPUT_HANDLE, inheritedOut);
        } else {
            freopen_s(&reopened, "CONOUT$", "w", stdout);
        }
        if (errWasRedirected) {
            SetStdHandle(STD_ERROR_HANDLE, inheritedErr);
        } else {
            freopen_s(&reopened, "CONOUT$", "w", stderr);
        }
    }

    // Redirected handles are readable output even with no console behind them: the caller is
    // holding the far end of the pipe or file.
    return attached || outWasRedirected;
}
#endif

// Whether a command-line message printed to stdout will reach anybody, which decides between
// printing usage and putting it in a dialog.
//
// On macOS and Linux the invoking terminal's stdout is inherited as a matter of course, so there
// is nothing to attach and printing has always reached whoever typed the command.
static bool claimConsole() {
#ifdef Q_OS_WIN
    return attachParentConsole();
#else
    return true;
#endif
}

// The single place that decides where an operator-facing command-line message goes. Every one of
// them - help, version, and the --connect usage error - is answering someone who typed a command,
// so they all face the same question of whether anybody can see stdout, and they must all answer
// it the same way.
//
// WHY the console is claimed HERE and not in main(): AttachConsole() attaches whatever the parent
// happens to be, and a console claimed at startup would be held for the whole GUI lifetime. A
// launcher or logging wrapper that spawns QK4 from a console process would then receive every
// qWarning for the rest of the session in its own window. Claiming it only when there is something
// to say confines that to the command-line paths, all of which return immediately afterwards.
static void reportToOperator(const QString &text, QMessageBox::Icon icon) {
    static const bool haveConsole = claimConsole();
    if (haveConsole) {
        QTextStream out(stdout);
        out << text;
        out.flush();
        return;
    }
    // No console: a shortcut or Explorer launch. Exiting silently is indistinguishable from the
    // app being broken, so say it in the only place this launch can be heard.
    QMessageBox box(icon, QStringLiteral("QK4"), text, QMessageBox::Ok);
    box.exec();
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
    const QStringList arguments = QCoreApplication::arguments();
    const bool parsedCleanly = parser.parse(arguments);
    // WHY not showHelp()/showVersion(): both call ::exit(), which ends the process without
    // unwinding, and Qt then reports its own half-torn-down state to whoever asked for --help -
    // "QThreadStorage: entry 2 destroyed before end of thread" on the line after the usage text.
    // Returning through main() instead shuts down in order and prints nothing extra. It also puts
    // these two on the same footing as the --connect message below, which Qt's versions are not:
    // showHelp() decides on a message box by its own rules, in its own words.
    if (parser.isSet(helpOption)) {
        reportToOperator(parser.helpText(), QMessageBox::Information);
        return 0;
    }
    if (parser.isSet(versionOption)) {
        reportToOperator(QCoreApplication::applicationName() + QStringLiteral(" ") +
                             QCoreApplication::applicationVersion() + QStringLiteral("\n"),
                         QMessageBox::Information);
        return 0;
    }

    // --connect WITH NO NAME is a usage error, not noise to ignore.
    //
    // An UNRECOGNISED option is ignored on purpose (see parse() above) because a desktop- or
    // Finder-launched app can be handed arguments it never declared. This is the opposite case:
    // our own option, used wrongly. The operator asked to open a particular radio and did not say
    // which, so running on would either connect to a different radio or to none, having been told
    // explicitly to connect to something.
    //
    // Listing the configured radios answers the question they were about to ask next. Printed
    // rather than shown in a dialog because a bare --connect is a typed command, and a shortcut
    // would have carried the name.
    // Matching the bare strings alone let `--connect=` through: Qt parses an explicit but empty
    // value as the option being set to nothing, `arguments.contains("--connect")` is false for it,
    // and the guard below never fired - so the operator got the GUI and no complaint. An explicit
    // empty value is the same mistake as a bare --connect and has to be caught with it.
    bool askedToConnect = false;
    for (const QString &argument : arguments) {
        if (argument == QStringLiteral("-c") || argument == QStringLiteral("--connect") ||
            argument.startsWith(QStringLiteral("--connect=")) || argument.startsWith(QStringLiteral("-c="))) {
            askedToConnect = true;
            break;
        }
    }
    //
    // Tested on the VALUE, not on isSet: after a failed parse Qt still reports the option as set,
    // with nothing in it, so isSet answers yes to the exact case this is here to catch.
    if (askedToConnect && parser.value(connectOption).trimmed().isEmpty()) {
        QString usage = QStringLiteral("--connect needs the name of a saved radio.\n\n");
        const auto radios = RadioSettings::instance()->radios();
        if (radios.isEmpty()) {
            usage += QStringLiteral("No radios are configured yet. Add one in QK4's Server Manager first.\n");
        } else {
            usage += QStringLiteral("Configured radios:\n");
            for (const RadioEntry &radio : radios) {
                usage += QStringLiteral("  ") + (radio.name.isEmpty() ? radio.host : radio.name) +
                         (radio.connectAtStartup ? QStringLiteral("   (currently opens at startup)") : QString()) +
                         QStringLiteral("\n");
            }
            usage += QStringLiteral("\nFor example:  QK4 --connect \"") + radios.first().name + QStringLiteral("\"\n");
        }

        reportToOperator(usage, QMessageBox::Warning);
        return 2;
    }

    // Only NOW, so the usage message above is not preceded by Qt restating the same problem
    // in its own words. Anything still unparsed at this point is an option QK4 never
    // declared, which is ignored on purpose.
    if (!parsedCleanly) {
        qWarning() << "Ignoring command line:" << parser.errorText();
    }

    MainWindow window;
    if (!parser.value(connectOption).trimmed().isEmpty()) {
        // Before exec(), because the startup connect runs on the first pass of the event loop.
        window.setStartupRadioOverride(parser.value(connectOption).trimmed());
    }
    window.show();

    return app.exec();
}
