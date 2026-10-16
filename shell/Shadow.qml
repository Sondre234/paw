// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// A shadow drawn through the GPU that frees what it draws with as it goes: Qt's
// RectangularShadow (6.11) never deletes the shader effect it makes for itself, and the shell
// gives that effect to the shadow (ShellController::keepShadowMaterial).
RectangularShadow {
    Component.onCompleted: shell.keepShadowMaterial(this)
}
