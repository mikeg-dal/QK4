// SPDX-License-Identifier: GPL-3.0-or-later
//
// The macOS half of RtScheduling::LatencyCriticalActivity: an NSProcessInfo activity, held while
// QK4 is connected to the radio.

#include "utils/rtscheduling.h"

#import <Foundation/Foundation.h>

namespace RtScheduling {

void LatencyCriticalActivity::begin() {
    if (m_token)
        return;
    // WHY these options: UserInitiated keeps App Nap from suspending or throttling QK4 while its
    // window is hidden or occluded and the radio is quiet; LatencyCritical also stops timer
    // coalescing, which would otherwise stretch the keyer's and the HaliKey poll's waits.
    id<NSObject> activity = [[NSProcessInfo processInfo]
        beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical
                          reason:@"Connected to an Elecraft K4: CW keying and audio are timing-critical"];
    m_token = (__bridge_retained void *)activity;
}

void LatencyCriticalActivity::end() {
    if (!m_token)
        return;
    id<NSObject> activity = (__bridge_transfer id<NSObject>)m_token;
    m_token = nullptr;
    [[NSProcessInfo processInfo] endActivity:activity];
}

} // namespace RtScheduling
