#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui_fullscreen.h"
#include "IconsFontAwesome5.h"
#include "IconsKenney.h"
#include "common/assert.h"
#include "common/string.h"
#include "core/host_display.h"
#include "core/host_interface.h"
#include "imgui_internal.h"
#include "imgui_styles.h"
#include <cmath>

namespace ImGuiFullscreen {
ImFont* g_standard_font = nullptr;
ImFont* g_medium_font = nullptr;
ImFont* g_large_font = nullptr;
ImFont* g_icon_font = nullptr;

float g_layout_scale = 1.0f;
float g_layout_padding_left = 0.0f;
float g_layout_padding_top = 0.0f;

static std::string s_font_filename;
static float s_font_size = 15.0f;
static const ImWchar* s_font_glyph_range = nullptr;

static u32 s_menu_button_index = 0;

static ImRect PadRect(const ImRect& r, float padding)
{
  return ImRect(ImVec2(r.Min.x + padding, r.Min.y + padding), ImVec2(r.Max.x - padding, r.Max.y - padding));
}

void SetFont(const char* filename, float size_pixels, const ImWchar* glyph_ranges)
{
  if (filename)
    s_font_filename = filename;
  else
    std::string().swap(s_font_filename);
  s_font_size = size_pixels;
  s_font_glyph_range = glyph_ranges;
}

static void AddIconFonts(float size)
{
  static const ImWchar range_fa[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  static const ImWchar range_kenney[] = {ICON_MIN_KI, ICON_MAX_KI, 0};

  ImFontConfig cfg;
  cfg.MergeMode = true;
  cfg.PixelSnapH = true;
  cfg.GlyphMinAdvanceX = size * 0.75f;
  cfg.GlyphMaxAdvanceX = size * 0.75f;

  ImGui::GetIO().Fonts->AddFontFromFileTTF("resources\\fa-solid-900.ttf", size * 0.75f, &cfg, range_fa);

  cfg.GlyphMinAdvanceX = size;
  cfg.GlyphMaxAdvanceX = size;
  ImGui::GetIO().Fonts->AddFontFromFileTTF("resources\\kenney-icon-font.ttf", size, &cfg, range_kenney);
}

bool UpdateFonts()
{
  const float standard_font_size = std::round(DPIScale(s_font_size));
  const float medium_font_size = std::round(LayoutScale(LAYOUT_MEDIUM_FONT_SIZE));
  const float large_font_size = std::round(LayoutScale(LAYOUT_LARGE_FONT_SIZE));

  if (g_standard_font && g_standard_font->FontSize == standard_font_size && medium_font_size &&
      g_medium_font->FontSize == medium_font_size && large_font_size && g_large_font->FontSize == large_font_size)
  {
    return false;
  }

  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();

  if (s_font_filename.empty())
  {
    g_standard_font = ImGui::AddRobotoRegularFont(standard_font_size);
    AddIconFonts(standard_font_size);
    g_medium_font = ImGui::AddRobotoRegularFont(medium_font_size);
    AddIconFonts(medium_font_size);
    g_large_font = ImGui::AddRobotoRegularFont(large_font_size);
    AddIconFonts(large_font_size);
  }
  else
  {
    g_standard_font =
      io.Fonts->AddFontFromFileTTF(s_font_filename.c_str(), standard_font_size, nullptr, s_font_glyph_range);
    AddIconFonts(standard_font_size);
    g_medium_font =
      io.Fonts->AddFontFromFileTTF(s_font_filename.c_str(), medium_font_size, nullptr, s_font_glyph_range);
    AddIconFonts(medium_font_size);
    g_large_font = io.Fonts->AddFontFromFileTTF(s_font_filename.c_str(), large_font_size, nullptr, s_font_glyph_range);
    AddIconFonts(large_font_size);
  }

  if (!io.Fonts->Build())
    Panic("Failed to rebuild font atlas");

  return true;
}

bool UpdateLayoutScale()
{
  static constexpr float LAYOUT_RATIO = LAYOUT_SCREEN_WIDTH / LAYOUT_SCREEN_HEIGHT;
  const ImGuiIO& io = ImGui::GetIO();

  const float menu_margin = DPIScale(20.0f);
  const float screen_width = io.DisplaySize.x;
  const float screen_height = io.DisplaySize.y - menu_margin;
  const float screen_ratio = screen_width / screen_height;
  const float old_scale = g_layout_scale;

  if (screen_ratio > LAYOUT_RATIO)
  {
    // screen is wider, use height, pad width
    g_layout_scale = screen_height / LAYOUT_SCREEN_HEIGHT;
    g_layout_padding_top = menu_margin;
    g_layout_padding_left = (screen_width - (LAYOUT_SCREEN_WIDTH * g_layout_scale)) / 2.0f;
  }
  else
  {
    // screen is taller, use width, pad height
    g_layout_scale = screen_width / LAYOUT_SCREEN_WIDTH;
    g_layout_padding_top = (screen_height - (LAYOUT_SCREEN_HEIGHT * g_layout_scale)) / 2.0f + menu_margin;
    g_layout_padding_left = 0.0f;
  }

  return g_layout_scale != old_scale;
}

void BeginLayout()
{
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, UIPrimaryTextColor());
  ImGui::PushStyleColor(ImGuiCol_Button, UIPrimaryLineColor());
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, UISecondaryDarkColor());
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UISecondaryColor());
  ImGui::PushStyleColor(ImGuiCol_Border, UISecondaryLightColor());
}

