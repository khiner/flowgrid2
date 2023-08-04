#pragma once

#include "Core/Field/Field.h"
#include "PrimitiveVectorAction.h"

template<IsPrimitive T> struct PrimitiveVector : Field, Actionable<typename Action::PrimitiveVector<T>::Any> {
    using Field::Field;

    // `ActionType` is a type alias in `Actionable`, but it is not accessible here.
    // `Actionable` is templated on `Action::PrimitiveVector::Type<T>::type`, which is a dependent type (it depends on `T`),
    // and base class members that use dependent template types are not visible in subclasses at compile time.
    using typename Actionable<typename Action::PrimitiveVector<T>::Any>::ActionType;

    void Apply(const ActionType &action) const override {
        Visit(
            action,
            [this](const Action::PrimitiveVector<T>::SetAt &a) { Set(a.i, a.value); },
        );
    }
    bool CanApply(const ActionType &) const override { return true; }

    void Refresh() override;
    void RenderValueTree(bool annotate, bool auto_select) const override;

    void SetJson(json &&) const override;
    json ToJson() const override;

    T operator[](u32 i) const { return Value[i]; }
    bool Contains(const T &value) const { return std::find(Value.begin(), Value.end(), value) != Value.end(); }
    u32 IndexOf(const T &value) const { return std::find(Value.begin(), Value.end(), value) - Value.begin(); }

    auto begin() const { return Value.begin(); }
    auto end() const { return Value.end(); }

    StorePath PathAt(u32 i) const { return Path / to_string(i); }
    u32 Size() const { return Value.size(); }

    void Set(const std::vector<T> &) const;
    void Set(size_t i, const T &) const;
    void Set(const std::vector<std::pair<int, T>> &) const;
    void PushBack(const T &) const;
    void Resize(u32) const;
    void Erase() const override;
    void Erase(const T &) const;

    void PushBack_(const T &);

protected:
    std::vector<T> Value;
};