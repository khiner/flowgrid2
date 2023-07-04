#include "PrimitiveField.h"

#include "Core/Store/Store.h"

template<IsPrimitive T> T PrimitiveField<T>::Get() const { return std::get<T>(store.Get(Path)); }
template<IsPrimitive T> void PrimitiveField<T>::Set(const T &value) const { store.Set(Path, value); }

template<IsPrimitive T> void PrimitiveField<T>::RenderValueTree(bool annotate, bool auto_select) const {
    Field::RenderValueTree(annotate, auto_select);
    TreeNode(Name, false, std::format("{}", Value).c_str());
}

// Explicit instantiations.
template struct PrimitiveField<bool>;
template struct PrimitiveField<int>;
template struct PrimitiveField<U32>;
template struct PrimitiveField<float>;
template struct PrimitiveField<std::string>;
