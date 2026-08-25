// The section 6.3 provenance mark, shared by every surface that shows a
// Sample or a Pattern: the small "AI" logo painted in a corner, and the
// sentence shown when the item is opened up. One definition so the two
// entities that carry Provenance are marked identically, and so the section
// 6.2 licence caveat is worded in exactly one place.
//
// Everything here is derived from core::Provenance at paint time; nothing is
// stored in the UI (project-data-model spec section 4, rule P5).
#pragma once

#include <QRect>
#include <QString>

#include "core/entities.h"

class QPainter;

namespace ui {

// The accessible name of the mark, for screen readers and tests.
inline const QString kProvenanceMarkLabel = "AI-generated";

// "Made by hand", or who generated it, when, and the section 6.2 caveat.
QString provenance_text(const core::Provenance& provenance);

// Paints the small logo into `corner`. Callers decide which corner.
void paint_provenance_mark(QPainter& painter, const QRect& corner);

}  // namespace ui
