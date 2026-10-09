// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A loudspeaker with a wave per half of the volume, or crossed out while muted, crossfading as
// it changes.
FadingIcon {
    property int level: 0
    property bool muted: false
    name: muted || level === 0 ? "volume-x" : level > 50 ? "volume-2" : "volume-1"
}
