#pragma once
#include <vector>
#include <memory>
#include "Visualizer.h"

class ShaderLibrary;
class ParticleRenderer;
namespace ui { class IParamVisitor; }

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
    const char* CurrentDebugInfoText() const;
    void SecondaryActionOnCurrent();
    void TertiaryActionOnCurrent();
    void AdjustPrimaryOnCurrent(float delta);
    int CurrentIndex() const { return index_; }
    int Count() const { return static_cast<int>(items_.size()); }

    // Forwards to the current visualizer's VisitSettings -- see
    // src/ui/EngineUi.h's DrawDebugPanel.
    void VisitCurrentSettings(ui::IParamVisitor& v);

    // Re-runs the current visualizer's Init(shaders, renderer): the "Rebuild
    // Systems" action for any ParamFlags::NeedsRebuild field a settings
    // panel edited (e.g. fog capacity, shape grid resolution). Init already
    // reassigns every GPU-owning unique_ptr member via make_unique, whose
    // prior contents are freed by the assignment before the new one is
    // constructed, so this is safe to call repeatedly without leaking.
    void RebuildCurrent(ShaderLibrary& shaders, ParticleRenderer& renderer);

private:
    std::vector<std::unique_ptr<Visualizer>> items_;
    int index_ = 0;
};
