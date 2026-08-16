// Deterministic "is this particle recruited (locked to the current shape)"
// hash, shared between particle_emit.comp (decides a particle's life at
// spawn: long-lived "core" vs. short-lived "shed") and shape_conform.glsl
// (the ongoing per-frame force check that actually pulls recruited
// particles toward the shape). Both must derive the identical decision
// from the same particle seed forever, or a particle assigned a long life
// at spawn could fail to be recruited by the force (or vice versa) --
// see GpuEmitParams::recruitFraction.
const float kRecruitHashConstant = 0.6180339887; // golden ratio conjugate

float RecruitRoll(float seed) {
    return fract(seed * kRecruitHashConstant);
}
