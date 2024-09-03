#pragma once

#include "Container.h"
#include "Core/Action/ActionProducer.h"
#include "Core/Container/PrimitiveVector.h"
#include "Core/Primitive/UInt.h"
#include "Core/ProducerComponentArgs.h"
#include "NavigableAction.h"

template<typename T> struct Navigable : Container, ActionProducer<typename Action::Navigable<T>::Any> {
    using ActionT = typename Action::Navigable<T>;
    using ArgsT = ProducerComponentArgs<typename ActionT::Any>;
    using typename ActionProducer<typename ActionT::Any>::ProducedActionType;

    Navigable(ArgsT &&args) : Container(std::move(args.Args)), ActionProducer<ProducedActionType>(std::move(args.Q)) {}

    void IssueClear() const { ActionProducer<ProducedActionType>::Q(typename ActionT::Clear{Id}); }
    template<typename U> void IssuePush(U &&value) const { ActionProducer<ProducedActionType>::Q(typename ActionT::Push{Id, std::forward<U>(value)}); }
    void IssueMoveTo(u32 index) const { ActionProducer<ProducedActionType>::Q(typename ActionT::MoveTo{Id, index}); }
    void IssueStepForward() const { ActionProducer<ProducedActionType>::Q(typename ActionT::MoveTo{Id, u32(Cursor) + 1}); }
    void IssueStepBackward() const { ActionProducer<ProducedActionType>::Q(typename ActionT::MoveTo{Id, u32(Cursor) - 1}); }

    inline auto begin() { return Value.begin(); }
    inline auto end() { return Value.end(); }

    bool Empty() const { return Value.Empty(); }
    bool CanStepBackward() const { return u32(Cursor) > 0; }
    bool CanStepForward() const { return !Value.Empty() && u32(Cursor) < Value.Size() - 1u; }

    auto Back() const { return Value.back(); }

    inline auto operator[](u32 index) { return Value[index]; }
    T operator*() const { return Value[Cursor]; }

    void RenderValueTree(bool annotate, bool auto_select) const override {
        Component::RenderValueTree(annotate, auto_select);
    }

    Prop(PrimitiveVector<T>, Value);
    Prop(UInt, Cursor);
};
