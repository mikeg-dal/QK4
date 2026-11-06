#pragma once

#include <QLatin1String>
#include <QList>
#include <QSslCipher>
#include <QSslConfiguration>

namespace TlsBackend {

// The cipher suites QK4 offers the K4 on its TLS port: every TLS 1.2 or 1.3 PSK suite the active
// backend supports. The one definition, shared by the connection and by `--tls-self-check`, so the
// self-check reports exactly what a connection would offer. Inline so TcpClient (and its test)
// need none of tlsbackend.cpp's platform code.
inline QList<QSslCipher> tlsPskCiphers() {
    QList<QSslCipher> psk;
    for (const QSslCipher &cipher : QSslConfiguration::supportedCiphers()) {
        if (cipher.name().contains(QLatin1String("PSK")) &&
            (cipher.protocol() == QSsl::TlsV1_2 || cipher.protocol() == QSsl::TlsV1_3))
            psk.append(cipher);
    }
    return psk;
}

// Makes Qt's OpenSSL backend the active one. Must run before the first QSslSocket is constructed.
void selectOpenSsl();

// `QK4 --tls-self-check`: reports which TLS backend and OpenSSL library are in use and whether a
// TLS 1.2+ PSK cipher suite is available. Opens no window and no connection. Returns 0 only when
// OpenSSL is active and PSK is available, 1 otherwise.
int runSelfCheck();

} // namespace TlsBackend
