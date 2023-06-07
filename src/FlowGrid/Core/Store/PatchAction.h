#pragma once

#include "Core/Action/Action.h"
#include "Core/Store/PatchJson.h"

namespace Action {
Define(ApplyPatch, 1, CustomMerge, "", Patch patch;);

Json(ApplyPatch, patch);

using Patch = ActionVariant<ApplyPatch>;
} // namespace Action
