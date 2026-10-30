#pragma once

#include "material/MaterialLibrary.h"
#include "model/ModelImport.h"
#include "render/Framebuffer.h"
#include "render/Mesh.h"
#include "render/RenderPipeline.h"
#include "render/ShaderLibrary.h"
#include "render/UiRenderer.h"
#include "render/Texture.h"
#include "scene/Scene.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace magnum {

// The asset libraries a draw call needs.
struct RenderResources {
    ShaderLibrary* shaders = nullptr;
    MaterialLibrary* materials = nullptr;
    TextureLibrary* textures = nullptr;
    const RenderPipeline* pipeline = nullptr; // null = the built-in pass list
};

struct RenderView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    int width = 1;
    int height = 1;
    float time = 0.0f;
    const UiDrawList* ui = nullptr; // what the UI passes draw (built by whoever owns the UI)
};

struct EditorOverlay {
    uint64_t selected = 0;
    bool drawGrid = true;
    bool drawMarkers = true; // boxes for actors without a mesh (lights, empties)
    bool wireframe = false;  // mesh pass as lines
    bool postProcess = true; // run the pipeline's post passes
};

// Draws a scene into the currently bound framebuffer.
class SceneRenderer {
public:
    // Drawn when an actor's material is missing or broken. It lives in the engine folder, so every project has it.
    static constexpr const char* kFallbackMaterial = "Engine/Materials/Default.lua";

    void Init();

    // GPU time of every pass of a recent frame (measured with timer queries; results arrive two frames late).
    struct PassTiming {
        std::string name;
        float ms = 0.0f;
    };
    const std::vector<PassTiming>& Timings() const { return m_timings; }
    void Shutdown();
    void Render(Scene& scene, RenderResources& resources, const RenderView& view, const EditorOverlay& overlay);

    // A lit sphere with the given material, for thumbnails and the material panel. Draws into the bound framebuffer.
    // `yawDegrees` / `pitchDegrees` turn the sphere (not the light), so a texture can be inspected from every side.
    void RenderMaterialPreview(RenderResources& resources, const std::string& materialPath, int width, int height,
                               float yawDegrees = 0.0f, float pitchDegrees = 0.0f);
    // The same sphere drawn with a shader and its default parameter values (no material file needed).
    void RenderShaderPreview(RenderResources& resources, const std::string& shaderPath, int width, int height);

private:
    RenderView BeginPreview(int width, int height); // clears, sets state, lights and camera for a preview sphere
    void EndPreview();
    const Mesh& MeshFor(Primitive primitive) const;
    // The GPU copy of a model file (its parts, one Mesh each), uploaded on first use and again when the file changes.
    // Null when the file cannot be read: the actor is then drawn as its primitive.
    struct ModelGpu {
        std::shared_ptr<const ImportedModel> source;
        std::vector<std::unique_ptr<Mesh>> parts;
    };
    const ModelGpu* ModelFor(const std::string& relPath) const;
    void DrawMeshes(Scene& scene, RenderResources& resources, const RenderView& view) const;
    void DrawMarkers(Scene& scene, RenderResources& resources, const RenderView& view, const EditorOverlay& overlay) const;
    void DrawGrid(RenderResources& resources, const RenderView& view) const;
    void DrawUi(RenderResources& resources, const RenderView& view);
    void DrawSprites(Scene& scene, RenderResources& resources, const RenderView& view) const;
    void DrawDecals(Scene& scene, RenderResources& resources, const RenderView& view);
    // Draws a material whose shader was written with S:fullscreen over the whole target. `readsScene` copies the
    // image drawn so far into the shader's scene_color texture first.
    // `scale` < 1 renders into a smaller target first and upscales it into the image.
    void DrawFullscreen(RenderResources& resources, const std::string& materialPath, const RenderView& view,
                        bool blend, bool readsScene, float scale);
    void BeginTimings();
    void BeginPassTimer(const std::string& name);
    void EndPassTimer();
    void SetFrameUniforms(const ShaderProgram& program, const RenderView& view, const glm::mat4& model) const;
    // Binds the material's parameters and textures to the shader's program.
    void BindMaterial(RenderResources& resources, MaterialAsset& material, const ShaderAsset& shader) const;
    // The same, for another program of the same shader (its instanced variant).
    void BindMaterial(RenderResources& resources, MaterialAsset& material, const ShaderAsset& shader,
                      const ShaderProgram& program) const;
    // The material and its shader, or the default material when either is missing or broken. False: nothing to draw.
    bool ResolveMaterial(RenderResources& resources, const std::string& materialPath, MaterialAsset*& material,
                         ShaderAsset*& shader) const;
    // Looks the material up (falling back to the default one if it or its shader is broken) and draws.
    void DrawWithMaterial(RenderResources& resources, const std::string& materialPath, const Mesh& mesh,
                          const glm::mat4& model, const RenderView& view) const;
    // Draws one mesh for every model matrix in `models` with a single instanced draw (one draw per matrix when
    // the shader has no instanced variant).
    void DrawBatch(RenderResources& resources, const std::string& materialPath, const Mesh& mesh,
                   const std::vector<glm::mat4>& models, const RenderView& view) const;

    Mesh m_primitives[kPrimitiveCount]; // indexed by Primitive
    Mesh m_lineBox;
    mutable std::unordered_map<std::string, ModelGpu> m_models; // by model path
    mutable uint32_t m_instanceBuffer = 0;   // MeshInstance array for instanced draws
    mutable size_t m_instanceCapacity = 0;   // bytes allocated in m_instanceBuffer
    uint32_t m_samplers[2] = {0, 0}; // sprite filtering: smooth, pixelated
    uint32_t m_emptyVao = 0;     // fullscreen triangles are generated from gl_VertexID
    uint32_t m_sceneTexture = 0; // copy of the image so far, for post passes
    uint32_t m_depthTexture = 0; // copy of the depth buffer, for decals
    int m_depthWidth = 0, m_depthHeight = 0;
    int m_sceneWidth = 0, m_sceneHeight = 0;
    UiRenderer m_ui;
    Framebuffer m_scaled;                       // target of passes rendered below full resolution
    std::unique_ptr<ShaderProgram> m_blit;      // draws m_scaled into the image

    // Timer queries, three frames in flight.
    static constexpr int kTimerSlots = 3;
    std::vector<uint32_t> m_queries[kTimerSlots];
    std::vector<std::string> m_queryNames[kTimerSlots];
    int m_timerSlot = 0;
    bool m_timerOpen = false;
    std::vector<PassTiming> m_timings;
    glm::vec3 m_lightDir{0.4f, 0.8f, 0.45f}; // direction towards the light
    glm::vec3 m_lightColor{1.0f};
};

} // namespace magnum