void EndLayout()
{
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(2);
}

bool BeginFullscreenColumnFractionWindow(float start_frac, float end_frac, const char* name)
{
  const float size_frac = end_frac - start_frac;
  ImGui::SetNextWindowSize(LayoutScale(ImVec2(LAYOUT_SCREEN_WIDTH * size_frac, LAYOUT_SCREEN_HEIGHT)));
  ImGui::SetNextWindowPos(
    ImVec2(LayoutScale(LAYOUT_SCREEN_WIDTH * start_frac) + g_layout_padding_left, g_layout_padding_top));

  return ImGui::Begin(name, nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
}

bool BeginFullscreenColumnWindow(float start, float end, const char* name, const ImVec4& background)
{
  ImGui::SetNextWindowSize(LayoutScale(ImVec2(end - start, LAYOUT_SCREEN_HEIGHT)));
  ImGui::SetNextWindowPos(ImVec2(LayoutScale(start) + g_layout_padding_left, g_layout_padding_top));

  ImGui::PushStyleColor(ImGuiCol_WindowBg, background);

  return ImGui::Begin(name, nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
}

bool BeginFullscreenWindow(float left, float top, float width, float height, const char* name,
                           const ImVec4& background /* = HEX_TO_IMVEC4(0x212121, 0xFF) */)
{
  if (left < 0.0f)
    left = (LAYOUT_SCREEN_WIDTH - width) * -left;
  if (top < 0.0f)
    top = (LAYOUT_SCREEN_HEIGHT - height) * -top;

  ImGui::SetNextWindowSize(LayoutScale(ImVec2(width, height)));
  ImGui::SetNextWindowPos(ImVec2(LayoutScale(left) + g_layout_padding_left, LayoutScale(top) + g_layout_padding_top));

  ImGui::PushStyleColor(ImGuiCol_WindowBg, background);

  return ImGui::Begin(name, nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoBringToFrontOnFocus);
}

void EndFullscreenWindow()
{
  ImGui::End();
  ImGui::PopStyleColor();
}

void BeginMenuButtons(u32 num_items, bool center)
{
  s_menu_button_index = 0;

  // ImGui::PushStyleColor(ImGuiCol_Button, 0xFF404040);
  // ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFF404040);
  // ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF101010);
  // ImGui::PushStyleColor(ImGuiCol_Border, 0xFF0060FF);

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, LayoutScale(8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, LayoutScale(1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

  if (center)
  {
    const float total_size = static_cast<float>(num_items) * LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT);
    const float window_height = ImGui::GetWindowHeight();
    if (window_height > total_size)
      ImGui::SetCursorPosY((window_height - total_size) / 2.0f);
  }
}

void EndMenuButtons()
{
  // ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(3);
}

// TODO: We really want clipping for all of these..

bool MenuCategory(const char* title, bool is_active, bool enabled, float height, ImFont* font)
{
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const auto* window = ImGui::GetCurrentWindow();
  if (is_active)
  {
    const float x_pad = window->WindowBorderSize + window->WindowPadding.x;
    const ImVec2 top_left(window->DC.CursorPos.x - x_pad, window->DC.CursorPos.y);
    const ImVec2 bottom_right(top_left + ImVec2(window->InnerClipRect.GetWidth() + x_pad * 2.0f, LayoutScale(height)));
    ImGui::RenderFrame(top_left, bottom_right, ImGui::GetColorU32(UIPrimaryColor()), false);
  }

  const float scaled_height = LayoutScale(height);
  const float x_pad = ImMax(ImFloor(window->WindowPadding.x * 0.5f), window->WindowBorderSize) +
                      ImGui::GetStyle().FrameBorderSize + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING);
  const ImVec2 pos(window->DC.CursorPos + ImVec2(x_pad, window->WindowBorderSize));
  const ImVec2 size(window->InnerClipRect.GetWidth() - x_pad * 3.0f, scaled_height);
  const ImRect bb(pos, pos + size);

  bool pressed, hovered, held;
  if (enabled)
  {
    pressed = ImGui::InvisibleButton(title, size);
    hovered = ImGui::IsItemHovered();
    held = ImGui::IsItemActive();
  }
  else
  {
    ImGui::ItemSize(ImGui::CalcItemSize(size, 0.0f, 0.0f));

    pressed = false;
    hovered = false;
    held = false;
  }

  if (hovered)
  {
    const ImU32 col = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, 0.0f);
  }

  const ImRect title_bb(PadRect(
    ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y), pos + ImVec2(size.x, LayoutScale(50.0f))),
    LayoutScale(4.0f)));
  const ImRect summary_bb(
    PadRect(ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y + LayoutScale(32.0f)),
                   pos + ImVec2(size.x, size.y)),
            LayoutScale(4.0f)));

  ImGui::PushFont(font);
  ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, title, nullptr, nullptr, ImVec2(0.0f, 0.0f), &title_bb);
  ImGui::PopFont();

  s_menu_button_index++;
  return pressed;
}

