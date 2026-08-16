#include "GpuParticleSystem.h"
#include "ShaderLibrary.h"
#include "ParticleRenderer.h"
#include "ShapeField.h"
#include "GpuBindings.h"
#include "GlCompat.h"
#include "GlUniforms.h"
#include "rlgl.h"

#include <vector>

using gl_uniforms::SetFloat;
using gl_uniforms::SetUint;
using gl_uniforms::SetVec4;

GpuParticleSystem::GpuParticleSystem(ShaderLibrary& shaders, ParticleRenderer& renderer, int capacity)
    : shaders_(shaders), renderer_(renderer), capacity_(capacity) {
    // Zero-initialized (life == 0, i.e. dead) via a null initial-data
    // pointer -- rlLoadShaderBuffer zero-clears in that case.
    particleBuffer_.Allocate(static_cast<unsigned int>(capacity_) * sizeof(GpuParticle));

    // Free list: [freeCount, freeIndices[0..capacity)], all slots free at
    // creation and listed in index order.
    std::vector<int> initial(static_cast<size_t>(capacity_) + 1);
    initial[0] = capacity_;
    for (int i = 0; i < capacity_; i++) initial[static_cast<size_t>(i) + 1] = i;
    freeListBuffer_.Allocate(static_cast<unsigned int>(initial.size() * sizeof(int)), initial.data());

    simProgram_ = shaders_.LoadCompute("assets/shaders/particles/particle_sim.comp");
    emitProgram_ = shaders_.LoadCompute("assets/shaders/particles/particle_emit.comp");
}

GpuParticleSystem::~GpuParticleSystem() {
    shaders_.UnloadCompute(simProgram_);
    shaders_.UnloadCompute(emitProgram_);
}

int GpuParticleSystem::AddForce(const GpuForceDesc& force) {
    forces_.push_back(force);
    forcesDirty_ = true;
    return static_cast<int>(forces_.size()) - 1;
}

void GpuParticleSystem::SetForce(int index, const GpuForceDesc& force) {
    if (index < 0 || index >= static_cast<int>(forces_.size())) return;
    forces_[static_cast<size_t>(index)] = force;
    forcesDirty_ = true;
}

void GpuParticleSystem::ClearForces() {
    forces_.clear();
    forcesDirty_ = true;
}

void GpuParticleSystem::Emit(const GpuEmitParams& params, int count) {
    if (count <= 0 || emitProgram_ == 0) return;

    GpuVec4 position{ params.position.x, params.position.y, params.position.z, static_cast<float>(params.mode) };
    // .w carries shellThickness for ShapeSurface mode; unused (0) otherwise.
    GpuVec4 positionJitter{ params.positionJitter.x, params.positionJitter.y, params.positionJitter.z,
                             params.shellThickness };
    GpuVec4 velocityBase{ params.velocity.x, params.velocity.y, params.velocity.z, 0 };
    GpuVec4 velocityJitter{ params.velocityJitter.x, params.velocityJitter.y, params.velocityJitter.z, 0 };

    if (params.mode == GpuEmitMode::Sphere) {
        velocityJitter = { params.speedMin, params.speedMax, 0, 0 };
    } else if (params.mode == GpuEmitMode::Orbit) {
        velocityBase = { 0, 0, 0, params.attractorStrength };
        velocityJitter = { params.orbitSpeedMultiplier, params.orbitVelocityJitter, 0, 0 };
    }

    GpuVec4 colorA{ params.colorA.r / 255.0f, params.colorA.g / 255.0f, params.colorA.b / 255.0f, 1.0f };
    GpuVec4 colorB{ params.colorB.r / 255.0f, params.colorB.g / 255.0f, params.colorB.b / 255.0f, 1.0f };
    GpuVec4 sizeLifeJitter{ params.size, params.sizeJitter, params.life, params.lifeJitter };

    rlEnableShader(emitProgram_);

    SetVec4(emitProgram_, "uPosition", position);
    SetVec4(emitProgram_, "uPositionJitter", positionJitter);
    SetVec4(emitProgram_, "uVelocityBase", velocityBase);
    SetVec4(emitProgram_, "uVelocityJitter", velocityJitter);
    SetVec4(emitProgram_, "uColorA", colorA);
    SetVec4(emitProgram_, "uColorB", colorB);
    SetVec4(emitProgram_, "uSizeLifeJitter", sizeLifeJitter);
    SetUint(emitProgram_, "uEmitCount", static_cast<unsigned int>(count));
    SetUint(emitProgram_, "uSeed", emitSeedCounter_++);

    particleBuffer_.BindBase(gpu_bindings::kParticleBuffer);
    freeListBuffer_.BindBase(gpu_bindings::kFreeListBuffer);
    // Only needed for ShapeSurface mode, but binding is cheap (a few
    // uniform sets) and harmless for other modes -- keeps this call site
    // simple rather than branching on mode here too.
    if (shapeField_ != nullptr) shapeField_->BindForSampling(emitProgram_);

    unsigned int groups = (static_cast<unsigned int>(count) + 63u) / 64u;
    rlComputeShaderDispatch(groups, 1, 1);
    rlDisableShader();

    gl_compat::ShaderStorageBarrier();
}

