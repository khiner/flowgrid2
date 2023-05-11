#pragma once

#include <unordered_map>

#include "Helper/Time.h"
#include "Primitive.h"

namespace fs = std::filesystem;

using StoreEntry = std::pair<StorePath, Primitive>;
using StoreEntries = std::vector<StoreEntry>;

struct StorePathHash {
    auto operator()(const StorePath &p) const noexcept { return fs::hash_value(p); }
};

struct PatchOp {
    enum Type {
        Add,
        Remove,
        Replace,
    };

    PatchOp::Type Op{};
    std::optional<Primitive> Value{}; // Present for add/replace
    std::optional<Primitive> Old{}; // Present for remove/replace
};

using PatchOps = std::unordered_map<StorePath, PatchOp, StorePathHash>;

static constexpr auto AddOp = PatchOp::Type::Add;
static constexpr auto RemoveOp = PatchOp::Type::Remove;
static constexpr auto ReplaceOp = PatchOp::Type::Replace;

struct Patch {
    PatchOps Ops;
    StorePath BasePath{RootPath};

    bool Empty() const noexcept { return Ops.empty(); }
};

struct StatePatch {
    Patch Patch{};
    TimePoint Time{};
};

string to_string(PatchOp::Type);