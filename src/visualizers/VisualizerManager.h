#pragma once
#include <vector>
#include <memory>
#include "Visualizer.h"

class ShaderLibrary;
class ParticleRenderer;

class VisualizerManager {
public:
    // Calls v->Init(shaders, renderer) before storing it.
    void Add(std::unique_ptr<Visualizer> v, ShaderLibrary& shaders, ParticleRenderer& renderer);

    void Next();
    void Prev();
    void SetIndex(int i);

    void Update(const FrameContext& frame);
    void Draw(const RenderContext& ctx) const;

    const char* CurrentName() const;
    int CurrentParticleCount() const;
    const char* CurrentExtraStatusLine() const;
    void SecondaryActionOnCurrent();
    int CurrentIndex() const { return index_; }
    int Count() const { return static_cast<int>(items_.size()); }

private:
    std::vector<std::unique_ptr<Visualizer>> items_;
    int index_ = 0;
};
