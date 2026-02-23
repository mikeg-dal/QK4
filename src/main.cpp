#include <QApplication>
#include <QDebug>
#include <QSysInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QFontDatabase>
#include <cmath>
#include <rhi/qrhi.h>
#ifdef Q_OS_MACOS
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformintegration.h>
#include <cstdlib>
#include <QDir>
#include <QFileInfo>
#endif
#include "mainwindow.h"
#include "ui/k4styles.h"

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
    app.setOrganizationName("AI5QK");
    app.setOrganizationDomain("ai5qk.com");

    if (QScreen *screen = app.primaryScreen()) {
        qreal diagonalInches = 0.0;
        const QSizeF physicalSizeMm = screen->physicalSize();
        if (physicalSizeMm.width() > 0.0 && physicalSizeMm.height() > 0.0) {
            const qreal diagonalMm = std::hypot(physicalSizeMm.width(), physicalSizeMm.height());
            diagonalInches = diagonalMm / 25.4;
        }
        K4Styles::configureForScreen(screen->availableGeometry().size(), screen->devicePixelRatio(), diagonalInches);
    } else {
        K4Styles::configureForScreen(QSize(1340, 840), 1.0, 0.0);
    }

    // Load embedded Inter font family
    setupFonts();

    MainWindow window;
    window.show();

    return app.exec();
}
