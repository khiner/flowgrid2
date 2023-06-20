#pragma once

#include "Core/Action/DefineAction.h"
#include "Core/Json.h"

DefineNestedActionType(
    Primitive, UInt,
    DefineFieldAction(Set, "", unsigned int value;);
    Json(Set, path, value);

    using Any = ActionVariant<Set>;
);
