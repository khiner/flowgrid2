#pragma once

#include "Field.h"

struct String : TypedField<string> {
    String(ComponentArgs &&, string_view value = "");

    operator bool() const;
    operator string_view() const;

    void Render(const std::vector<string> &options) const;

private:
    void Render() const override;
};