bool MenuButton(const char* title, const char* summary)
{
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const auto* window = ImGui::GetCurrentWindow();
  const float item_height = LAYOUT_MENU_BUTTON_HEIGHT;
  const float x_pad = ImMax(ImFloor(window->WindowPadding.x * 0.5f), window->WindowBorderSize) +
                      ImGui::GetStyle().FrameBorderSize + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING);
  const ImVec2 pos(window->DC.CursorPos + ImVec2(x_pad, window->WindowBorderSize));
  const ImVec2 size(window->InnerClipRect.GetWidth() - x_pad * 3.0f, LayoutScale(item_height));
  const ImRect bb(pos, pos + size);

  const bool pressed = ImGui::InvisibleButton(title, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();

  const ImU32 line_col = ImGui::GetColorU32(ImGuiCol_Button);
  const float line_height = LayoutScale(1.0f);
#if 0
  if (s_menu_button_index == 0)
    dl->AddLine(bb.GetTL(), bb.GetTR(), line_col, line_height);

  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y), ImVec2(bb.Max.x, bb.Max.y), line_col, line_height);
#endif

  if (hovered)
  {
    const ImU32 col = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, 0.0f);
  }

  const ImRect title_bb(PadRect(
    ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y), pos + ImVec2(size.x, LayoutScale(50.0f))),
    LayoutScale(4.0f)));
  const ImRect summary_bb(
    PadRect(ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y + LayoutScale(32.0f)),
                   pos + ImVec2(size.x, size.y)),
            LayoutScale(4.0f)));

  ImGui::PushFont(g_large_font);
  ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, title, nullptr, nullptr, ImVec2(0.0f, 0.0f), &title_bb);
  ImGui::PopFont();

  if (summary)
  {
    ImGui::PushFont(g_medium_font);
    ImGui::RenderTextClipped(summary_bb.Min, summary_bb.Max, summary, nullptr, nullptr, ImVec2(0.0f, 0.0f),
                             &summary_bb);
    ImGui::PopFont();
  }

  s_menu_button_index++;
  return pressed;
}

bool MenuImageButton(const char* title, const char* summary, ImTextureID user_texture_id, const ImVec2& image_size,
                     const ImVec2& uv0, const ImVec2& uv1)
{
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const auto* window = ImGui::GetCurrentWindow();
  const float item_height = LAYOUT_MENU_BUTTON_HEIGHT;
  const float x_pad = ImMax(ImFloor(window->WindowPadding.x * 0.5f), window->WindowBorderSize) +
                      ImGui::GetStyle().FrameBorderSize + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING);
  const ImVec2 pos(window->DC.CursorPos + ImVec2(x_pad, window->WindowBorderSize));
  const ImVec2 size(window->InnerClipRect.GetWidth() - x_pad * 3.0f, LayoutScale(item_height));
  const ImRect bb(pos, pos + size);

  const bool pressed = ImGui::InvisibleButton(title, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();

  const ImU32 line_col = ImGui::GetColorU32(ImGuiCol_Button);
  const float line_height = LayoutScale(1.0f);
#if 0
  if (s_menu_button_index == 0)
    dl->AddLine(bb.GetTL(), bb.GetTR(), line_col, line_height);

  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y), ImVec2(bb.Max.x, bb.Max.y), line_col, line_height);
