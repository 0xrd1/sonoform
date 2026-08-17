#include "TransportBar.h"
#include "EngineUi.h" // ui::WantsMouse() -- so this bar never fights an ImGui panel drawn over it
#include "raymath.h"
#include <cstdio>
#include <string>

namespace ui {

namespace {

constexpr float kBarHeight = 64.0f;
constexpr float kMargin = 18.0f;
constexpr float kProgressHeight = 6.0f;
constexpr float kProgressY = 12.0f;      // offset from the bar's top
constexpr float kKnobRadius = 7.0f;
constexpr float kControlY = 32.0f;       // offset from the bar's top
constexpr float kButtonSize = 30.0f;
constexpr float kButtonGap = 8.0f;
constexpr float kVolumeWidth = 90.0f;
constexpr float kVolumeHeight = 4.0f;

// Scrub/volume drag state persists across frames (App rebuilds
// TransportState fresh every frame, same as PanelState -- see EngineUi.h's
// comment on why that's fine: the *state struct* is disposable, this
// latch is not) -- module-local statics, same pattern EngineUi.cpp's own
// `static char nameBuf[]` already uses for the one Presets text field.
bool s_draggingProgress = false;
bool s_draggingVolume = false;
float s_seekThrottle = 0.0f;

std::string FormatTime(float seconds) {
    if (seconds < 0.0f) seconds = 0.0f;
    int total = static_cast<int>(seconds + 0.5f);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

// A flat button background plus a triangle/bar glyph -- raylib's default
// font (the only one this project loads -- see EngineUi.cpp's Setup()
// comment on font ownership) has no media-control glyphs, so icons are
// drawn as shapes rather than text, the same way VoidFloor/HUD draw
// everything else in this project: plain rlgl primitives, no image assets.
void DrawIconButton(Rectangle rect, bool hovered, bool enabled, int kind, Color accent) {
    Color bg = enabled ? (hovered ? Fade(WHITE, 0.18f) : Fade(WHITE, 0.10f)) : Fade(WHITE, 0.05f);
    DrawRectangleRec(rect, bg);
    Color glyph = enabled ? accent : Fade(accent, 0.35f);

    float cx = rect.x + rect.width * 0.5f;
    float cy = rect.y + rect.height * 0.5f;
    float h = rect.height * 0.28f; // glyph half-height

    if (kind == 0) { // Prev: a bar + a left-pointing triangle
        DrawRectangle(static_cast<int>(cx - h - 3), static_cast<int>(cy - h), 3, static_cast<int>(h * 2), glyph);
        DrawTriangle({ cx + h, cy - h }, { cx - 2.0f, cy }, { cx + h, cy + h }, glyph);
    } else if (kind == 1) { // Play: a right-pointing triangle
        DrawTriangle({ cx - h * 0.7f, cy - h }, { cx - h * 0.7f, cy + h }, { cx + h, cy }, glyph);
    } else if (kind == 2) { // Pause: two vertical bars
        DrawRectangle(static_cast<int>(cx - h), static_cast<int>(cy - h), 3, static_cast<int>(h * 2), glyph);
        DrawRectangle(static_cast<int>(cx + h - 3), static_cast<int>(cy - h), 3, static_cast<int>(h * 2), glyph);
    } else { // Next: a right-pointing triangle + a bar
        DrawTriangle({ cx - h, cy - h }, { cx - h, cy + h }, { cx + 2.0f, cy }, glyph);
        DrawRectangle(static_cast<int>(cx + h), static_cast<int>(cy - h), 3, static_cast<int>(h * 2), glyph);
    }
}

} // namespace

void DrawTransportBar(TransportState& state) {
    if (!state.visible) return;

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    float barY = static_cast<float>(screenH) - kBarHeight;

    DrawRectangle(0, static_cast<int>(barY), screenW, static_cast<int>(kBarHeight), Fade(BLACK, 0.55f));
    DrawLine(0, static_cast<int>(barY), screenW, static_cast<int>(barY), Fade(WHITE, 0.08f));

    // Same left-click-only mouse layer every other piece of raylib-drawn
    // UI in this project already respects -- see App::UpdateCameraOrbit's
    // uiOwnsMouse gate. Reading disabled entirely while an ImGui panel has
    // the pointer, so hovering/dragging never leaks through to both layers
    // at once.
    bool uiOwnsMouse = ui::WantsMouse();
    Vector2 mouse = GetMousePosition();

    // ---- Progress track -------------------------------------------------
    Rectangle progressRect{ kMargin, barY + kProgressY, static_cast<float>(screenW) - kMargin * 2.0f, kProgressHeight };
    bool hoverProgress = !uiOwnsMouse && state.enabled && CheckCollisionPointRec(mouse, progressRect);

    if (!uiOwnsMouse && hoverProgress && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        s_draggingProgress = true;
        s_seekThrottle = 0.0f;
    }

    float progressRatio = (state.duration > 0.0001f) ? Clamp(state.timePlayed / state.duration, 0.0f, 1.0f) : 0.0f;
    if (s_draggingProgress) {
        float dragRatio = Clamp((mouse.x - progressRect.x) / progressRect.width, 0.0f, 1.0f);
        progressRatio = dragRatio;
        s_seekThrottle += GetFrameTime();
        if (s_seekThrottle >= 0.08f) {
            s_seekThrottle = 0.0f;
            if (state.onSeek) state.onSeek(dragRatio * state.duration);
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (state.onSeek) state.onSeek(dragRatio * state.duration);
            s_draggingProgress = false;
        }
    }

    DrawRectangleRec(progressRect, Fade(WHITE, 0.15f));
    Rectangle filled{ progressRect.x, progressRect.y, progressRect.width * progressRatio, progressRect.height };
    DrawRectangleRec(filled, state.enabled ? SKYBLUE : Fade(SKYBLUE, 0.35f));
    float knobX = progressRect.x + progressRect.width * progressRatio;
    float knobY = progressRect.y + progressRect.height * 0.5f;
    if (state.enabled && (hoverProgress || s_draggingProgress)) {
        DrawCircle(static_cast<int>(knobX), static_cast<int>(knobY), kKnobRadius, SKYBLUE);
        DrawCircleLines(static_cast<int>(knobX), static_cast<int>(knobY), kKnobRadius, WHITE);
    } else if (state.enabled) {
        DrawCircle(static_cast<int>(knobX), static_cast<int>(knobY), kKnobRadius * 0.6f, SKYBLUE);
    }

    // ---- Transport buttons + track label + time --------------------------
    float bx = kMargin;
    float by = barY + kControlY;
    Rectangle prevRect{ bx, by, kButtonSize, kButtonSize };
    bx += kButtonSize + kButtonGap;
    Rectangle playRect{ bx, by, kButtonSize, kButtonSize };
    bx += kButtonSize + kButtonGap;
    Rectangle nextRect{ bx, by, kButtonSize, kButtonSize };

    bool hoverPrev = !uiOwnsMouse && state.enabled && CheckCollisionPointRec(mouse, prevRect);
    bool hoverPlay = !uiOwnsMouse && state.enabled && CheckCollisionPointRec(mouse, playRect);
    bool hoverNext = !uiOwnsMouse && state.enabled && CheckCollisionPointRec(mouse, nextRect);

    if (hoverPrev && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && state.onPrev) state.onPrev();
    if (hoverPlay && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && state.onPausedChanged) state.onPausedChanged(!state.paused);
    if (hoverNext && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && state.onNext) state.onNext();

    DrawIconButton(prevRect, hoverPrev, state.enabled, 0, RAYWHITE);
    DrawIconButton(playRect, hoverPlay, state.enabled, state.paused ? 1 : 2, RAYWHITE);
    DrawIconButton(nextRect, hoverNext, state.enabled, 3, RAYWHITE);

    // Track label + n/total, vertically centered on the button row.
    float labelX = nextRect.x + nextRect.width + kButtonGap * 2.0f;
    float labelY = by + kButtonSize * 0.5f - 8.0f;
    const char* label = state.enabled ? state.trackName : "No track loaded";
    DrawText(label, static_cast<int>(labelX), static_cast<int>(labelY), 16, RAYWHITE);
    if (state.enabled && state.trackCount > 1) {
        const char* idx = TextFormat("(%d/%d)", state.trackIndex + 1, state.trackCount);
        int labelW = MeasureText(label, 16);
        DrawText(idx, static_cast<int>(labelX) + labelW + 8, static_cast<int>(labelY), 16, Fade(RAYWHITE, 0.6f));
    }

    // Time readout, right-aligned just left of the volume slider.
    std::string timeText = FormatTime(state.timePlayed) + " / " + FormatTime(state.duration);
    int timeW = MeasureText(timeText.c_str(), 16);
    float volumeX = static_cast<float>(screenW) - kMargin - kVolumeWidth;
    float timeX = volumeX - 16.0f - static_cast<float>(timeW);
    DrawText(timeText.c_str(), static_cast<int>(timeX), static_cast<int>(labelY), 16, Fade(RAYWHITE, 0.75f));

    // ---- Volume slider -----------------------------------------------------
    Rectangle volumeRect{ volumeX, by + kButtonSize * 0.5f - kVolumeHeight * 0.5f, kVolumeWidth, kVolumeHeight };
    // A little vertical slack around the thin track so it's not a
    // pixel-precise target -- matches the progress track's own generous
    // hit area (progressRect is already taller than its drawn line).
    Rectangle volumeHitRect{ volumeRect.x, volumeRect.y - 8.0f, volumeRect.width, volumeRect.height + 16.0f };
    bool hoverVolume = !uiOwnsMouse && CheckCollisionPointRec(mouse, volumeHitRect);
    if (!uiOwnsMouse && hoverVolume && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) s_draggingVolume = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s_draggingVolume = false;

    float volumeRatio = Clamp(state.volume, 0.0f, 1.0f);
    if (s_draggingVolume) {
        volumeRatio = Clamp((mouse.x - volumeRect.x) / volumeRect.width, 0.0f, 1.0f);
        if (state.onVolumeChanged) state.onVolumeChanged(volumeRatio);
    }
    DrawRectangleRec(volumeRect, Fade(WHITE, 0.15f));
    DrawRectangle(static_cast<int>(volumeRect.x), static_cast<int>(volumeRect.y),
                  static_cast<int>(volumeRect.width * volumeRatio), static_cast<int>(volumeRect.height), Fade(RAYWHITE, 0.8f));
    float volKnobX = volumeRect.x + volumeRect.width * volumeRatio;
    float volKnobY = volumeRect.y + volumeRect.height * 0.5f;
    DrawCircle(static_cast<int>(volKnobX), static_cast<int>(volKnobY), hoverVolume || s_draggingVolume ? 5.0f : 3.5f, RAYWHITE);

    // Dragging is a latched gesture (mirrors App::UpdateCameraOrbit's
    // orbiting_): if the track/volume becomes disabled (e.g. audio turned
    // off) mid-drag, drop the latch so it doesn't reattach to a future
    // enabled state with stale intent.
    if (!state.enabled) { s_draggingProgress = false; s_draggingVolume = false; }
}

} // namespace ui
