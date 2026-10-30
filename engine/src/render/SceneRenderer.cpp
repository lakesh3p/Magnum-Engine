#include "render/SceneRenderer.h"

#include "core/Log.h"
#include "scene/SpriteSize.h"

#include <glad/gl.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>

namespace magnum {

namespace {

constexpr const char* kGridShader = "Engine/Shaders/grid.lua";
constexpr const char* kSelectionShader = "Engine/Shaders/selection.lua";
constexpr const char* kSpriteShader = "Engine/Shaders/sprite.lua";
constexpr const char* kDecalShader = "Engine/Shaders/decal.lua";

} // namespace

void SceneRenderer::Init() {
    for (int i = 0; i < kPrimitiveCount; ++i) m_primitives[i].BuildPrimitive(static_cast<Primitive>(i));
    m_lineBox.BuildLineBox();
    glCreateVertexArrays(1, &m_emptyVao);
    m_ui.Init();
    glCreateSamplers(2, m_samplers);
    for (int i = 0; i < 2; ++i) {
        const GLint filter = i == 0 ? GL_LINEAR : GL_NEAREST;
        glSamplerParameteri(m_samplers[i], GL_TEXTURE_MIN_FILTER, filter);
        glSamplerParameteri(m_samplers[i], GL_TEXTURE_MAG_FILTER, filter);
        glSamplerParameteri(m_samplers[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_samplers[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // Copies a smaller render target into the image (bilinear), used by passes with scale < 1.
    CompiledShaderSource blit;
    blit.name = "Blit";
    blit.luaFile = "(built in) blit";
    blit.vertexGlsl =
        "#version 450 core\n"
        "out vec2 v_uv;\n"
        "void main() {\n"
        "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
        "    v_uv = p;\n"
        "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    blit.fragmentGlsl =
        "#version 450 core\n"
        "uniform sampler2D u_tex;\n"
        "in vec2 v_uv;\n"
        "out vec4 o_color;\n"
        "void main() { o_color = texture(u_tex, v_uv); }\n";
    std::vector<ShaderDiagnostic> diagnostics;
    m_blit = ShaderProgram::Create(blit, diagnostics);
    for (const ShaderDiagnostic& d : diagnostics) Log::Error("{}", d.ToString());
}

void SceneRenderer::Shutdown() {
    for (Mesh& mesh : m_primitives) mesh.Destroy();
    m_lineBox.Destroy();
    m_models.clear();
    m_scaled.Release();
    m_blit.reset();
    m_ui.Shutdown();
    for (int i = 0; i < kTimerSlots; ++i) {
        if (!m_queries[i].empty()) glDeleteQueries(static_cast<GLsizei>(m_queries[i].size()), m_queries[i].data());
        m_queries[i].clear();
        m_queryNames[i].clear();
    }
    if (m_samplers[0]) glDeleteSamplers(2, m_samplers);
    m_samplers[0] = m_samplers[1] = 0;
    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
    if (m_instanceBuffer) glDeleteBuffers(1, &m_instanceBuffer);
    m_instanceBuffer = 0;
    m_instanceCapacity = 0;
    if (m_sceneTexture) glDeleteTextures(1, &m_sceneTexture);
    if (m_depthTexture) glDeleteTextures(1, &m_depthTexture);
    m_emptyVao = m_sceneTexture = m_depthTexture = 0;
    m_sceneWidth = m_sceneHeight = m_depthWidth = m_depthHeight = 0;
}

const Mesh& SceneRenderer::MeshFor(Primitive primitive) const {
    const int index = static_cast<int>(primitive);
    return m_primitives[index >= 0 && index < kPrimitiveCount ? index : 0];
}

void SceneRenderer::SetFrameUniforms(const ShaderProgram& program, const RenderView& view,
                                     const glm::mat4& model) const {
    const glm::mat4 viewProj = view.projection * view.view;
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
    program.SetMat4("u_model", glm::value_ptr(model));
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    program.SetMat4("u_view_proj", glm::value_ptr(viewProj));
    program.SetMat4("u_view", glm::value_ptr(view.view));
    program.SetMat4("u_projection", glm::value_ptr(view.projection));
    program.SetMat4("u_inv_view_proj", glm::value_ptr(invViewProj));
    program.SetMat3("u_normal_matrix", glm::value_ptr(normalMatrix));
    program.SetVec3("u_camera_pos", view.cameraPosition.x, view.cameraPosition.y, view.cameraPosition.z);
    program.SetVec3("u_light_dir", m_lightDir.x, m_lightDir.y, m_lightDir.z);
    program.SetVec3("u_light_color", m_lightColor.x, m_lightColor.y, m_lightColor.z);
    program.SetFloat("u_time", view.time);
    program.SetFloat("u_aspect", static_cast<float>(view.width) / static_cast<float>(view.height));
    program.SetVec2("u_resolution", static_cast<float>(view.width), static_cast<float>(view.height));
}

void SceneRenderer::BindMaterial(RenderResources& resources, MaterialAsset& material, const ShaderAsset& shader) const {
    BindMaterial(resources, material, shader, *shader.Program());
}

void SceneRenderer::BindMaterial(RenderResources& resources, MaterialAsset& material, const ShaderAsset& shader,
                                 const ShaderProgram& program) const {
    int unit = 0;
    for (const MaterialAsset::Resolved& r : material.Resolve(shader)) {
        if (r.param->isTexture) {
            uint32_t id = 0;
            if (!r.texture.empty()) id = resources.textures->Get(r.texture, r.param->srgb).id;
            glBindTextureUnit(static_cast<GLuint>(unit), id ? id : resources.textures->White());
            program.SetInt(r.param->uniform.c_str(), unit);
            ++unit;
        } else {
            program.SetParamValue(*r.param, r.value);
        }
    }
}

bool SceneRenderer::ResolveMaterial(RenderResources& resources, const std::string& materialPath, MaterialAsset*& material,
                                    ShaderAsset*& shader) const {
    material = resources.materials->Get(materialPath);
    shader = material ? resources.shaders->Get(material->Data().shader) : nullptr;
    if (!shader || !shader->Program()) { // missing or broken: show the default material instead of nothing
        material = resources.materials->Get(kFallbackMaterial);
        shader = material ? resources.shaders->Get(material->Data().shader) : nullptr;
    }
    return shader && shader->Program();
}

void SceneRenderer::DrawWithMaterial(RenderResources& resources, const std::string& materialPath, const Mesh& mesh,
                                     const glm::mat4& model, const RenderView& view) const {
    MaterialAsset* material = nullptr;
    ShaderAsset* shader = nullptr;
    if (!ResolveMaterial(resources, materialPath, material, shader)) return;

    shader->Program()->Use();
    SetFrameUniforms(*shader->Program(), view, model);
    BindMaterial(resources, *material, *shader);
    mesh.Draw();
}

void SceneRenderer::DrawBatch(RenderResources& resources, const std::string& materialPath, const Mesh& mesh,
                              const std::vector<glm::mat4>& models, const RenderView& view) const {
    MaterialAsset* material = nullptr;
    ShaderAsset* shader = nullptr;
    if (!ResolveMaterial(resources, materialPath, material, shader)) return;

    const ShaderProgram* instanced = shader->InstancedProgram();
    if (!instanced) {
        for (const glm::mat4& model : models) DrawWithMaterial(resources, materialPath, mesh, model, view);
        return;
    }

    // One MeshInstance per actor: the model matrix and its normal matrix, as the instanced shader variant reads them.
    std::vector<MeshInstance> instances(models.size()); // value-initialized: the padding is zero
    for (size_t i = 0; i < models.size(); ++i) {
        const glm::mat4& m = models[i];
        const glm::mat3 normal = glm::transpose(glm::inverse(glm::mat3(m)));
        std::memcpy(instances[i].model, glm::value_ptr(m), sizeof(instances[i].model));
        for (int c = 0; c < 3; ++c) {
            std::memcpy(instances[i].normal + 4 * c, glm::value_ptr(normal) + 3 * c, 3 * sizeof(float));
        }
    }

    const size_t bytes = instances.size() * sizeof(MeshInstance);
    if (bytes > m_instanceCapacity) {
        if (!m_instanceBuffer) glCreateBuffers(1, &m_instanceBuffer);
        glNamedBufferData(m_instanceBuffer, static_cast<GLsizeiptr>(bytes), instances.data(), GL_DYNAMIC_DRAW);
        m_instanceCapacity = bytes;
    } else {
        glNamedBufferSubData(m_instanceBuffer, 0, static_cast<GLsizeiptr>(bytes), instances.data());
    }

    instanced->Use();
    SetFrameUniforms(*instanced, view, glm::mat4(1.0f)); // the model comes from the instances, not the uniform
    BindMaterial(resources, *material, *shader, *instanced);
    mesh.DrawInstanced(m_instanceBuffer, static_cast<int>(instances.size()));
}

const SceneRenderer::ModelGpu* SceneRenderer::ModelFor(const std::string& relPath) const {
    std::shared_ptr<const ImportedModel> source = LoadModel(relPath);
    if (!source) return nullptr;
    ModelGpu& gpu = m_models[relPath];
    if (gpu.source != source) { // new, or the file changed since it was uploaded
        gpu.parts.clear();
        for (const ImportedMesh& part : source->meshes) {
            auto mesh = std::make_unique<Mesh>();
            mesh->Create(part.vertices, part.indices, GL_TRIANGLES);
            gpu.parts.push_back(std::move(mesh));
        }
        gpu.source = source;
    }
    return &gpu;
}

void SceneRenderer::DrawMeshes(Scene& scene, RenderResources& resources, const RenderView& view) const {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Actors with the same mesh and material are drawn together: one instanced draw for each group.
    std::map<std::pair<const Mesh*, std::string>, std::vector<glm::mat4>> batches;
    for (auto [e, t, mesh] : scene.Registry().view<TransformComponent, MeshComponent>().each()) {
        if (const ModelGpu* model = ModelFor(mesh.model)) {
            for (const std::unique_ptr<Mesh>& part : model->parts) {
                batches[{part.get(), mesh.material}].push_back(t.Matrix());
            }
            continue;
        }
        batches[{&MeshFor(mesh.primitive), mesh.material}].push_back(t.Matrix());
    }
    for (const auto& [key, models] : batches) {
        DrawBatch(resources, key.second, *key.first, models, view);
    }
}

void SceneRenderer::DrawMarkers(Scene& scene, RenderResources& resources, const RenderView& view,
                                const EditorOverlay& overlay) const {
    entt::registry& reg = scene.Registry();
    // Selection box and markers: always visible, drawn over the scene.
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    ShaderAsset* sel = resources.shaders->Get(kSelectionShader);
    if (!sel || !sel->Program()) return;
    const ShaderProgram& program = *sel->Program();
    program.Use();
    auto drawBox = [&](uint64_t id, const TransformComponent& t, const glm::vec3& color) {
        const glm::mat4 model = t.Matrix() * glm::scale(glm::mat4(1.0f), scene.LocalHalfExtents(id) * 2.0f);
        SetFrameUniforms(program, view, model);
        program.SetVec3("p_color", color.x, color.y, color.z);
        m_lineBox.Draw();
    };

    if (overlay.drawMarkers) {
        for (auto [e, id, t] : reg.view<IdComponent, TransformComponent>().each()) {
            if (id.id != overlay.selected && !reg.all_of<MeshComponent>(e) && !reg.all_of<SpriteComponent>(e) &&
                !reg.all_of<DecalComponent>(e)) {
                drawBox(id.id, t, glm::vec3(0.55f, 0.55f, 0.6f));
            }
        }
    }
    if (overlay.selected != 0) {
        if (const TransformComponent* t = scene.TryGet<TransformComponent>(overlay.selected)) {
            drawBox(overlay.selected, *t, glm::vec3(1.0f, 0.6f, 0.1f));
        }
    }
}

void SceneRenderer::DrawGrid(RenderResources& resources, const RenderView& view) const {
    // Blended, no depth writes, visible from both sides.
    ShaderAsset* grid = resources.shaders->Get(kGridShader);
    if (!grid || !grid->Program()) return;
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    const ShaderProgram& program = *grid->Program();
    program.Use();
    SetFrameUniforms(program, view, glm::scale(glm::mat4(1.0f), glm::vec3(600.0f, 1.0f, 600.0f)));
    m_primitives[static_cast<int>(Primitive::Plane)].Draw();
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void SceneRenderer::DrawSprites(Scene& scene, RenderResources& resources, const RenderView& view) const {
    entt::registry& reg = scene.Registry();
    ShaderAsset* shader = resources.shaders->Get(kSpriteShader);
    if (!shader || !shader->Program() || reg.view<SpriteComponent>().empty()) return;

    // Back to front: by order, then by distance along the view direction.
    struct Item {
        entt::entity entity;
        int order;
        float depth;
    };
    std::vector<Item> items;
    for (auto [e, sprite, t] : reg.view<SpriteComponent, TransformComponent>().each()) {
        items.push_back({e, sprite.order, (view.view * glm::vec4(t.position, 1.0f)).z});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.order != b.order ? a.order < b.order : a.depth < b.depth; // view-space z is negative in front of the camera
    });

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE); // solid geometry hides sprites behind it, sprites never hide each other
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const ShaderProgram& program = *shader->Program();
    program.Use();
    const ShaderParam* tintParam = nullptr;
    const ShaderParam* rectParam = nullptr;
    for (const ShaderParam& p : shader->Params()) {
        if (p.name == "tint") tintParam = &p;
        else if (p.name == "uv_rect") rectParam = &p;
    }
    const glm::mat3 cameraRotation = glm::transpose(glm::mat3(view.view)); // the camera axes in the world

    for (const Item& item : items) {
        const SpriteComponent& sprite = reg.get<SpriteComponent>(item.entity);
        const TransformComponent& t = reg.get<TransformComponent>(item.entity);
        const glm::vec2 size = SpriteWorldSize(sprite);

        glm::mat4 model;
        if (sprite.billboard) {
            glm::mat4 facing(cameraRotation);
            facing = glm::rotate(facing, glm::radians(t.rotation.z), glm::vec3(0, 0, 1));
            model = glm::translate(glm::mat4(1.0f), t.position) * facing * glm::scale(glm::mat4(1.0f), t.scale);
        } else {
            model = t.Matrix();
        }
        model = model * glm::scale(glm::mat4(1.0f), glm::vec3(size.x, size.y, 1.0f));
        SetFrameUniforms(program, view, model);

        const Texture texture = sprite.texture.empty() ? Texture{} : resources.textures->Get(sprite.texture, false);
        glBindTextureUnit(0, texture.id ? texture.id : resources.textures->White());
        glBindSampler(0, m_samplers[sprite.pixelated ? 1 : 0]);
        program.SetInt("p_tex", 0);
        if (tintParam) program.SetParamValue(*tintParam, {sprite.tint.r, sprite.tint.g, sprite.tint.b, sprite.opacity});
        if (rectParam) {
            const glm::vec4 rect = SpriteUvRect(sprite);
            program.SetParamValue(*rectParam, {rect.x, rect.y, rect.z, rect.w});
        }
        MeshFor(Primitive::Quad).Draw();
    }
    glBindSampler(0, 0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void SceneRenderer::DrawDecals(Scene& scene, RenderResources& resources, const RenderView& view) {
    entt::registry& reg = scene.Registry();
    if (reg.view<DecalComponent>().empty()) return;

    // Decals read the depth of the solid scene, so it is copied into a texture first (a depth buffer cannot be sampled
    // while it is the target of the same pass).
    if (m_depthWidth != view.width || m_depthHeight != view.height || !m_depthTexture) {
        if (m_depthTexture) glDeleteTextures(1, &m_depthTexture);
        glCreateTextures(GL_TEXTURE_2D, 1, &m_depthTexture);
        glTextureStorage2D(m_depthTexture, 1, GL_DEPTH_COMPONENT24, view.width, view.height);
        glTextureParameteri(m_depthTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_depthTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_depthTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_depthTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_depthWidth = view.width;
        m_depthHeight = view.height;
    }
    glCopyTextureSubImage2D(m_depthTexture, 0, 0, 0, 0, 0, view.width, view.height);

    struct Item {
        entt::entity entity;
        int order;
        float depth;
    };
    std::vector<Item> items;
    for (auto [e, decal, t] : reg.view<DecalComponent, TransformComponent>().each()) {
        items.push_back({e, decal.order, (view.view * glm::vec4(t.position, 1.0f)).z});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.order != b.order ? a.order < b.order : a.depth < b.depth;
    });

    // Back faces of the box, so a decal still shows when the camera is inside it; no depth test (the shader compares
    // the surface it reads with the box itself), blended over what is there.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

    constexpr int kDepthUnit = 7;
    for (const Item& item : items) {
        const DecalComponent& decal = reg.get<DecalComponent>(item.entity);
        const TransformComponent& t = reg.get<TransformComponent>(item.entity);
        const glm::mat4 model = t.Matrix();

        MaterialAsset* material = decal.material.empty() ? nullptr : resources.materials->Get(decal.material);
        ShaderAsset* shader = material ? resources.shaders->Get(material->Data().shader) : resources.shaders->Get(kDecalShader);
        if (!shader || !shader->Program()) continue;
        const ShaderProgram& program = *shader->Program();
        program.Use();
        SetFrameUniforms(program, view, model);
        const glm::mat4 inverse = glm::inverse(model);
        program.SetMat4("u_inv_model", glm::value_ptr(inverse));
        glBindTextureUnit(kDepthUnit, m_depthTexture);
        program.SetInt("u_scene_depth", kDepthUnit);
        if (material) {
            BindMaterial(resources, *material, *shader);
        } else {
            const Texture texture = decal.texture.empty() ? Texture{} : resources.textures->Get(decal.texture, false);
            glBindTextureUnit(0, texture.id ? texture.id : resources.textures->White());
            program.SetInt("p_tex", 0);
            for (const ShaderParam& p : shader->Params()) {
                if (p.name == "tint") program.SetParamValue(p, {decal.tint.r, decal.tint.g, decal.tint.b, decal.opacity});
                else if (p.name == "angle_fade") program.SetParamValue(p, {decal.angleFade});
            }
        }
        MeshFor(Primitive::Cube).Draw();
    }
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

void SceneRenderer::DrawUi(RenderResources& resources, const RenderView& view) {
    if (view.ui && resources.textures) m_ui.Draw(*view.ui, *resources.textures, view.width, view.height);
}

void SceneRenderer::DrawFullscreen(RenderResources& resources, const std::string& materialPath, const RenderView& view,
                                   bool blend, bool readsScene, float scale) {
    MaterialAsset* material = resources.materials->Get(materialPath);
    ShaderAsset* shader = material ? resources.shaders->Get(material->Data().shader) : nullptr;
    if (!shader || !shader->Program()) return; // a broken effect is skipped, the rest of the frame still draws

    if (readsScene) {
        if (m_sceneWidth != view.width || m_sceneHeight != view.height || !m_sceneTexture) {
            if (m_sceneTexture) glDeleteTextures(1, &m_sceneTexture);
            glCreateTextures(GL_TEXTURE_2D, 1, &m_sceneTexture);
            glTextureStorage2D(m_sceneTexture, 1, GL_RGBA8, view.width, view.height);
            glTextureParameteri(m_sceneTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(m_sceneTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(m_sceneTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(m_sceneTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            m_sceneWidth = view.width;
            m_sceneHeight = view.height;
        }
        glCopyTextureSubImage2D(m_sceneTexture, 0, 0, 0, 0, 0, view.width, view.height);
    }

    // Below full resolution the pass draws into a smaller target (cleared to transparent, colors premultiplied by
    // the blend below) that is then stretched over the image.
    const bool scaled = !readsScene && scale < 0.999f && m_blit;
    RenderView passView = view;
    GLint previousFbo = 0;
    GLint previousViewport[4] = {0, 0, view.width, view.height};
    if (scaled) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        passView.width = std::max(1, static_cast<int>(static_cast<float>(view.width) * scale));
        passView.height = std::max(1, static_cast<int>(static_cast<float>(view.height) * scale));
        m_scaled.Resize(passView.width, passView.height);
        m_scaled.Bind();
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (blend) {
        glEnable(GL_BLEND);
        if (scaled) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    } else {
        glDisable(GL_BLEND);
    }

    const ShaderProgram& program = *shader->Program();
    program.Use();
    SetFrameUniforms(program, passView, glm::mat4(1.0f));
    BindMaterial(resources, *material, *shader);
    if (readsScene) {
        constexpr int kSceneUnit = 7;
        glBindTextureUnit(kSceneUnit, m_sceneTexture);
        program.SetInt("u_scene_color", kSceneUnit);
    }
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (scaled) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied
        } else {
            glDisable(GL_BLEND);
        }
        m_blit->Use();
        glBindTextureUnit(0, m_scaled.ColorTexture());
        m_blit->SetInt("u_tex", 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

// ---- GPU timing ------------------------------------------------------------------------------

void SceneRenderer::BeginTimings() {
    m_timerSlot = (m_timerSlot + 1) % kTimerSlots;
    // This slot was filled kTimerSlots frames ago, so its results are ready by now.
    m_timings.clear();
    for (size_t i = 0; i < m_queryNames[m_timerSlot].size(); ++i) {
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(m_queries[m_timerSlot][i], GL_QUERY_RESULT, &nanoseconds);
        m_timings.push_back({m_queryNames[m_timerSlot][i], static_cast<float>(static_cast<double>(nanoseconds) * 1e-6)});
    }
    m_queryNames[m_timerSlot].clear();
}

void SceneRenderer::BeginPassTimer(const std::string& name) {
    std::vector<uint32_t>& pool = m_queries[m_timerSlot];
    const size_t index = m_queryNames[m_timerSlot].size();
    if (index >= pool.size()) {
        GLuint query = 0;
        glGenQueries(1, &query);
        pool.push_back(query);
    }
    m_queryNames[m_timerSlot].push_back(name);
    glBeginQuery(GL_TIME_ELAPSED, pool[index]);
    m_timerOpen = true;
}

void SceneRenderer::EndPassTimer() {
    if (!m_timerOpen) return;
    glEndQuery(GL_TIME_ELAPSED);
    m_timerOpen = false;
}

void SceneRenderer::Render(Scene& scene, RenderResources& resources, const RenderView& view,
                           const EditorOverlay& overlay) {
    entt::registry& reg = scene.Registry();

    // The first light in the scene drives the lit shaders; without one, a fixed fallback is used.
    m_lightDir = glm::normalize(glm::vec3(0.4f, 0.8f, 0.45f));
    m_lightColor = glm::vec3(1.0f);
    const auto lights = reg.view<TransformComponent, LightComponent>();
    if (const auto first = lights.begin(); first != lights.end()) {
        const auto [t, light] = lights.get<TransformComponent, LightComponent>(*first);
        m_lightDir = -t.Forward();
        m_lightColor = light.color * light.intensity;
    }

    static const PipelineData builtIn = DefaultPipeline();
    const PipelineData& pipeline = resources.pipeline ? resources.pipeline->Data() : builtIn;

    BeginTimings();
    bool uiDrawn = false;
    for (const PipelinePass& pass : pipeline.passes) {
        if (!pass.enabled) continue;
        std::string timerName = PassTypeName(pass.type);
        if (!pass.material.empty()) timerName += " " + std::filesystem::path(pass.material).stem().string();
        BeginPassTimer(timerName);
        switch (pass.type) {
        case PipelinePass::Type::Clear:
            glDepthMask(GL_TRUE);
            glClearColor(pass.color[0], pass.color[1], pass.color[2], 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            break;
        case PipelinePass::Type::Sky: DrawFullscreen(resources, pass.material, view, false, false, pass.scale); break;
        case PipelinePass::Type::Meshes:
            if (overlay.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            DrawMeshes(scene, resources, view);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            break;
        case PipelinePass::Type::Sprites: DrawSprites(scene, resources, view); break;
        case PipelinePass::Type::Decals: DrawDecals(scene, resources, view); break;
        case PipelinePass::Type::Markers: DrawMarkers(scene, resources, view, overlay); break;
        case PipelinePass::Type::Grid:
            if (overlay.drawGrid) DrawGrid(resources, view);
            break;
        case PipelinePass::Type::Overlay: DrawFullscreen(resources, pass.material, view, true, false, pass.scale); break;
        case PipelinePass::Type::Post:
            if (overlay.postProcess) DrawFullscreen(resources, pass.material, view, false, true, 1.0f);
            break;
        case PipelinePass::Type::Ui:
            DrawUi(resources, view);
            uiDrawn = true;
            break;
        }
        EndPassTimer();
    }

    if (!uiDrawn && view.ui) { // a pipeline without a ui pass still shows the UI, on top of everything
        BeginPassTimer("ui");
        DrawUi(resources, view);
        EndPassTimer();
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

RenderView SceneRenderer::BeginPreview(int width, int height) {
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // transparent: the panel shows through around the sphere
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    RenderView view;
    view.cameraPosition = glm::vec3(0.0f, 0.0f, 1.95f); // far enough that the whole sphere fits the frame
    view.view = glm::lookAt(view.cameraPosition, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    view.projection = glm::perspective(glm::radians(36.0f), static_cast<float>(width) / static_cast<float>(height), 0.1f, 10.0f);
    view.width = width;
    view.height = height;
    view.time = 0.0f;
    m_lightDir = glm::normalize(glm::vec3(0.55f, 0.65f, 0.7f));
    m_lightColor = glm::vec3(1.0f);
    return view;
}

void SceneRenderer::EndPreview() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

void SceneRenderer::RenderMaterialPreview(RenderResources& resources, const std::string& materialPath, int width,
                                          int height, float yawDegrees, float pitchDegrees) {
    const RenderView view = BeginPreview(width, height);
    const glm::mat4 model = glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3(1, 0, 0)),
                                        glm::radians(yawDegrees), glm::vec3(0, 1, 0));
    DrawWithMaterial(resources, materialPath, MeshFor(Primitive::Sphere), model, view);
    EndPreview();
}

void SceneRenderer::RenderShaderPreview(RenderResources& resources, const std::string& shaderPath, int width, int height) {
    const RenderView view = BeginPreview(width, height);
    ShaderAsset* shader = resources.shaders->Get(shaderPath);
    if (shader && shader->Program()) {
        const ShaderProgram& program = *shader->Program();
        program.Use();
        SetFrameUniforms(program, view, glm::mat4(1.0f));
        int unit = 0;
        for (const ShaderParam& p : shader->Params()) {
            if (p.isTexture) {
                uint32_t id = 0;
                if (!p.defaultTexture.empty()) id = resources.textures->Get(p.defaultTexture, p.srgb).id;
                glBindTextureUnit(static_cast<GLuint>(unit), id ? id : resources.textures->White());
                program.SetInt(p.uniform.c_str(), unit++);
            } else {
                program.SetParamValue(p, p.defaultValue);
            }
        }
        MeshFor(Primitive::Sphere).Draw();
    }
    EndPreview();
}

} // namespace magnum
