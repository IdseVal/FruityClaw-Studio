// The section 6.3 provenance mark, shared by every surface that shows a
// Sample or a Pattern: the small "AI" logo painted in a corner, and the
// sentence shown when the item is opened up. One definition so the two
// entities that carry Provenance are marked identically, and so the section
// 6.2 licence caveat is worded in exactly one place.
//
// The mark and the caveat are two different questions with two different
// answers (ADR-062, O-19.1). The mark follows `ai_origin` and so appears on
// everything the Assistant authored; the caveat follows `generative_output`
// and so appears only on `generate_pattern` and `generate_sample` output. A
// Pattern the Assistant tidied is AI-authored and is marked; it is the user's
// own material and section 6.1 still promises it is releasable.
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

// "Made by hand"; or who authored it and when, with the section 6.2 caveat
// appended for generative output only.
QString provenance_text(const core::Provenance& provenance);

// Paints the small logo into `corner`. Callers decide which corner.
void paint_provenance_mark(QPainter& painter, const QRect& corner);

}  // namespace ui
