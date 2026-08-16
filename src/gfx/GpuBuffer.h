#pragma once

// RAII wrapper around a raylib/rlgl Shader Storage Buffer Object (SSBO).
// All particle, force, and (later) shape-field data lives in one of these.
// Requires an OpenGL 4.3 context (see core/GlCompat.h) -- rlgl's SSBO
// functions silently no-op otherwise.
class GpuBuffer {
public:
    GpuBuffer() = default;
    GpuBuffer(unsigned int sizeBytes, const void* initialData = nullptr);
    ~GpuBuffer();

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;

    // (Re)allocates the buffer, discarding any previous contents.
    // `initialData` may be null, in which case the buffer is zero-cleared
    // (see rlLoadShaderBuffer in rlgl.h) -- convenient for a particle pool
    // that should start entirely dead (life == 0).
    void Allocate(unsigned int sizeBytes, const void* initialData = nullptr);
    void Release();

    // All sizes/offsets on this class -- here and in Allocate/Update above
    // -- are in bytes, matching rlgl's SSBO functions (thin wrappers over
    // glBufferSubData/glGetBufferSubData, both byte-addressed).
    void Update(const void* data, unsigned int sizeBytes, unsigned int offset = 0);
    void Read(void* dest, unsigned int sizeBytes, unsigned int offset = 0) const;

    // Binds this buffer to an SSBO binding point (see gfx/GpuBindings.h).
    // Binding is global GL state, not per-shader, so callers must rebind
    // before every dispatch/draw that reads or writes this buffer.
    void BindBase(unsigned int bindingIndex) const;

    unsigned int Id() const { return id_; }
    unsigned int SizeBytes() const { return sizeBytes_; }
    bool IsValid() const { return id_ != 0; }

private:
    unsigned int id_ = 0;
    unsigned int sizeBytes_ = 0;
};
