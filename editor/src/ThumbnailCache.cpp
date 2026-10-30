#include "ThumbnailCache.h"

namespace magnum::editor {

uint64_t AssetRevision(RenderResources& resources) {
    return resources.materials->Revision() * 1000003ull + resources.shaders->Revision() * 1009ull + resources.textures->Revision();
}

uint32_t ThumbnailCache::Material(SceneRenderer& renderer, RenderResources& resources, const std::string& materialPath,
                                  int size) {
    Entry& entry = m_entries[materialPath + "#" + std::to_string(size)];
    const uint64_t revision = AssetRevision(resources);

    if (entry.revision != revision) {
        if (m_budget <= 0) return entry.fb ? entry.fb->ColorTexture() : 0; // keep the stale image for now
        --m_budget;

        if (!entry.fb) entry.fb = std::make_unique<Framebuffer>();
        entry.fb->Resize(size, size);
        entry.fb->Bind();
        renderer.RenderMaterialPreview(resources, materialPath, size, size);
        Framebuffer::BindDefault();
        entry.revision = revision;
    }
    return entry.fb->ColorTexture();
}

uint32_t ThumbnailCache::Shader(SceneRenderer& renderer, RenderResources& resources, const std::string& shaderPath, int size) {
    Entry& entry = m_entries["shader:" + shaderPath + "#" + std::to_string(size)];
    const uint64_t revision = AssetRevision(resources);

    if (entry.revision != revision) {
        if (m_budget <= 0) return entry.fb ? entry.fb->ColorTexture() : 0;
        --m_budget;

        if (!entry.fb) entry.fb = std::make_unique<Framebuffer>();
        entry.fb->Resize(size, size);
        entry.fb->Bind();
        renderer.RenderShaderPreview(resources, shaderPath, size, size);
        Framebuffer::BindDefault();
        entry.revision = revision;
    }
    return entry.fb->ColorTexture();
}

} // namespace magnum::editor