void GpuParticleSystem::ApplyRadialImpulse(Vector3 center, float strength, float maxRadius) {
    pendingImpulseCenter_ = center;
    pendingImpulseStrength_ = strength;
    pendingImpulseRadius_ = maxRadius;
    pendingImpulseActive_ = true;
}

void GpuParticleSystem::Update(float dt, float time) {
    if (simProgram_ == 0) return;

    if (forcesDirty_ && !forces_.empty()) {
        unsigned int neededBytes = static_cast<unsigned int>(forces_.size() * sizeof(GpuForceDesc));
        if (forceBuffer_.SizeBytes() < neededBytes) {
            forceBuffer_.Allocate(neededBytes);
        }
        forceBuffer_.Update(forces_.data(), neededBytes);
        forcesDirty_ = false;
    }

    rlEnableShader(simProgram_);

    SetUint(simProgram_, "uCapacity", static_cast<unsigned int>(capacity_));
    SetUint(simProgram_, "uForceCount", static_cast<unsigned int>(forces_.size()));
    SetFloat(simProgram_, "uDt", dt);
    SetFloat(simProgram_, "uTime", time);

    GpuVec4 impulseCenter{ pendingImpulseCenter_.x, pendingImpulseCenter_.y, pendingImpulseCenter_.z, pendingImpulseStrength_ };
    GpuVec4 impulseRadius{ pendingImpulseRadius_, pendingImpulseActive_ ? 1.0f : 0.0f, 0, 0 };
    SetVec4(simProgram_, "uImpulseCenter", impulseCenter);
    SetVec4(simProgram_, "uImpulseRadius", impulseRadius);

    particleBuffer_.BindBase(gpu_bindings::kParticleBuffer);
    freeListBuffer_.BindBase(gpu_bindings::kFreeListBuffer);
    if (!forces_.empty()) forceBuffer_.BindBase(gpu_bindings::kForceBuffer);
    if (shapeField_ != nullptr) shapeField_->BindForSampling(simProgram_);

    unsigned int groups = (static_cast<unsigned int>(capacity_) + 63u) / 64u;
    rlComputeShaderDispatch(groups, 1, 1);
    rlDisableShader();

    gl_compat::ShaderStorageBarrier();

    pendingImpulseActive_ = false; // one-shot: consumed this frame
}

int GpuParticleSystem::AliveCountApprox() const {
    int freeCount = 0;
    freeListBuffer_.Read(&freeCount, sizeof(int), 0);
    return capacity_ - freeCount;
}

void GpuParticleSystem::DebugDumpFirst(int count) const {
    int freeCount = 0;
    freeListBuffer_.Read(&freeCount, sizeof(int), 0);
    TraceLog(LOG_INFO, "GpuParticleSystem: capacity=%d freeCount=%d aliveApprox=%d",
             capacity_, freeCount, capacity_ - freeCount);

    std::vector<GpuParticle> sample(static_cast<size_t>(count));
    particleBuffer_.Read(sample.data(), static_cast<unsigned int>(count) * sizeof(GpuParticle), 0);
    for (int i = 0; i < count; i++) {
        const GpuParticle& p = sample[static_cast<size_t>(i)];
        TraceLog(LOG_INFO, "  [%d] pos=(%.2f,%.2f,%.2f) life=%.3f vel=(%.2f,%.2f,%.2f) size=%.3f color=(%.2f,%.2f,%.2f,%.2f) maxLife=%.3f",
                 i, p.positionLife.x, p.positionLife.y, p.positionLife.z, p.positionLife.w,
                 p.velocitySize.x, p.velocitySize.y, p.velocitySize.z, p.velocitySize.w,
                 p.color.x, p.color.y, p.color.z, p.color.w, p.params.x);
    }
}

void GpuParticleSystem::Draw(const Matrix& viewProj, Vector3 cameraRight, Vector3 cameraUp,
                              int fadeMode, float sizeScale,
                              const LightSample* lights, int lightCount) const {
    particleBuffer_.BindBase(gpu_bindings::kParticleBuffer);
    renderer_.Draw(capacity_, viewProj, cameraRight, cameraUp, fadeMode, sizeScale, lights, lightCount);
}
