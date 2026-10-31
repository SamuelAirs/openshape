// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Portions of this file are adapted from OpenShape3D
// (https://github.com/laanlabs/openshape3d, commit be3f2fc),
// Copyright (c) 2026 Laan Labs and the openshape3d contributors,
// licensed under the MIT License (third_party/openshape3d/LICENSE).

#pragma once

// Private to the geometry library: cheap checks on untrusted OCCT BRep text
// (project files) before OCCT's reader sees it, and on shapes before they
// are meshed.

#include <TopoDS_Shape.hxx>

#include <string>
#include <string_view>

namespace os::geom::detail {

// Whether `text` could be what BRepTools::Write produced, judged by its
// bytes and numbers alone (one pass, no parsing of the geometry). OCCT's
// reader trusts the counts and indices in the text: an inflated section
// count makes it loop or allocate for records that are not there (minutes
// on a 2 KB text), an out-of-range sub-shape reference indexes its tables
// unchecked (release builds of OCCT compile the range checks out), and an
// infinite coordinate breaks the geometry it builds. Refused:
//  - bytes other than printable ASCII, tab, LF and CR (BRepTools::Write
//    writes nothing else; a NUL or 0xFF derails the stream's number parsing);
//  - a section count ("Curves 12", "TShapes 34") larger than the text;
//  - a TShapes count other than the number of records ("Ve", "Ed", ...
//    lines), and a text without a TShapes section or a root reference;
//  - in the TShapes section, a sub-shape reference (the lines of pairs
//    "+12 0 -3 2 ... *" and the root reference at the end) to a record not
//    read before it (OCCT indexes its table with it unchecked: release
//    builds compile the range checks out) or with a location outside
//    0..Locations;
//  - a token of 128 characters or more (OCCT reads some into 255-byte
//    buffers on the stack; BRepTools::Write's longest are about 25);
//  - any other integer larger in size than max(text length, 10^7): counts
//    and indices need at least a byte per record, and 10^7 mm (10 km) is
//    far beyond any coordinate OpenShape writes;
//  - a doubled sign ("+-5"), a hexadecimal number ("0x1p3", which the
//    stream reads as 0 and then desynchronizes) and a number that parses
//    to infinity or NaN ("1e999", "inf", "nan").
// `why` (optional) gets the reason, for the developer message.
// Adapted from OS3DPlausibleSectionCounts.
bool plausibleBrepText(std::string_view text, std::string* why = nullptr);

// Whether the shape has a non-empty bounding box with finite coordinates.
// A shape with an infinite or NaN coordinate can make the mesher loop
// forever; an open box (an infinite shape) and coordinates beyond 10^9 mm
// are refused too. Adapted from OS3DFiniteBounds.
bool finiteBounds(const TopoDS_Shape& shape);

} // namespace os::geom::detail
