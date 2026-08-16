#include "PostProcess.h"
#include "ShaderLibrary.h"
#include <algorithm>

namespace {
constexpr int kBloomDownscale = 2; // bloom chain runs at half resolution
}

PostProcess::PostProcess(ShaderLibrary& shaders, int screenWidth, int screenHeight)
    : shaders_(shaders) {
    Resize(screenWidth, screenHeight);

    brightPassShader_ = shaders_.LoadGraphicsFS("assets/shaders/post/brightpass.fs");
    blurShader_ = shaders_.LoadGraphicsFS("assets/shaders/post/blur.fs");
    compositeShader_ = shaders_.LoadGraphicsFS("assets/shaders/post/composite.fs");

    locBrightThreshold_ = GetShaderLocation(brightPassShader_, "uThreshold");
    locBlurTexelStep_ = GetShaderLocation(blurShader_, "uTexelStep");
    locCompositeBloomTex_ = GetShaderLocation(compositeShader_, "uBloomTex");
    locCompositeIntensity_ = GetShaderLocation(compositeShader_, "uBloomIntensity");
}

void PostProcess::Resize(int screenWidth, int screenHeight) {
    sceneTarget_.Allocate(screenWidth, screenHeight);
    int bloomW = std::max(1, screenWidth / kBloomDownscale);
    int bloomH = std::max(1, screenHeight / kBloomDownscale);
    brightTarget_.Allocate(bloomW, bloomH);
    blurTargetA_.Allocate(bloomW, bloomH);
    blurTargetB_.Allocate(bloomW, bloomH);
}

void PostProcess::BeginScene() {
    sceneTarget_.BeginDraw();
    ClearBackground(Color{ 6, 6, 12, 255 });
}

void PostProcess::EndScene() {
    sceneTarget_.EndDraw();
}

void PostProcess::Composite(float threshold, float intensity) const {
    // Bright-pass extraction: sceneTarget_ -> brightTarget_. Both are
    // render textures (GL's bottom-up storage), so no Y-flip is needed
    // between them.
    brightTarget_.BeginDraw();
    ClearBackground(BLACK);
    BeginShaderMode(brightPassShader_);
    if (locBrightThreshold_ != -1) {
        SetShaderValue(brightPassShader_, locBrightThreshold_, &threshold, SHADER_UNIFORM_FLOAT);
    }
    DrawTexturePro(sceneTarget_.Texture(),
        Rectangle{ 0, 0, static_cast<float>(sceneTarget_.Width()), static_cast<float>(sceneTarget_.Height()) },
        Rectangle{ 0, 0, static_cast<float>(brightTarget_.Width()), static_cast<float>(brightTarget_.Height()) },
        Vector2{ 0, 0 }, 0.0f, WHITE);
    EndShaderMode();
    brightTarget_.EndDraw();

    // Horizontal blur: brightTarget_ -> blurTargetA_.
    Vector2 texelH{ 1.0f / static_cast<float>(brightTarget_.Width()), 0.0f };
    blurTargetA_.BeginDraw();
    ClearBackground(BLACK);
    BeginShaderMode(blurShader_);
    if (locBlurTexelStep_ != -1) SetShaderValue(blurShader_, locBlurTexelStep_, &texelH, SHADER_UNIFORM_VEC2);
    DrawTextureRec(brightTarget_.Texture(),
        Rectangle{ 0, 0, static_cast<float>(brightTarget_.Width()), static_cast<float>(brightTarget_.Height()) },
        Vector2{ 0, 0 }, WHITE);
    EndShaderMode();
    blurTargetA_.EndDraw();

    // Vertical blur: blurTargetA_ -> blurTargetB_.
    Vector2 texelV{ 0.0f, 1.0f / static_cast<float>(blurTargetA_.Height()) };
    blurTargetB_.BeginDraw();
    ClearBackground(BLACK);
    BeginShaderMode(blurShader_);
    if (locBlurTexelStep_ != -1) SetShaderValue(blurShader_, locBlurTexelStep_, &texelV, SHADER_UNIFORM_VEC2);
    DrawTextureRec(blurTargetA_.Texture(),
        Rectangle{ 0, 0, static_cast<float>(blurTargetA_.Width()), static_cast<float>(blurTargetA_.Height()) },
        Vector2{ 0, 0 }, WHITE);
    EndShaderMode();
    blurTargetB_.EndDraw();

    // Final composite: sceneTarget_ + blurred bloom -> whatever the
    // current render target is (the backbuffer, from App::Draw). This is
    // the one genuine render-texture -> screen transition, so it needs
    // the Y-flip (negative source height) that undoes GL's bottom-up
    // render-texture storage; composite.fs separately flips its uBloomTex
    // sample to match (see that shader's comment).
    BeginShaderMode(compositeShader_);
    if (locCompositeBloomTex_ != -1) {
        SetShaderValueTexture(compositeShader_, locCompositeBloomTex_, blurTargetB_.Texture());
    }
    if (locCompositeIntensity_ != -1) {
        SetShaderValue(compositeShader_, locCompositeIntensity_, &intensity, SHADER_UNIFORM_FLOAT);
    }
    DrawTexturePro(sceneTarget_.Texture(),
        Rectangle{ 0, 0, static_cast<float>(sceneTarget_.Width()), -static_cast<float>(sceneTarget_.Height()) },
        Rectangle{ 0, 0, static_cast<float>(sceneTarget_.Width()), static_cast<float>(sceneTarget_.Height()) },
        Vector2{ 0, 0 }, 0.0f, WHITE);
    EndShaderMode();
}