#endif

  if (hovered)
  {
    const ImU32 col = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, 0.0f);
  }

  const ImRect title_bb(PadRect(
    ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y), pos + ImVec2(size.x, LayoutScale(50.0f))),
    LayoutScale(4.0f)));
  const ImRect summary_bb(
    PadRect(ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y + LayoutScale(32.0f)),
                   pos + ImVec2(size.x, size.y)),
            LayoutScale(4.0f)));

  ImGui::PushFont(g_large_font);
  ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, title, nullptr, nullptr, ImVec2(0.0f, 0.0f), &title_bb);
  ImGui::PopFont();

  if (summary)
  {
    ImGui::PushFont(g_medium_font);
    ImGui::RenderTextClipped(summary_bb.Min, summary_bb.Max, summary, nullptr, nullptr, ImVec2(0.0f, 0.0f),
                             &summary_bb);
    ImGui::PopFont();
  }

  const float image_padding = ((size.y - image_size.y) / 2.0f);
  const ImVec2 image_bb_min(pos.x + size.x - image_size.x - image_padding, pos.y + image_padding);
  const ImVec2 image_bb_max(image_bb_min + image_size);
  dl->AddImage(user_texture_id, image_bb_min, image_bb_max, uv0, uv1);

  s_menu_button_index++;
  return pressed;
}

bool ToggleButton(const char* title, const char* summary, bool* v, bool enabled)
{
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const auto* window = ImGui::GetCurrentWindow();
  const float x_pad = ImMax(ImFloor(window->WindowPadding.x * 0.5f), window->WindowBorderSize) +
                      ImGui::GetStyle().FrameBorderSize + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING);
  const ImVec2 pos(window->DC.CursorPos + ImVec2(x_pad, window->WindowBorderSize));
  const ImVec2 size(window->InnerClipRect.GetWidth() - x_pad * 3.0f, LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT));
  const ImRect bb(pos, pos + size);

  const bool pressed = ImGui::InvisibleButton(title, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();

  const ImU32 line_col = ImGui::GetColorU32(ImGuiCol_Button);
  const float line_height = LayoutScale(1.0f);

#if 0
  if (s_menu_button_index == 0)
    dl->AddLine(bb.GetTL(), bb.GetTR(), line_col, line_height);

  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y), ImVec2(bb.Max.x, bb.Max.y), line_col, line_height);
#endif

  if (hovered)
  {
    const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive :
                                                             hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, 0.0f);
  }

  const ImRect title_bb(PadRect(
    ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y), pos + ImVec2(size.x, LayoutScale(50.0f))),
    LayoutScale(4.0f)));
  const ImRect summary_bb(
    PadRect(ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y + LayoutScale(32.0f)),
                   pos + ImVec2(size.x, size.y)),
            LayoutScale(4.0f)));

  ImGui::PushFont(g_large_font);
  ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, title, nullptr, nullptr, ImVec2(0.0f, 0.0f), &title_bb);
  ImGui::PopFont();

  if (summary)
  {
    ImGui::PushFont(g_medium_font);
    ImGui::RenderTextClipped(summary_bb.Min, summary_bb.Max, summary, nullptr, nullptr, ImVec2(0.0f, 0.0f),
                             &summary_bb);
    ImGui::PopFont();
  }

  const float toggle_width = LayoutScale(50.0f);
  const float toggle_height = LayoutScale(25.0f);
  const float toggle_x = LayoutScale(8.0f);
  const float toggle_y = (LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT) - toggle_height) * 0.5f;
  const float toggle_radius = toggle_height * 0.5f;
  const ImVec2 toggle_pos(pos.x + size.x - toggle_width - toggle_x, pos.y + toggle_y);

  if (ImGui::IsItemClicked())
    *v = !*v;

  float t = *v ? 1.0f : 0.0f;
  ImGuiContext& g = *GImGui;
  float ANIM_SPEED = 0.08f;
  if (g.LastActiveId == g.CurrentWindow->GetID(title)) // && g.LastActiveIdTimer < ANIM_SPEED)
  {
    float t_anim = ImSaturate(g.LastActiveIdTimer / ANIM_SPEED);
    t = *v ? (t_anim) : (1.0f - t_anim);
  }

  ImU32 col_bg;
  if (ImGui::IsItemHovered())
    col_bg = ImGui::GetColorU32(ImLerp(HEX_TO_IMVEC4(0x9e9e9e, 0xff), UISecondaryLightColor(), t));
  else
    col_bg = ImGui::GetColorU32(ImLerp(HEX_TO_IMVEC4(0x757575, 0xff), UISecondaryLightColor(), t));

  dl->AddRectFilled(toggle_pos, ImVec2(toggle_pos.x + toggle_width, toggle_pos.y + toggle_height), col_bg,
                    toggle_height * 0.5f);
  dl->AddCircleFilled(
    ImVec2(toggle_pos.x + toggle_radius + t * (toggle_width - toggle_radius * 2.0f), toggle_pos.y + toggle_radius),
    toggle_radius - 1.5f, IM_COL32(255, 255, 255, 255), 32);

  s_menu_button_index++;
  return pressed;
}

