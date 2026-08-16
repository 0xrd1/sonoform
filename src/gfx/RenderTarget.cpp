#include "RenderTarget.h"

RenderTarget::RenderTarget(int width, int height) {
    Allocate(width, height);
}

RenderTarget::~RenderTarget() {
    Release();
}

RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : target_(other.target_), width_(other.width_), height_(other.height_) {
    other.target_ = RenderTexture2D{};
    other.width_ = 0;
    other.height_ = 0;
}

RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept {
    if (this != &other) {
        Release();
        target_ = other.target_;
        width_ = other.width_;
        height_ = other.height_;
        other.target_ = RenderTexture2D{};
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void RenderTarget::Allocate(int width, int height) {
    Release();
    target_ = LoadRenderTexture(width, height);
    width_ = width;
    height_ = height;
}

void RenderTarget::Release() {
    if (target_.id != 0) {
        UnloadRenderTexture(target_);
        target_ = RenderTexture2D{};
        width_ = 0;
        height_ = 0;
    }
}

void RenderTarget::BeginDraw() const {
    BeginTextureMode(target_);
}

void RenderTarget::EndDraw() const {
    EndTextureMode();
}
