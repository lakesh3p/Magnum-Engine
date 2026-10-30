#pragma once

#include "shader/ShaderTypes.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace magnum {

// One step of a frame. Passes run in the order the script lists them.
//   clear    : fills color and depth                      (color)
//   sky      : fullscreen shader behind everything        (material)
//   meshes   : every mesh actor with its own material
//   sprites  : every Sprite, sorted by order and depth
//   decals   : every Decal, projected onto the solid surfaces drawn so far (put it after meshes)
//   markers  : boxes for actors without a mesh, and the selection box (editor only)
//   grid     : the editor ground grid (editor only)
//   overlay  : fullscreen shader blended over the image   (material)
//   post     : fullscreen shader that reads the image so far as scene_color (material)
//   ui       : the UI canvases (put it last so post effects do not touch it; drawn at the end when missing)
struct PipelinePass {
    enum class Type { Clear, Sky, Meshes, Sprites, Decals, Markers, Grid, Overlay, Post, Ui };

    Type type = Type::Meshes;
    std::string material; // Sky, Overlay and Post: a material whose shader was written with S:fullscreen
    std::array<float, 3> color{0.07f, 0.07f, 0.08f};
    bool enabled = true;
    float scale = 1.0f; // sky and overlay: render at this fraction of the resolution (0.25 - 1) and upscale; clouds are smooth, so 0.5 costs a quarter
    int line = 0; // line in the script, for diagnostics
};

struct PipelineData {
    std::string name = "Pipeline";
    std::vector<PipelinePass> passes;
};

const char* PassTypeName(PipelinePass::Type type);

// The pass list used when no pipeline script is loaded.
PipelineData DefaultPipeline();

// Runs a pipeline script:
//   return pipeline { name = "...", passes = { { "clear", color = {r, g, b} }, { "sky", material = "..." }, ... } }
// Sandboxed: no file or OS access.
bool ParsePipeline(const std::string& source, const std::string& displayName, PipelineData& out,
                   std::vector<ShaderDiagnostic>& diagnostics);

// A pipeline script on disk. Reloads when it changes; a broken edit keeps the last working pipeline.
class RenderPipeline {
public:
    void Init(const std::filesystem::path& projectRoot, const std::string& relPath);
    void Poll(); // cheap; call every frame
    bool Reload();

    const PipelineData& Data() const { return m_data; }
    const std::string& Path() const { return m_path; }
    const std::vector<ShaderDiagnostic>& Diagnostics() const { return m_diagnostics; }
    uint64_t Revision() const { return m_revision; }

private:
    std::filesystem::file_time_type Stamp() const;

    std::filesystem::path m_root;
    std::string m_path;
    std::filesystem::file_time_type m_stamp{};
    std::chrono::steady_clock::time_point m_lastPoll{};
    PipelineData m_data = DefaultPipeline();
    std::vector<ShaderDiagnostic> m_diagnostics;
    uint64_t m_revision = 0;
};

} // namespace magnum
