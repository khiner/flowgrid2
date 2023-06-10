#pragma once

#include "Field.h"

struct Enum : TypedField<int>, MenuItemDrawable {
    Enum(Stateful *parent, string_view path_segment, string_view name_help, std::vector<string> names, int value = 0);
    Enum(Stateful *parent, string_view path_segment, string_view name_help, std::function<const string(int)> get_name, int value = 0);

    void Render(const std::vector<int> &options) const;
    void MenuItem() const override;

    const std::vector<string> Names;

private:
    void Render() const override;
    string OptionName(const int option) const;

    const std::optional<std::function<const string(int)>> GetName{};
};