// ---- Sprites and 2D ------------------------------------------------------------------------------

void SpriteSizesAndFramesFollowTheImage() {
    magnum::SetProjectRoot(MAGNUM_PROJECT_DIR);
    magnum::SpriteComponent s;
    s.texture = "Content/Textures/checker.png"; // 256 x 256
    int w = 0, h = 0;
    CHECK(magnum::ImageSize(s.texture, w, h) && w == 256 && h == 256);
    CHECK(Near(magnum::SpriteWorldSize(s).x, 2.56f) && Near(magnum::SpriteWorldSize(s).y, 2.56f));
    s.pixelsPerUnit = 256.0f;
    CHECK(Near(magnum::SpriteWorldSize(s).x, 1.0f));
    s.columns = 4;
    s.rows = 2;
    CHECK(Near(magnum::SpriteWorldSize(s).x, 0.25f) && Near(magnum::SpriteWorldSize(s).y, 0.5f)); // one atlas cell is 64 x 128 px
    s.width = 2.0f; // one side given: the cell's proportions are kept
    CHECK(Near(magnum::SpriteWorldSize(s).x, 2.0f) && Near(magnum::SpriteWorldSize(s).y, 4.0f));
    s.height = 3.0f; // both given: used as they are
    CHECK(Near(magnum::SpriteWorldSize(s).x, 2.0f) && Near(magnum::SpriteWorldSize(s).y, 3.0f));
    s.texture = "Content/Textures/nothing.png";
    s.width = s.height = 0.0f;
    CHECK(Near(magnum::SpriteWorldSize(s).x, 1.0f) && Near(magnum::SpriteWorldSize(s).y, 1.0f)); // missing image: 1 x 1

    // Frames are counted from the top left; images are stored bottom row first, so the first row is the top of v.
    magnum::SpriteComponent a;
    a.columns = 4;
    a.rows = 2;
    a.frame = 0;
    glm::vec4 r = magnum::SpriteUvRect(a);
    CHECK(Near(r.x, 0.0f) && Near(r.y, 0.5f) && Near(r.z, 0.25f) && Near(r.w, 0.5f));
    a.frame = 6; // second row, third column
    r = magnum::SpriteUvRect(a);
    CHECK(Near(r.x, 0.5f) && Near(r.y, 0.0f));
    a.flipX = true; // flipping makes the size negative and starts from the far edge
    r = magnum::SpriteUvRect(a);
    CHECK(Near(r.x, 0.75f) && Near(r.z, -0.25f));
    a.frame = 99; // out of range stays on the last cell
    CHECK(magnum::SpriteUvRect(a).y >= 0.0f);
}

void SpriteAnimationsLoopAndStop() {
    magnum::SpriteComponent s;
    s.columns = 4;
    s.rows = 1;
    s.fps = 8.0f;
    s.animate = true;
    for (int i = 0; i < 5; ++i) magnum::AdvanceSpriteAnimation(s, 0.125f);
    CHECK(s.frame == 1 && s.animate); // 0.625 s at 8 fps = 5 steps, looping over 4 cells

    s.firstFrame = 1; // a clip of cells 1..2
    s.frameCount = 2;
    s.animationTime = 0.0f;
    magnum::AdvanceSpriteAnimation(s, 0.125f);
    CHECK(s.frame == 2);
    magnum::AdvanceSpriteAnimation(s, 0.125f);
    CHECK(s.frame == 1); // wrapped back to the first cell of the clip

    s.loop = false; // a one-shot rests on its last cell and stops
    s.animationTime = 0.0f;
    magnum::AdvanceSpriteAnimation(s, 1.0f);
    CHECK(s.frame == 2 && !s.animate);
    magnum::AdvanceSpriteAnimation(s, 1.0f);
    CHECK(s.frame == 2);
}

