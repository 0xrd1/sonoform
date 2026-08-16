#pragma once
#include "raylib.h"

// RAII wrapper around a raylib RenderTexture2D -- an offscreen color
// target the post-processing bloom chain renders into and samples from.
class RenderTarget {
public:
    RenderTarget() = default;
    RenderTarget(int width, int height);
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&& other) noexcept;
    RenderTarget& operator=(RenderTarget&& other) noexcept;

    void Allocate(int width, int height);
    void Release();

    void BeginDraw() const;
    void EndDraw() const;

    Texture2D Texture() const { return target_.texture; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsValid() const { return target_.id != 0; }

private:
    RenderTexture2D target_{};
    int width_ = 0;
    int height_ = 0;
};
