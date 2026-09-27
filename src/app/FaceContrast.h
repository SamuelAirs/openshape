// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>

class QImage;
class QQuickWindow;

namespace os::interact {
class InteractionController;
}

namespace os::app {

// Developer measurement (`--face-contrast` with `--screenshot`): how clearly
// the faces of the bodies on screen differ in the rendered image. Every face
// is sampled where it is visible (not behind another face, clear of edges
// and of the QML overlays, not highlighted); its shade is the median luma
// (0-255) of its samples. Faces that meet at a sharp edge (normals more than
// 20 degrees apart along it) are the pairs that must look different. The
// one-line summary: faces measured, sharp pairs, their smallest and median
// difference, how many differ by less than 8 levels, and the darkest and
// lightest face.
QString faceContrastReport(const QImage& image, QQuickWindow* window, const interact::InteractionController& interaction);

} // namespace os::app
