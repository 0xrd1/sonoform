#include "ProceduralShapes.h"
#include "ShapeField.h"

void ProceduralShapeProvider::BakeInto(ShapeField& field, float time) {
    (void)time; // analytic shapes here are static; a future animated/morphing provider would use it
    field.BakeProcedural(static_cast<int>(type_));
}
