#pragma once

#include "immer/set.hpp"

#include "Core/Component.h"

template<typename T> struct PrimitiveSet : Component {
    using ContainerT = immer::set<T>;

    PrimitiveSet(ComponentArgs &&args) : Component(std::move(args)) {
        FieldIds.insert(Id);
        Refresh();
    }
    ~PrimitiveSet() {
        Erase();
        FieldIds.erase(Id);
    }

    void Refresh() override {} // Not cached.
    void RenderValueTree(bool annotate, bool auto_select) const override;

    void SetJson(json &&) const override;
    json ToJson() const override;

    bool Contains(const T &value) const { return Get().count(value); }
    bool Empty() const { return Get().empty(); }

    void Insert(const T &) const;
    void Erase_(const T &) const;
    void Clear() const;

    ContainerT Get() const;

    bool Exists() const; // Check if exists in store.
    void Erase() const override;
};
