#include "logging/logcategories.h"

// WHY: QtInfoMsg on every definition. Without a severity argument Q_LOGGING_CATEGORY enables
// debug output, in release builds too, which put a formatted stderr write on the keyer and audio
// threads for every element and frame.
Q_LOGGING_CATEGORY(lcApp, "qk4.app", QtInfoMsg)
Q_LOGGING_CATEGORY(lcUi, "qk4.ui", QtInfoMsg)
Q_LOGGING_CATEGORY(lcUiMenu, "qk4.ui.menu", QtInfoMsg)
Q_LOGGING_CATEGORY(lcUiBandNav, "qk4.ui.bandnav", QtInfoMsg)
Q_LOGGING_CATEGORY(lcUiModePopup, "qk4.ui.modepopup", QtInfoMsg)
Q_LOGGING_CATEGORY(lcUiTextDecode, "qk4.ui.textdecode", QtInfoMsg)
Q_LOGGING_CATEGORY(lcUiMacro, "qk4.ui.macro", QtInfoMsg)

Q_LOGGING_CATEGORY(lcLink, "qk4.link", QtInfoMsg)
Q_LOGGING_CATEGORY(lcLinkTcp, "qk4.link.tcp", QtInfoMsg)
Q_LOGGING_CATEGORY(lcLinkDiscovery, "qk4.link.discovery", QtInfoMsg)
Q_LOGGING_CATEGORY(lcTxOwner, "qk4.tx.owner", QtInfoMsg)
Q_LOGGING_CATEGORY(lcCatTx, "qk4.cat.tx", QtInfoMsg)
Q_LOGGING_CATEGORY(lcCatRx, "qk4.cat.rx", QtInfoMsg)

Q_LOGGING_CATEGORY(lcAudio, "qk4.audio", QtInfoMsg)
Q_LOGGING_CATEGORY(lcAudioRx, "qk4.audio.rx", QtInfoMsg)
Q_LOGGING_CATEGORY(lcAudioTx, "qk4.audio.tx", QtInfoMsg)
Q_LOGGING_CATEGORY(lcAudioSidetone, "qk4.audio.sidetone", QtInfoMsg)

Q_LOGGING_CATEGORY(lcCwKeyer, "qk4.cw.keyer", QtInfoMsg)
Q_LOGGING_CATEGORY(lcCwHalikey, "qk4.cw.halikey", QtInfoMsg)
Q_LOGGING_CATEGORY(lcCwMidi, "qk4.cw.midi", QtInfoMsg)
Q_LOGGING_CATEGORY(lcCwVail, "qk4.cw.vail", QtInfoMsg)

Q_LOGGING_CATEGORY(lcHardware, "qk4.hardware", QtInfoMsg)
Q_LOGGING_CATEGORY(lcKpod, "qk4.kpod", QtInfoMsg)
Q_LOGGING_CATEGORY(lcKpodUdev, "qk4.kpod.udev", QtInfoMsg)
Q_LOGGING_CATEGORY(lcKpodPlus, "qk4.kpod.plus", QtInfoMsg)

Q_LOGGING_CATEGORY(lcCatServer, "qk4.catserver", QtInfoMsg)
Q_LOGGING_CATEGORY(lcCatServerPush, "qk4.catserver.push", QtInfoMsg)
Q_LOGGING_CATEGORY(lcTci, "qk4.tci", QtInfoMsg)
Q_LOGGING_CATEGORY(lcTciWs, "qk4.tci.ws", QtInfoMsg)
Q_LOGGING_CATEGORY(lcDxCluster, "qk4.dxcluster", QtInfoMsg)
Q_LOGGING_CATEGORY(lcKpa1500, "qk4.kpa1500", QtInfoMsg)
Q_LOGGING_CATEGORY(lcKpa1500Ui, "qk4.kpa1500.ui", QtInfoMsg)

Q_LOGGING_CATEGORY(lcSpectrum, "qk4.spectrum", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSpectrumRender, "qk4.spectrum.render", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSpectrumMiniPan, "qk4.spectrum.minipan", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSpectrumDiag, "qk4.spectrum.diag", QtInfoMsg)

namespace Logging {

const std::vector<CategoryFn> &allCategories() {
    static const std::vector<CategoryFn> categories{
        lcApp,          lcUi,        lcUiMenu,        lcUiBandNav, lcUiModePopup, lcUiTextDecode,   lcUiMacro,
        lcLink,         lcLinkTcp,   lcLinkDiscovery, lcTxOwner,   lcCatTx,       lcCatRx,          lcAudio,
        lcAudioRx,      lcAudioTx,   lcAudioSidetone, lcCwKeyer,   lcCwHalikey,   lcCwMidi,         lcCwVail,
        lcHardware,     lcKpod,      lcKpodUdev,      lcKpodPlus,  lcCatServer,   lcCatServerPush,  lcTci,
        lcTciWs,        lcDxCluster, lcKpa1500,       lcKpa1500Ui, lcSpectrum,    lcSpectrumRender, lcSpectrumMiniPan,
        lcSpectrumDiag,
    };
    return categories;
}

} // namespace Logging
