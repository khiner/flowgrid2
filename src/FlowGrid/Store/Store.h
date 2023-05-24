#pragma once

#include "StoreFwd.h"

#include "../StateMember.h"

using std::vector;

// A `Field` is a drawable state-member that wraps around a primitive type.
namespace Field {

// TODO methods for these non-primitive fields are actually defined in `App.cpp`, because of circular dependencies.
template<IsPrimitive T>
struct Vector : Base {
    using Base::Base;

    StorePath PathAt(const Count i) const;
    Count Size() const;
    T operator[](const Count i) const;
    void Set(const vector<T> &) const;
    void Set(const vector<std::pair<int, T>> &) const;

    void Update() override;

private:
    vector<T> Value;
};

// Vector of vectors. Inner vectors need not have the same length.
template<IsPrimitive T>
struct Vector2D : Base {
    using Base::Base;

    StorePath PathAt(const Count i, const Count j) const;
    Count Size() const; // Number of outer vectors
    Count Size(Count i) const; // Size of inner vector at index `i`

    T operator()(Count i, Count j) const;
    void Set(const vector<vector<T>> &) const;

    void Update() override;

private:
    vector<vector<T>> Value;
};

template<IsPrimitive T>
struct Matrix : Base {
    using Base::Base;

    StorePath PathAt(const Count row, const Count col) const;
    Count Rows() const;
    Count Cols() const;
    T operator()(const Count row, const Count col) const;

    void Update() override;

private:
    Count RowCount, ColCount;
    vector<T> Data;
};
} // namespace Field

namespace store {
void OnApplicationStateInitialized();

void BeginTransient();
const Store EndTransient(bool commit = true);
TransientStore &GetTransient(); // xxx temporary until all sets are done internally.
bool IsTransientMode();
Store GetPersistent();

Primitive Get(const StorePath &);
Count CountAt(const StorePath &);

void Set(const StorePath &, const Primitive &);
void Set(const StoreEntries &);
void Set(const StorePath &, const vector<Primitive> &);
void Set(const StorePath &, const vector<Primitive> &, Count row_count); // For `SetMatrix` action.

void Erase(const StorePath &);

// Overwrite the main application store.
// This is the only place `ApplicationStore` is modified.
void Set(const Store &);

Patch CreatePatch(const Store &before, const Store &after, const StorePath &BasePath = RootPath);
void ApplyPatch(const Patch &);

} // namespace store
