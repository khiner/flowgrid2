#pragma once

#include "Core/Field/Field.h"
#include "Core/Primitive/Bool.h"

/*
A component that is created/destroyed dynamically.
Think of it like a store-backed `std::unique_ptr<ComponentType>`.
*/
template<typename ComponentType> struct DynamicComponent : Field {
    DynamicComponent(ComponentArgs &&args) : Field(std::move(args)) {
        ComponentContainerFields.insert(Id);
        ComponentContainerAuxiliaryFields.insert(HasValue.Id);
    }
    ~DynamicComponent() {
        ComponentContainerAuxiliaryFields.erase(HasValue.Id);
        ComponentContainerFields.erase(Id);
    }

    operator bool() const { return bool(Value); }
    auto operator->() const { return Value.get(); }
    inline auto Get() const { return Value.get(); }

    void Refresh() override {
        if (HasValue && !Value) Value = std::make_unique<ComponentType>(ComponentArgs{this, "Value"});
        else if (!HasValue && Value) Reset();
    }

    inline void IssueToggle() const {
        HasValue.IssueToggle();
    }

    inline void Erase() const override {
        HasValue.Set(false);
        if (Value) Value->Erase();
    }

    inline void Reset() { Value.reset(); }

    void RenderValueTree(bool annotate, bool auto_select) const override {
        if (!Value) {
            if (auto_select) ScrollToChanged();
            TextUnformatted(std::format("{} (empty)", Name));
            return;
        }

        if (TreeNode(Name)) {
            Value->RenderValueTree(annotate, auto_select);
            TreePop();
        }
    }

    Prop(Bool, HasValue, false);

private:
    std::unique_ptr<ComponentType> Value;
};