bool EnumChoiceButtonImpl(const char* title, const char* summary, s32* value_pointer,
                          const char* (*to_display_name_function)(s32 value, void* opaque), void* opaque, u32 count)
{
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const auto* window = ImGui::GetCurrentWindow();
  const float item_height = LAYOUT_MENU_BUTTON_HEIGHT;
  const float x_pad = ImMax(ImFloor(window->WindowPadding.x * 0.5f), window->WindowBorderSize) +
                      ImGui::GetStyle().FrameBorderSize + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING);
  const ImVec2 pos(window->DC.CursorPos + ImVec2(x_pad, window->WindowBorderSize));
  const ImVec2 size(window->InnerClipRect.GetWidth() - x_pad * 3.0f, LayoutScale(item_height));
  const ImRect bb(pos, pos + size);

  const bool pressed = ImGui::InvisibleButton(title, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();

  const ImU32 line_col = ImGui::GetColorU32(ImGuiCol_Button);
  const float line_height = LayoutScale(1.0f);
#if 0
  if (s_menu_button_index == 0)
    dl->AddLine(bb.GetTL(), bb.GetTR(), line_col, line_height);

  dl->AddLine(ImVec2(bb.Min.x, bb.Max.y), ImVec2(bb.Max.x, bb.Max.y), line_col, line_height);
#endif

  if (hovered)
  {
    const ImU32 col = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, 0.0f);
  }

  const ImRect title_bb(PadRect(
    ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y), pos + ImVec2(size.x, LayoutScale(50.0f))),
    LayoutScale(4.0f)));
  const ImRect summary_bb(
    PadRect(ImRect(ImVec2(pos.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), pos.y + LayoutScale(32.0f)),
                   pos + ImVec2(size.x, size.y)),
            LayoutScale(4.0f)));

  ImGui::PushFont(g_large_font);
  ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, title, nullptr, nullptr, ImVec2(0.0f, 0.0f), &title_bb);
  ImGui::RenderTextClipped(title_bb.Min, summary_bb.Max, to_display_name_function(*value_pointer, opaque), nullptr,
                           nullptr, ImVec2(1.0f, 0.5f), &bb);
  ImGui::PopFont();

  if (summary)
  {
    ImGui::PushFont(g_medium_font);
    ImGui::RenderTextClipped(summary_bb.Min, summary_bb.Max, summary, nullptr, nullptr, ImVec2(0.0f, 0.0f),
                             &summary_bb);
    ImGui::PopFont();
  }

  TinyString popup_name;
  popup_name.Format("%s_popup", title);
  if (pressed)
    ImGui::OpenPopup(popup_name);

  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::SetNextWindowSize(
    ImVec2(LayoutScale(500.0f), LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY * static_cast<float>(count)) +
                                  (style.WindowPadding.y * 2.0f) + style.WindowBorderSize));

  bool changed = false;
  if (ImGui::BeginPopupModal(popup_name, nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove))
  {
    BeginMenuButtons(static_cast<u32>(count), false);
    for (s32 i = 0; i < static_cast<s32>(count); i++)
    {
      if (MenuCategory(to_display_name_function(i, opaque), i == *value_pointer))
      {
        *value_pointer = i;
        changed = true;
        ImGui::CloseCurrentPopup();
      }
    }

    EndMenuButtons();

    ImGui::EndPopup();
  }

  return changed;
}

} // namespace ImGuiFullscreen