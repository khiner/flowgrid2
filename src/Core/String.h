#pragma once

#include "Component.h"

struct String : Component {
    String(ComponentArgs &&, std::string_view value = "");
    ~String();

    std::string_view Get() const;

    operator std::string_view() const { return Get(); }
    operator fs::path() const { return std::string(*this); }
    operator bool() const { return !Get().empty(); }

    void Render(const std::vector<std::string> &options) const;

    json ToJson() const override;
    void SetJson(TransientStore &, json &&) const override;

    void IssueSet(std::string_view) const;
    void Set(TransientStore &, std::string_view) const;

    void RenderValueTree(bool annotate, bool auto_select) const override;

    void Erase(TransientStore &) const override;

private:
    void Render() const override;
};
