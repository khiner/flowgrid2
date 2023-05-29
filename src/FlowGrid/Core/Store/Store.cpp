#include "Store.h"

#include "immer/algorithm.hpp"
#include "immer/map.hpp"
#include "immer/map_transient.hpp"
#include <range/v3/core.hpp>
#include <range/v3/view/map.hpp>

Store ApplicationStore{};
const Store &AppStore = ApplicationStore;

namespace store {
void Apply(const Action::StoreAction &action) {
    using namespace Action;
    Match(
        action,
        [](const SetValue &a) { Set(a.path, a.value); },
        [](const SetValues &a) { Set(a.values); },
        [](const SetVector &a) { Set(a.path, a.value); },
        [](const SetMatrix &a) { Set(a.path, a.data, a.row_count); },
        [](const ToggleValue &a) { Set(a.path, !std::get<bool>(store::Get(a.path))); },
        [](const Action::ApplyPatch &a) { ApplyPatch(a.patch); },
    );
}

bool IsTransient = true; // Use transient store for initialization.

bool IsTransientMode() { return IsTransient; }

TransientStore Transient{};

void BeginTransient() {
    if (IsTransient) return;

    Transient = AppStore.transient();
    IsTransient = true;
}
const Store EndTransient() {
    if (!IsTransient) return AppStore;

    const Store new_store = Transient.persistent();
    Transient = {};
    IsTransient = false;

    return new_store;
}
void CommitTransient() {
    if (!IsTransient) return;

    Set(Transient.persistent());
    Transient = {};
    IsTransient = false;
}

TransientStore &GetTransient() { return Transient; }
Store GetPersistent() { return Transient.persistent(); }

Primitive Get(const StorePath &path) { return IsTransient ? Transient.at(path) : AppStore.at(path); }
Count CountAt(const StorePath &path) { return IsTransient ? Transient.count(path) : AppStore.count(path); }

Patch CreatePatch(const Store &before, const Store &after, const StorePath &base_path) {
    PatchOps ops{};
    diff(
        before,
        after,
        [&](auto const &added_element) {
            ops[added_element.first.lexically_relative(base_path)] = {PatchOp::Type::Add, added_element.second, {}};
        },
        [&](auto const &removed_element) {
            ops[removed_element.first.lexically_relative(base_path)] = {PatchOp::Type::Remove, {}, removed_element.second};
        },
        [&](auto const &old_element, auto const &new_element) {
            ops[old_element.first.lexically_relative(base_path)] = {PatchOp::Type::Replace, new_element.second, old_element.second};
        }
    );

    return {ops, base_path};
}

Patch CreatePatch(const Store &store, const StorePath &base_path) {
    return CreatePatch(AppStore, store, base_path);
}

Patch CreatePatch(const StorePath &base_path) {
    return CreatePatch(AppStore, store::EndTransient(), base_path);
}

void ApplyPatch(const Patch &patch) {
    auto &store = Transient;
    for (const auto &[partial_path, op] : patch.Ops) {
        const auto &path = patch.BasePath / partial_path;
        if (op.Op == PatchOp::Type::Add || op.Op == PatchOp::Type::Replace) store.set(path, *op.Value);
        else if (op.Op == PatchOp::Type::Remove) store.erase(path);
    }
}

// Transient modifiers
void Set(const StorePath &path, const Primitive &value) {
    auto &store = Transient;
    store.set(path, value);
}

void Set(const StoreEntries &values) {
    auto &store = Transient;
    for (const auto &[path, value] : values) store.set(path, value);
}
void Set(const StorePath &path, const vector<Primitive> &values) {
    auto &store = Transient;
    Count i = 0;
    while (i < values.size()) {
        store.set(path / to_string(i), values[i]);
        i++;
    }
    while (store.count(path / to_string(i))) {
        store.erase(path / to_string(i));
        i++;
    }
}

void Set(const StorePath &path, const vector<Primitive> &data, const Count row_count) {
    assert(data.size() % row_count == 0);
    auto &store = Transient;
    const Count col_count = data.size() / row_count;
    Count row = 0;
    while (row < row_count) {
        Count col = 0;
        while (col < col_count) {
            store.set(path / to_string(row) / to_string(col), data[row * col_count + col]);
            col++;
        }
        while (store.count(path / to_string(row) / to_string(col))) {
            store.erase(path / to_string(row) / to_string(col));
            col++;
        }
        row++;
    }

    while (store.count(path / to_string(row) / to_string(0))) {
        Count col = 0;
        while (store.count(path / to_string(row) / to_string(col))) {
            store.erase(path / to_string(row) / to_string(col));
            col++;
        }
        row++;
    }
}

void Erase(const StorePath &path) {
    auto &store = Transient;
    store.erase(path);
}

void Set(const Store &store) {
    ApplicationStore = store;
}
} // namespace store