void PhysicsWorksInTwoDimensions() {
    magnum::Scene scene;
    entt::registry& reg = scene.Registry();
    auto sprite = [&](const char* name, glm::vec3 position, float width, float height, bool body) {
        const uint64_t id = scene.CreateActor(name);
        const entt::entity e = scene.Find(id);
        reg.get<magnum::TransformComponent>(e).position = position;
        magnum::SpriteComponent s;
        s.width = width;
        s.height = height;
        reg.emplace<magnum::SpriteComponent>(e, s);
        reg.emplace<magnum::ColliderComponent>(e);
        if (body) {
            magnum::RigidbodyComponent rb;
            rb.lockZ = true;
            reg.emplace<magnum::RigidbodyComponent>(e, rb);
        }
        return id;
    };
    sprite("Floor", {0, -1, 0}, 20.0f, 1.0f, false);                  // top edge at y = -0.5
    const uint64_t hero = sprite("Hero", {0.3f, 3, 0.2f}, 1.0f, 2.0f, true); // starts slightly off the plane

    // The collider takes its size from the sprite, with enough depth that a 2D body never meets it edge-on in Z.
    magnum::Physics::Shape shape;
    CHECK(magnum::Physics::ShapeOf(scene, hero, shape) && Near(shape.half.x, 0.5f) && Near(shape.half.y, 1.0f) && shape.half.z >= 0.5f);

    magnum::Physics physics;
    physics.Begin(scene);
    physics.SetVelocity(hero, {2.0f, 0.0f, 3.0f}); // a push along Z is ignored for a 2D body
    Run(physics, scene, 4.0f);
    const magnum::TransformComponent* t = scene.TryGet<magnum::TransformComponent>(hero);
    CHECK(Near(t->position.y, 0.5f, 0.03f)); // standing on the floor: -0.5 + half height 1.0 = 0.5
    CHECK(Near(t->position.z, 0.2f, 1e-4f));
    CHECK(physics.Grounded(hero));
}

void ScriptsDriveSpritesAndTheMouseInTwoD() {
    magnum::SetProjectRoot(MAGNUM_PROJECT_DIR);
    ScriptProject project;
    project.Write("hero.lua",
                  "function on_start(self)\n"
                  "  self.sprite:play(2, 5, 12)\n"
                  "  world.spawn_sprite('Content/Textures/checker.png', 3, 4, 1.5, 1.5, 'Coin')\n"
                  "end\n"
                  "function on_update(self, dt)\n"
                  "  self.sprite.flip_x = input.key('a')\n"
                  "  self.sprite.opacity = 0.5\n"
                  "  self.sprite.order = 7\n"
                  "  local p = input.mouse_world(0)\n"
                  "  if p then self.position = p end\n"
                  "end\n");
    magnum::Scene scene;
    const uint64_t hero = AddScripted(scene, "Hero", "Content/Scripts/hero.lua");
    magnum::SpriteComponent sc;
    sc.columns = 8;
    scene.Registry().emplace<magnum::SpriteComponent>(scene.Find(hero), sc);
    const uint64_t camera = scene.CreateActor("Camera");
    magnum::CameraComponent cam;
    cam.orthographic = true;
    cam.orthoSize = 4.0f;
    scene.Registry().emplace<magnum::CameraComponent>(scene.Find(camera), cam);
    scene.TryGet<magnum::TransformComponent>(camera)->position = {10, 2, 10};

    bool a = false;
    magnum::GameInput input;
    input.keyDown = [&](int key) { return key == 65 && a; };
    input.aspect = 2.0f;
    input.mouseInView = true;
    input.mousePosition = {0.75f, 0.25f}; // right of the center, above the center

    magnum::Gameplay gameplay;
    gameplay.Begin(scene, project.dir);
    const magnum::SpriteComponent* s = scene.TryGet<magnum::SpriteComponent>(hero);
    CHECK(s->animate && s->firstFrame == 2 && s->frameCount == 4 && s->frame == 2 && Near(s->fps, 12.0f));

    gameplay.Update(scene, 0.1f, input);
    CHECK(!s->flipX && s->order == 7 && Near(s->opacity, 0.5f));
    CHECK(s->frame == 3); // 0.1 s at 12 fps is one step into the clip
    a = true;
    gameplay.Update(scene, 0.01f, input);
    CHECK(s->flipX);

    // Orthographic: the view is 2 * 4 high and 2 * 4 * 2 wide around (10, 2); the mouse is a quarter of the view right
    // of the center and a quarter above it.
    const magnum::TransformComponent* t = scene.TryGet<magnum::TransformComponent>(hero);
    CHECK(Near(t->position.x, 14.0f, 1e-3f) && Near(t->position.y, 4.0f, 1e-3f) && Near(t->position.z, 0.0f, 1e-3f));

    // A sprite spawned by a script is a real actor.
    const entt::entity coin = scene.Find(scene.Order().back());
    const magnum::SpriteComponent* cs = scene.Registry().try_get<magnum::SpriteComponent>(coin);
    CHECK(cs && Near(cs->width, 1.5f) && scene.Registry().get<magnum::TransformComponent>(coin).position.x == 3.0f);

    input.mouseInView = false; // outside the view the mouse functions give nothing
    scene.TryGet<magnum::TransformComponent>(hero)->position = {1, 1, 1};
    gameplay.Update(scene, 0.01f, input);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(hero)->position.x, 1.0f));
}

