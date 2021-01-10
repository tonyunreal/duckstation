#pragma once
#include "common/types.h"
#include "imgui.h"

namespace ImGuiFullscreen {
#define HEX_TO_IMVEC4(hex, alpha)                                                                                      \
  ImVec4(static_cast<float>((hex >> 16) & 0xFFu) / 255.0f, static_cast<float>((hex >> 8) & 0xFFu) / 255.0f,            \
         static_cast<float>(hex & 0xFFu) / 255.0f, static_cast<float>(alpha) / 255.0f)

static constexpr float LAYOUT_SCREEN_WIDTH = 1280.0f;
static constexpr float LAYOUT_SCREEN_HEIGHT = 720.0f;
static constexpr float LAYOUT_LARGE_FONT_SIZE = 26.0f;
static constexpr float LAYOUT_MEDIUM_FONT_SIZE = 16.0f;
static constexpr float LAYOUT_SMALL_FONT_SIZE = 10.0f;
static constexpr float LAYOUT_MENU_BUTTON_HEIGHT = 60.0f;
static constexpr float LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY = 40.0f;
static constexpr float LAYOUT_MENU_BUTTON_X_PADDING = 4.0f;

extern ImFont* g_standard_font;
extern ImFont* g_medium_font;
extern ImFont* g_large_font;

extern float g_layout_scale;
extern float g_layout_padding_left;
extern float g_layout_padding_top;

static ALWAYS_INLINE float DPIScale(float v)
{
  return ImGui::GetIO().DisplayFramebufferScale.x * v;
}

static ALWAYS_INLINE float DPIScale(int v)
{
  return ImGui::GetIO().DisplayFramebufferScale.x * static_cast<float>(v);
}

static ALWAYS_INLINE ImVec2 DPIScale(const ImVec2& v)
{
  const ImVec2& fbs = ImGui::GetIO().DisplayFramebufferScale;
  return ImVec2(v.x * fbs.x, v.y * fbs.y);
}

static ALWAYS_INLINE float WindowWidthScale(float v)
{
  return ImGui::GetWindowWidth() * v;
}

static ALWAYS_INLINE float WindowHeightScale(float v)
{
  return ImGui::GetWindowHeight() * v;
}

static ALWAYS_INLINE float LayoutScale(float v)
{
  return g_layout_scale * v;
}

static ALWAYS_INLINE ImVec2 LayoutScale(const ImVec2& v)
{
  return ImVec2(v.x * g_layout_scale, v.y * g_layout_scale);
}

static ALWAYS_INLINE ImVec2 LayoutScale(float x, float y)
{
  return ImVec2(x * g_layout_scale, y * g_layout_scale);
}

static ALWAYS_INLINE ImVec4 UIPrimaryColor()
{
  return HEX_TO_IMVEC4(0x212121, 0xff);
}

static ALWAYS_INLINE ImVec4 UIPrimaryLightColor()
{
  return HEX_TO_IMVEC4(0x484848, 0xff);
}

static ALWAYS_INLINE ImVec4 UIPrimaryDarkColor()
{
  return HEX_TO_IMVEC4(0x484848, 0xff);
}

static ALWAYS_INLINE ImVec4 UIPrimaryTextColor()
{
  return HEX_TO_IMVEC4(0xffffff, 0xff);
}

static ALWAYS_INLINE ImVec4 UIPrimaryDisabledTextColor()
{
  return HEX_TO_IMVEC4(0xaaaaaa, 0xff);
}

static ALWAYS_INLINE ImVec4 UITextHighlightColor()
{
  return HEX_TO_IMVEC4(0x90caf9, 0xff);
}

static ALWAYS_INLINE ImVec4 UIPrimaryLineColor()
{
  return HEX_TO_IMVEC4(0xffffff, 0xff);
}

static ALWAYS_INLINE ImVec4 UISecondaryColor()
{
  return HEX_TO_IMVEC4(0x1565c0, 0xff);
}

static ALWAYS_INLINE ImVec4 UISecondaryLightColor()
{
  return HEX_TO_IMVEC4(0x5e92f3, 0xff);
}

static ALWAYS_INLINE ImVec4 UISecondaryDarkColor()
{
  return HEX_TO_IMVEC4(0x003c8f, 0xff);
}

static ALWAYS_INLINE ImVec4 UISecondaryTextColor()
{
  return HEX_TO_IMVEC4(0xffffff, 0xff);
}

void SetFont(const char* filename, float size_pixels, const ImWchar* glyph_ranges);

/// Rebuilds fonts to a new scale if needed. Returns true if fonts have changed and the texture needs updating.
bool UpdateFonts();

bool UpdateLayoutScale();

void BeginLayout();
void EndLayout();

bool BeginFullscreenColumnFractionWindow(float start_frac, float end_frac, const char* name);

bool BeginFullscreenColumnWindow(float start, float end, const char* name,
                                 const ImVec4& background = HEX_TO_IMVEC4(0x212121, 0xFF));
bool BeginFullscreenWindow(float left, float top, float width, float height, const char* name,
                           const ImVec4& background = HEX_TO_IMVEC4(0x212121, 0xFF));
void EndFullscreenWindow();

void BeginMenuButtons(u32 num_items, bool center);
void EndMenuButtons();
bool MenuCategory(const char* title, bool is_active, bool enabled = true,
                  float height = LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY, ImFont* font = g_large_font);
bool MenuButton(const char* title, const char* summary = nullptr);
bool MenuImageButton(const char* title, const char* summary, ImTextureID user_texture_id, const ImVec2& image_size,
                     const ImVec2& uv0 = ImVec2(0.0f, 0.0f), const ImVec2& uv1 = ImVec2(1.0f, 1.0f));
bool ToggleButton(const char* title, const char* summary, bool* v, bool enabled = true);
bool EnumChoiceButtonImpl(const char* title, const char* summary, s32* value_pointer,
                          const char* (*to_display_name_function)(s32 value, void* opaque), void* opaque, u32 count);

template<typename DataType, typename CountType>
static ALWAYS_INLINE bool EnumChoiceButton(const char* title, const char* summary, DataType* value_pointer,
                                           const char* (*to_display_name_function)(DataType value), CountType count)
{
  s32 value = static_cast<s32>(*value_pointer);
  auto to_display_name_wrapper = [](s32 value, void* opaque) -> const char* {
    return static_cast<decltype(to_display_name_function)>(opaque)(static_cast<DataType>(value));
  };
  return EnumChoiceButtonImpl(title, summary, &value, to_display_name_wrapper, to_display_name_function,
                              static_cast<u32>(count));
}

} // namespace ImGuiFullscreen