#include <QApplication>
#include "logging/logcategories.h"
#include "logging/logpaths.h"
#include "logging/logsession.h"
#include "logging/logsink.h"
#include <QDir>
#include <QScreen>
#include <QCommandLineParser>
#include <QDebug>
#include <QSysInfo>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QMessageBox>
#include <rhi/qrhi.h>
#ifdef Q_OS_MACOS
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformintegration.h>
#include <cstdlib>
#endif
#include <QTextStream>
#include "network/tlsbackend.h"
#include "settings/radiosettings.h"
#include "mainwindow.h"
#include "settings/interfacescale.h"
#include "utils/rtscheduling.h"
#include "ui/styling/k4styles.h"
// WHY: last, after every project header. windows.h defines SendMessage, ERROR and a long tail of
// other unqualified macros, and anything it reaches first it rewrites - a header that later
// declares a member called SendMessage or an enumerator called ERROR fails in a way that names
// neither. WIN32_LEAN_AND_MEAN and NOMINMAX (defined for every target in CMakeLists.txt) cut most of
// it; include order handles the rest.
#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#endif

static QString packagingDescription() {
    if (qEnvironmentVariableIsSet("FLATPAK_ID"))
        return QStringLiteral("Flatpak ") + qEnvironmentVariable("FLATPAK_ID");
#ifdef Q_OS_MACOS
    return QCoreApplication::applicationDirPath().contains(QLatin1String(".app/Contents/MacOS"))
               ? QStringLiteral("macOS app bundle")
               : QStringLiteral("macOS executable");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#else
    return QStringLiteral("Linux executable");
#endif
}

// The environment a log was written in, at the top of every log file.
static QStringList sessionHeader() {
    QStringList lines;
    lines << QStringLiteral("QK4 %1").arg(QStringLiteral(QK4_VERSION));
    lines << QStringLiteral("Qt build %1, runtime %2")
                 .arg(QStringLiteral(QT_VERSION_STR), QString::fromLatin1(qVersion()));
    lines << QStringLiteral("OS %1 (%2 %3, %4)")
                 .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                      QSysInfo::currentCpuArchitecture());
    lines << QStringLiteral("Packaging %1").arg(packagingDescription());
    lines
        << QStringLiteral("TLS backend %1, %2").arg(QSslSocket::activeBackend(), QSslSocket::sslLibraryVersionString());
    for (const QScreen *screen : QGuiApplication::screens()) {
        lines << QStringLiteral("Screen %1 %2x%3 at %4x")
                     .arg(screen->name())
                     .arg(screen->size().width())
                     .arg(screen->size().height())
                     .arg(screen->devicePixelRatio());
    }
    return lines;
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
        qCWarning(lcApp) << "Failed to load Inter font - using system default";
    }

    // Set Inter Medium as the default application font (crisper than Regular)
    // Use setPixelSize() for consistent sizing across macOS (72 PPI) and Windows (96 PPI)
    QFont defaultFont(K4Styles::Fonts::Primary);
    defaultFont.setPixelSize(K4Styles::Dimensions::FontSizeBody);
    defaultFont.setWeight(QFont::Medium);
    defaultFont.setHintingPreference(QFont::PreferFullHinting);
    defaultFont.setStyleStrategy(QFont::PreferAntialias);
    QApplication::setFont(defaultFont);
}

#ifdef Q_OS_WIN
// Re-attach to the console of whichever shell launched QK4, so command-line diagnostics can
// actually be read.
//
// WHY: CMakeLists.txt builds QK4 as a WIN32-subsystem binary, which is what
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
// WHY: the console is claimed HERE and not in main() because AttachConsole() attaches whatever the parent
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