void PlatformerLevelPlays() {
    const std::filesystem::path project = std::filesystem::path(MAGNUM_ENGINE_DIR) / "Projects" / "Platformer2D";
    magnum::SetEngineRoot(MAGNUM_ENGINE_DIR);
    magnum::SetProjectRoot(project);
    magnum::Scene scene;
    magnum::SceneEditorState editor;
    std::string error;
    CHECK(magnum::LoadScene(scene, editor, project / "Content/Scenes/Level1.scene", error));
    auto byName = [&](const char* name) -> uint64_t {
        for (const uint64_t id : scene.Order())
            if (scene.TryGet<magnum::NameComponent>(id)->name == name) return id;
        return 0;
    };
    const uint64_t player = byName("Player");
    CHECK(player != 0 && scene.TryGet<magnum::SpriteComponent>(player));

    bool right = false;
    magnum::GameInput input;
    input.keyDown = [&](int key) { return key == 68 && right; };
    auto frame = [&](magnum::Gameplay& g, int count) {
        for (int i = 0; i < count; ++i) g.Update(scene, 1.0f / 60.0f, input);
    };
    auto label = [&](magnum::Gameplay& g, const char* name) -> const magnum::UiNode* { return g.Ui().Get(g.Ui().Find(name)); };

    magnum::Gameplay gameplay;
    gameplay.Begin(scene, project);
    CHECK(gameplay.ScriptCount() == 1 + 4 + 9); // the player, four slimes and nine clouds
    CHECK(label(gameplay, "PlayButton") && label(gameplay, "CoinsLabel") && label(gameplay, "Heart3")); // the three canvases of the scene

    // The main menu holds the game still until Play is pressed: the player does not run, nothing falls.
    right = true;
    frame(gameplay, 30);
    const magnum::TransformComponent* t = scene.TryGet<magnum::TransformComponent>(player);
    CHECK(Near(t->position.x, 0.0f) && Near(t->position.y, 1.0f, 1e-4f));
    right = false;
    std::string luaError;
    CHECK(gameplay.RunLua("ui.find('PlayButton'):click()", luaError));
    frame(gameplay, 60); // the menu fades, the game runs
    CHECK(!label(gameplay, "MainMenu")->visible);
    CHECK(gameplay.GetPhysics().Grounded(player) && Near(t->position.y, 1.0f, 0.05f)); // standing on the grass
    CHECK(label(gameplay, "CoinsLabel")->text == "0 / 19");
    CHECK(scene.TryGet<magnum::SpriteComponent>(player)->firstFrame == 0 && scene.TryGet<magnum::SpriteComponent>(player)->animate);

    right = true;
    frame(gameplay, 30);
    CHECK(t->position.x > 2.0f);
    CHECK(scene.TryGet<magnum::SpriteComponent>(player)->firstFrame == 2 && !scene.TryGet<magnum::SpriteComponent>(player)->flipX); // running right
    right = false;

    // Landing on a slime squashes it and bounces the player.
    const uint64_t slime = byName("Slime 2");
    CHECK(slime != 0);
    const glm::vec3 above = scene.TryGet<magnum::TransformComponent>(slime)->position + glm::vec3(0, 2.0f, 0);
    scene.TryGet<magnum::TransformComponent>(player)->position = above;
    gameplay.GetPhysics().SetVelocity(player, {0, -6.0f, 0});
    frame(gameplay, 30);
    CHECK(!scene.Exists(slime));

    // Falling out of the world puts the player back at the start and costs a life: the last heart dims.
    CHECK(Near(label(gameplay, "Heart3")->opacity, 1.0f));
    scene.TryGet<magnum::TransformComponent>(player)->position = {10, -30, 0};
    frame(gameplay, 4);
    CHECK(t->position.x < 1.0f && t->position.y > 0.0f);
    CHECK(label(gameplay, "Heart3")->opacity < 0.95f && Near(label(gameplay, "Heart1")->opacity, 1.0f));

    // P opens the pause menu and freezes the game, P again (or Resume) continues.
    bool p = false;
    input.keyDown = [&](int key) { return (key == 68 && right) || (key == 80 && p); };
    p = true;
    frame(gameplay, 1);
    p = false;
    frame(gameplay, 5);
    CHECK(label(gameplay, "PauseMenu")->visible);
    const glm::vec3 frozen = t->position;
    right = true;
    frame(gameplay, 30);
    CHECK(Near(t->position.x, frozen.x) && Near(t->position.y, frozen.y));
    p = true;
    frame(gameplay, 1);
    p = false;
    frame(gameplay, 40);
    CHECK(t->position.x > frozen.x + 1.0f);
}

