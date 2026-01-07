#pragma once

#include "imgui.h"

#include <format>

// ImGui Extended/Extensions
namespace ImGuiEx
{
    template<typename... Args>
    void TextFormat(std::format_string<Args...> fmt, Args&&... args)
    {
        const std::string text = std::format(fmt, std::forward<Args>(args)...);
        ImGui::TextUnformatted(text.c_str());
    }
}