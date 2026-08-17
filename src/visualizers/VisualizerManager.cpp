#include "VisualizerManager.h"

void VisualizerManager::Add(std::unique_ptr<Visualizer> v, ShaderLibrary& shaders, ParticleRenderer& renderer) {
    v->Init(shaders, renderer);
    items_.push_back(std::move(v));
}

void VisualizerManager::Next() {
    if (items_.empty()) return;
    index_ = (index_ + 1) % static_cast<int>(items_.size());
}

void VisualizerManager::Prev() {
    if (items_.empty()) return;
    index_ = (index_ - 1 + static_cast<int>(items_.size())) % static_cast<int>(items_.size());
}

void VisualizerManager::SetIndex(int i) {
    if (items_.empty()) return;
    if (i < 0 || i >= static_cast<int>(items_.size())) return;
    index_ = i;
}

void VisualizerManager::Update(const FrameContext& frame) {
    if (items_.empty()) return;
    items_[static_cast<size_t>(index_)]->Update(frame);
}

void VisualizerManager::Draw(const RenderContext& ctx) const {
    if (items_.empty()) return;
    items_[static_cast<size_t>(index_)]->Draw(ctx);
}

const char* VisualizerManager::CurrentName() const {
    if (items_.empty()) return "None";
    return items_[static_cast<size_t>(index_)]->Name();
}

int VisualizerManager::CurrentParticleCount() const {
    if (items_.empty()) return 0;
    return items_[static_cast<size_t>(index_)]->ParticleCount();
}

const char* VisualizerManager::CurrentDebugInfoText() const {
    if (items_.empty()) return nullptr;
    return items_[static_cast<size_t>(index_)]->DebugInfoText();
}

void VisualizerManager::SecondaryActionOnCurrent() {
    if (items_.empty()) return;
    items_[static_cast<size_t>(index_)]->SecondaryAction();
}

void VisualizerManager::TertiaryActionOnCurrent() {
    if (items_.empty()) return;
    items_[static_cast<size_t>(index_)]->TertiaryAction();
}

void VisualizerManager::AdjustPrimaryOnCurrent(float delta) {
    if (items_.empty()) return;
    items_[static_cast<size_t>(index_)]->AdjustPrimary(delta);
}

void VisualizerManager::VisitCurrentSettings(ui::IParamVisitor& v) {
    if (items_.empty()) return;
    items_[static_cast<size_t>(index_)]->VisitSettings(v);
}

void VisualizerManager::RebuildCurrent(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    if (items_.empty()) return;
    items_[static_cast<size_t>(index_)]->Init(shaders, renderer);
}