void DecalAndSpriteShadersCompile() {
    for (const char* rel : {"Engine/Shaders/decal.lua", "Engine/Shaders/sprite.lua"}) {
        magnum::CompiledShaderSource source;
        std::vector<magnum::ShaderDiagnostic> diagnostics;
        const bool ok = magnum::CompileLuaShader(std::filesystem::path(MAGNUM_ENGINE_DIR) / rel, rel, std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua",
                                               "Engine/Lua/shader_dsl.lua", source, diagnostics);
        if (!ok) std::printf("  %s: %s\n", rel, diagnostics.empty() ? "?" : diagnostics[0].ToString().c_str());
        CHECK(ok);
        if (std::string(rel).find("decal") != std::string::npos && ok) {
            CHECK(source.fragmentGlsl.find("gl_FragCoord") != std::string::npos);
            CHECK(source.fragmentGlsl.find("u_scene_depth") != std::string::npos && source.fragmentGlsl.find("u_inv_model") != std::string::npos);
            CHECK(source.fragmentGlsl.find("uniform vec4 u_frag_coord") == std::string::npos); // frag_coord is not a uniform
        }
    }
}

void ScriptsCanSpawnDecalsOnSurfaces() {
    ScriptProject project;
    project.Write("shooter.lua",
                  "function on_start(self)\n"
                  "  local hit = world.raycast(vec3(2, 5, 1), vec3(0, -1, 0), 20, self)\n"
                  "  if hit then world.spawn_decal('Content/Textures/checker.png', hit.point, hit.normal, 2, nil, 'Hole') end\n"
                  "  local wall = world.raycast(vec3(0, 1, 0), vec3(1, 0, 0), 20, self)\n"
                  "  if wall then world.spawn_decal('Content/Textures/checker.png', wall.point, wall.normal, 1) end\n"
                  "end\n");
    magnum::Scene scene;
    AddBox(scene, "Ground", {0, -0.5f, 0}, {20, 1, 20}, false);
    AddBox(scene, "Wall", {6, 2, 0}, {1, 4, 8}, false);
    AddScripted(scene, "Shooter", "Content/Scripts/shooter.lua", {0, 1, 5});
    magnum::Gameplay gameplay;
    gameplay.Begin(scene, project.dir);

    uint64_t floor = 0, side = 0;
    for (const uint64_t id : scene.Order()) {
        if (!scene.TryGet<magnum::DecalComponent>(id)) continue;
        (scene.TryGet<magnum::NameComponent>(id)->name == "Hole" ? floor : side) = id;
    }
    CHECK(floor != 0 && side != 0);
    const magnum::TransformComponent* f = scene.TryGet<magnum::TransformComponent>(floor);
    CHECK(Near(f->position.x, 2.0f) && Near(f->position.y, 0.0f) && Near(f->position.z, 1.0f));
    CHECK(Near(f->rotation.x, 90.0f, 0.01f) && Near(f->scale.x, 2.0f) && Near(f->scale.z, 1.0f)); // projects downwards, depth = half the size
    CHECK(scene.TryGet<magnum::DecalComponent>(floor)->texture == "Content/Textures/checker.png");

    // On the wall's -X face the projection axis (local Z) points along +X.
    const magnum::TransformComponent* w = scene.TryGet<magnum::TransformComponent>(side);
    CHECK(Near(w->position.x, 5.5f));
    const glm::vec3 axis = glm::vec3(w->RotationMatrix() * glm::vec4(0, 0, 1, 0));
    CHECK(Near(axis.x, 1.0f) && Near(axis.y, 0.0f, 1e-2f) && Near(axis.z, 0.0f, 1e-2f));
}
