#ifndef LOGGING_LOGCATEGORIES_H
#define LOGGING_LOGCATEGORIES_H

#include <QLoggingCategory>
#include <vector>

// Every QK4 logging category, declared once. Names follow qk4.<area>[.<detail>]; every category is
// defined at QtInfoMsg, so debug output is off until a rule turns it on. See src/logging/README.md
// for the old names and how to enable a category.

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcUi)
Q_DECLARE_LOGGING_CATEGORY(lcUiMenu)
Q_DECLARE_LOGGING_CATEGORY(lcUiBandNav)
Q_DECLARE_LOGGING_CATEGORY(lcUiModePopup)
Q_DECLARE_LOGGING_CATEGORY(lcUiTextDecode)
Q_DECLARE_LOGGING_CATEGORY(lcUiMacro)

Q_DECLARE_LOGGING_CATEGORY(lcLink)
Q_DECLARE_LOGGING_CATEGORY(lcLinkTcp)
Q_DECLARE_LOGGING_CATEGORY(lcLinkDiscovery)
Q_DECLARE_LOGGING_CATEGORY(lcTxOwner)
Q_DECLARE_LOGGING_CATEGORY(lcCatTx)
Q_DECLARE_LOGGING_CATEGORY(lcCatRx)

Q_DECLARE_LOGGING_CATEGORY(lcAudio)
Q_DECLARE_LOGGING_CATEGORY(lcAudioRx)
Q_DECLARE_LOGGING_CATEGORY(lcAudioTx)
Q_DECLARE_LOGGING_CATEGORY(lcAudioSidetone)

Q_DECLARE_LOGGING_CATEGORY(lcCwKeyer)
Q_DECLARE_LOGGING_CATEGORY(lcCwHalikey)
Q_DECLARE_LOGGING_CATEGORY(lcCwMidi)
Q_DECLARE_LOGGING_CATEGORY(lcCwVail)

Q_DECLARE_LOGGING_CATEGORY(lcHardware)
Q_DECLARE_LOGGING_CATEGORY(lcKpod)
Q_DECLARE_LOGGING_CATEGORY(lcKpodUdev)
Q_DECLARE_LOGGING_CATEGORY(lcKpodPlus)

Q_DECLARE_LOGGING_CATEGORY(lcCatServer)
Q_DECLARE_LOGGING_CATEGORY(lcCatServerPush)
Q_DECLARE_LOGGING_CATEGORY(lcTci)
Q_DECLARE_LOGGING_CATEGORY(lcTciWs)
Q_DECLARE_LOGGING_CATEGORY(lcDxCluster)
Q_DECLARE_LOGGING_CATEGORY(lcKpa1500)
Q_DECLARE_LOGGING_CATEGORY(lcKpa1500Ui)

Q_DECLARE_LOGGING_CATEGORY(lcSpectrum)
Q_DECLARE_LOGGING_CATEGORY(lcSpectrumRender)
Q_DECLARE_LOGGING_CATEGORY(lcSpectrumMiniPan)
Q_DECLARE_LOGGING_CATEGORY(lcSpectrumDiag)

namespace Logging {

using CategoryFn = const QLoggingCategory &(*)();

// Every category above, in declaration order, for tests and the Options page.
const std::vector<CategoryFn> &allCategories();

} // namespace Logging

#endif // LOGGING_LOGCATEGORIES_H
