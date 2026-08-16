#pragma once
#include "raylib.h"
#include "RenderTarget.h"

class ShaderLibrary;

// Cheap LDR bloom: extract bright pixels from the rendered scene, blur
// them (separable two-pass Gaussian at half resolution), and additively
// composite the result back over the original scene. Not physically based
// HDR bloom -- raylib's default render targets are 8-bit -- but a
// well-established technique that reads convincingly as neon glow at a
// fraction of the cost of a true HDR pipeline, and the main thing that
// makes the Neon Fog visualizer's highlights actually look like light
// rather than flat additive sprites.
class PostProcess {
public:
    PostProcess(ShaderLibrary& shaders, int screenWidth, int screenHeight);

    // Everything drawn between BeginScene()/EndScene() (typically the
    // whole BeginMode3D block) is captured to an offscreen target instead
    // of going straight to the backbuffer, so it can be bloom-processed
    // before final display.
    void BeginScene();
    void EndScene();

    // Draws the composited result (scene + bloom) to whichever render
    // target is currently active -- the backbuffer, when called between
    // App's BeginDrawing/EndDrawing and after EndMode3D.
    void Composite(float threshold, float intensity) const;

    void Resize(int screenWidth, int screenHeight);

private:
    ShaderLibrary& shaders_;

    RenderTarget sceneTarget_;
    RenderTarget brightTarget_;
    RenderTarget blurTargetA_;
    RenderTarget blurTargetB_;

    Shader brightPassShader_{};
    Shader blurShader_{};
    Shader compositeShader_{};

    int locBrightThreshold_ = -1;
    int locBlurTexelStep_ = -1;
    int locCompositeBloomTex_ = -1;
    int locCompositeIntensity_ = -1;
};