int main(int argc, char *argv[]) {
    // First, so every message from here on goes through the log writer and none is written
    // synchronously on the thread that raised it.
    Logging::install();
    struct LoggingSession {
        ~LoggingSession() { Logging::shutdown(); }
    } loggingSession;

    // PassThrough: fractional scale factors (125%, 150%) are used as-is, not rounded.
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Must precede QApplication: Qt reads the scale factor once, when it is constructed.
    InterfaceScale::applyBeforeApp();

    QApplication app(argc, argv);
    app.setApplicationName("QK4");
    app.setApplicationVersion(QK4_VERSION);

    // Identity only. Settings do not depend on it: RadioSettings names its store "QK4"/"QK4"
    // explicitly.
    app.setOrganizationName("KF5O");
    app.setOrganizationDomain("kf5o.com");

    // Load embedded Inter font family
    setupFonts();

    // Before any widget exists, so no control is ever polished with the platform's theme
    app.setStyleSheet(K4Styles::applicationStyleSheet());

    // Must precede MainWindow — its controllers construct the first QSslSocket
    TlsBackend::selectOpenSsl();

    // --connect <name> opens one particular saved radio, overriding the list's auto-connect tick,
    // so one install can carry a desktop shortcut per K4.
    //
    // WHY: parse() and not process() - process() prints an error and EXITS on an unrecognised
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
    const QCommandLineOption tlsSelfCheckOption(
        QStringLiteral("tls-self-check"),
        QStringLiteral("Report the TLS backend and OpenSSL library in use, then exit (0 = TLS-PSK available)."));
    parser.addOption(tlsSelfCheckOption);
    const QCommandLineOption logFileOption(
        QStringLiteral("log-file"),
        QStringLiteral("Write a log file for this session, whatever the Diagnostics page says. It goes in ") +
            QDir::toNativeSeparators(Logging::logDirectory(Logging::currentPathInputs())) + QStringLiteral("."));
    parser.addOption(logFileOption);
    const QCommandLineOption logStderrOption(
        QStringLiteral("log-stderr"),
        QStringLiteral("Also print log messages to the console while a log file is being written."));
    parser.addOption(logStderrOption);
    const QCommandLineOption logRulesOption(
        QStringLiteral("log-rules"),
        QStringLiteral("Logging rules applied after the Diagnostics page's, separated by ';' - for example "
                       "\"qk4.cw.*.debug=true;qk4.cat.tx.debug=true\". QT_LOGGING_RULES still wins."),
        QStringLiteral("rules"));
    parser.addOption(logRulesOption);
    const QStringList arguments = QCoreApplication::arguments();
    const bool parsedCleanly = parser.parse(arguments);
    // WHY: not showHelp()/showVersion() - both call ::exit(), which ends the process without
    // unwinding, and Qt then reports its own half-torn-down state to whoever asked for --help -
    // "QThreadStorage: entry 2 destroyed before end of thread" on the line after the usage text.
    // Returning through main() instead shuts down in order and prints nothing extra. It also puts
    // these two on the same footing as the --connect message below, which Qt's versions are not:
    // showHelp() decides on a message box by its own rules, in its own words.
    //
    // WHY: --help-all is tested by name. addHelpOption() declares both --help and --help-all but
    // returns an option covering only --help, and parse() ignores the rest, so --help-all would
    // open the GUI. Qt's own listing of its generic
    // options is not reachable without showHelp()'s ::exit(), so this answers with the same text
    // --help gives rather than pretending to more.
    if (parser.isSet(helpOption) || parser.isSet(QStringLiteral("help-all"))) {
        reportToOperator(parser.helpText(), QMessageBox::Information);
        return 0;
    }
    if (parser.isSet(tlsSelfCheckOption))
        return TlsBackend::runSelfCheck();
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
    // Listing the configured radios answers the question they were about to ask next. It goes to
    // the console when there is one and to a dialog when there is not, because a shortcut can
    // carry a bare --connect too and exiting silently there is indistinguishable from the app
    // being broken.
    //
    // WHY: `--connect=` too. Qt parses an explicit empty value as the option set to nothing, which
    // is the same mistake as a bare --connect.
    bool askedToConnect = false;
    for (const QString &argument : arguments) {
        if (argument == QStringLiteral("-c") || argument == QStringLiteral("--connect") ||
            argument.startsWith(QStringLiteral("--connect=")) || argument.startsWith(QStringLiteral("-c="))) {
            askedToConnect = true;
            break;
        }
    }

    // WHY: tested on the VALUE, not on isSet. After a failed parse Qt still reports the option as
    // set, with nothing in it, so isSet answers yes to the exact case this is here to catch.
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
        qCWarning(lcApp) << "Ignoring command line:" << parser.errorText();
    }

    Logging::CommandLine logCommandLine;
    logCommandLine.extraRules = parser.value(logRulesOption);
    logCommandLine.forceFile = parser.isSet(logFileOption);
    logCommandLine.stderrRequested = parser.isSet(logStderrOption);
    Logging::setCommandLine(logCommandLine);
    Logging::setSessionHeader(sessionHeader());
    auto applyLogging = [] {
        const RadioSettings *settings = RadioSettings::instance();
        Logging::apply(settings->logFileEnabled(), settings->logConfig());
    };
    applyLogging();
    QObject::connect(RadioSettings::instance(), &RadioSettings::loggingChanged, RadioSettings::instance(),
                     applyLogging);
    Logging::sink()->start();
    if (const char *throttling = RtScheduling::optOutOfPowerThrottling())
        qCInfo(lcApp) << "Windows:" << throttling;

    int exitCode = 0;
    bool relaunch = false;
    {
        MainWindow window;
        if (InterfaceScale::fitNeedsRelaunch(window)) {
            // Before show() and exec(): the operator never sees the misfitted window, and the
            // startup connect - which runs on the first pass of the event loop - never starts.
            window.close();
            relaunch = true;
        } else {
            if (!parser.value(connectOption).trimmed().isEmpty()) {
                // Before exec(), because the startup connect runs on the first pass of the event loop.
                window.setStartupRadioOverride(parser.value(connectOption).trimmed());
            }
            window.show();
            exitCode = app.exec();
            relaunch = InterfaceScale::restartRequested();
        }
    }
    // WHY: only after MainWindow is destroyed. Its teardown closes the CAT and TCI listeners and
    // releases the USB devices; a new instance started any earlier would race this one for them.
    if (relaunch) {
        // Before the new instance starts, so it finds this one's log closed and rotates it.
        Logging::shutdown();
        InterfaceScale::relaunch();
    }
    return exitCode;
}
