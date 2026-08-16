#include "GpuBuffer.h"
#include "rlgl.h"

GpuBuffer::GpuBuffer(unsigned int sizeBytes, const void* initialData) {
    Allocate(sizeBytes, initialData);
}

GpuBuffer::~GpuBuffer() {
    Release();
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : id_(other.id_), sizeBytes_(other.sizeBytes_) {
    other.id_ = 0;
    other.sizeBytes_ = 0;
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        Release();
        id_ = other.id_;
        sizeBytes_ = other.sizeBytes_;
        other.id_ = 0;
        other.sizeBytes_ = 0;
    }
    return *this;
}

void GpuBuffer::Allocate(unsigned int sizeBytes, const void* initialData) {
    Release();
    // RL_DYNAMIC_COPY: contents are respecified repeatedly (dynamic) and
    // both written and read by the GL itself (copy) -- matches how every
    // particle buffer is used (compute writes, vertex shader reads).
    id_ = rlLoadShaderBuffer(sizeBytes, initialData, RL_DYNAMIC_COPY);
    sizeBytes_ = sizeBytes;
}

void GpuBuffer::Release() {
    if (id_ != 0) {
        rlUnloadShaderBuffer(id_);
        id_ = 0;
        sizeBytes_ = 0;
    }
}

void GpuBuffer::Update(const void* data, unsigned int sizeBytes, unsigned int offset) {
    rlUpdateShaderBuffer(id_, data, sizeBytes, offset);
}

void GpuBuffer::Read(void* dest, unsigned int sizeBytes, unsigned int offset) const {
    rlReadShaderBuffer(id_, dest, sizeBytes, offset);
}

void GpuBuffer::BindBase(unsigned int bindingIndex) const {
    rlBindShaderBuffer(id_, bindingIndex);
}
