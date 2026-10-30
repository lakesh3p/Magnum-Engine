// ---- Physics and scripts -----------------------------------------------------------------------

namespace {

uint64_t AddBox(magnum::Scene& scene, const char* name, glm::vec3 position, glm::vec3 scale, bool body, bool trigger = false) {
    const uint64_t id = scene.CreateActor(name);
    entt::registry& reg = scene.Registry();
    const entt::entity e = scene.Find(id);
    reg.get<magnum::TransformComponent>(e).position = position;
    reg.get<magnum::TransformComponent>(e).scale = scale;
    reg.emplace<magnum::MeshComponent>(e);
    magnum::ColliderComponent collider;
    collider.trigger = trigger;
    reg.emplace<magnum::ColliderComponent>(e, collider);
    if (body) reg.emplace<magnum::RigidbodyComponent>(e);
    return id;
}

void Run(magnum::Physics& physics, magnum::Scene& scene, float seconds) {
    for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) physics.Step(scene, 1.0f / 60.0f);
}

} // namespace

void PhysicsBodyFallsAndRestsOnTheGround() {
    magnum::Scene scene;
    const uint64_t ground = AddBox(scene, "Ground", {0, -0.5f, 0}, {20, 1, 20}, false);
    const uint64_t box = AddBox(scene, "Box", {0, 5, 0}, {1, 1, 1}, true);
    magnum::Physics physics;
    physics.Begin(scene);

    Run(physics, scene, 0.5f);
    CHECK(scene.TryGet<magnum::TransformComponent>(box)->position.y < 5.0f); // it is falling
    CHECK(!physics.Grounded(box));

    Run(physics, scene, 3.0f);
    const magnum::TransformComponent* t = scene.TryGet<magnum::TransformComponent>(box);
    CHECK(Near(t->position.y, 0.5f, 0.03f)); // resting on top of the ground (its top is at y = 0)
    CHECK(physics.Grounded(box));
    CHECK(std::abs(physics.Velocity(box).y) < 0.5f);
    CHECK(scene.TryGet<magnum::TransformComponent>(ground)->position.y == -0.5f); // static things do not move
}

void PhysicsFastBodyDoesNotTunnelThroughAPlane() {
    magnum::Scene scene;
    const uint64_t plane = scene.CreateActor("Floor");
    scene.Registry().emplace<magnum::MeshComponent>(scene.Find(plane), magnum::MeshComponent{magnum::Primitive::Plane, ""});
    scene.Registry().emplace<magnum::ColliderComponent>(scene.Find(plane));
    scene.TryGet<magnum::TransformComponent>(plane)->scale = glm::vec3(30.0f);

    const uint64_t ball = AddBox(scene, "Ball", {0, 30, 0}, {0.5f, 0.5f, 0.5f}, true);
    scene.TryGet<magnum::ColliderComponent>(ball)->shape = magnum::ColliderShape::Sphere;
    magnum::Physics physics;
    physics.Begin(scene);
    physics.SetVelocity(ball, {0, -60, 0}); // far faster than one frame of the plane's thickness
    Run(physics, scene, 2.0f);
    CHECK(scene.TryGet<magnum::TransformComponent>(ball)->position.y > 0.0f);
    CHECK(physics.Grounded(ball));
}

void PhysicsKinematicDoesNotMoveAndBlocks() {
    magnum::Scene scene;
    const uint64_t platform = AddBox(scene, "Platform", {0, 1, 0}, {4, 0.5f, 4}, true);
    scene.TryGet<magnum::RigidbodyComponent>(platform)->kinematic = true;
    const uint64_t box = AddBox(scene, "Box", {0, 4, 0}, {1, 1, 1}, true);
    magnum::Physics physics;
    physics.Begin(scene);
    Run(physics, scene, 3.0f);
    CHECK(scene.TryGet<magnum::TransformComponent>(platform)->position.y == 1.0f);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(box)->position.y, 1.25f + 0.5f, 0.05f));
}

void PhysicsEventsAreReportedOnce() {
    magnum::Scene scene;
    AddBox(scene, "Ground", {0, -0.5f, 0}, {20, 1, 20}, false);
    const uint64_t zone = AddBox(scene, "Zone", {0, 2, 0}, {3, 3, 3}, false, true);
    const uint64_t box = AddBox(scene, "Box", {0, 6, 0}, {1, 1, 1}, true);
    magnum::Physics physics;
    physics.Begin(scene);

    int enters = 0, exits = 0, hits = 0;
    uint64_t enteredBy = 0;
    for (float t = 0.0f; t < 4.0f; t += 1.0f / 60.0f) {
        physics.Step(scene, 1.0f / 60.0f);
        for (const auto& [a, b] : physics.TriggerEnters()) {
            ++enters;
            enteredBy = (a == zone) ? b : a;
        }
        exits += static_cast<int>(physics.TriggerExits().size());
        hits += static_cast<int>(physics.Hits().size());
    }
    CHECK(enters == 1 && enteredBy == box);
    CHECK(exits == 0 || exits == 1); // it comes to rest inside or below the zone
    CHECK(hits == 1);                // the landing, once, not every frame it keeps touching
}

void PhysicsRaycastFindsTheNearestSolid() {
    magnum::Scene scene;
    const uint64_t nearBox = AddBox(scene, "Near", {0, 0, -5}, {2, 2, 2}, false);
    AddBox(scene, "Far", {0, 0, -10}, {2, 2, 2}, false);
    AddBox(scene, "Zone", {0, 0, -2}, {1, 1, 1}, false, true); // triggers are not solid
    magnum::Physics physics;
    magnum::Physics::Hit hit;
    CHECK(physics.Raycast(scene, {0, 0, 0}, {0, 0, -1}, 100.0f, 0, hit));
    CHECK(hit.actor == nearBox && Near(hit.distance, 4.0f, 1e-3f) && Near(hit.normal.z, 1.0f, 1e-3f));
    CHECK(!physics.Raycast(scene, {0, 0, 0}, {0, 0, -1}, 3.0f, 0, hit)); // out of range
    CHECK(!physics.Raycast(scene, {0, 0, 0}, {0, 1, 0}, 100.0f, 0, hit));
}

namespace {

// A throwaway project folder with scripts in Content/Scripts.
struct ScriptProject {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "magnum_test_scripts";
    ScriptProject() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "Content" / "Scripts");
    }
    ~ScriptProject() { std::filesystem::remove_all(dir); }
    void Write(const std::string& name, const std::string& source) { WriteText(dir / "Content" / "Scripts" / name, source); }
};

uint64_t AddScripted(magnum::Scene& scene, const char* name, const std::string& script, glm::vec3 position = {0, 0, 0}) {
    const uint64_t id = scene.CreateActor(name);
    scene.TryGet<magnum::TransformComponent>(id)->position = position;
    scene.Registry().emplace<magnum::ScriptComponent>(scene.Find(id), magnum::ScriptComponent{script});
    return id;
}

} // namespace

void ScriptsRunTheirCallbacksAndKeepState() {
    ScriptProject project;
    project.Write("walker.lua",
                  "function on_start(self) self.state.steps = 0; self.position = vec3(1, 0, 0) end\n"
                  "function on_update(self, dt)\n"
                  "  self.state.steps = self.state.steps + 1\n"
                  "  self.position = self.position + vec3(0, 0, -1) * dt\n"
                  "  self.rotation = vec3(0, self.state.steps, 0)\n"
                  "end\n");
    magnum::Scene scene;
    const uint64_t a = AddScripted(scene, "A", "Content/Scripts/walker.lua");
    const uint64_t b = AddScripted(scene, "B", "Content/Scripts/walker.lua", {0, 5, 0});

    magnum::Gameplay gameplay;
    CHECK(gameplay.Begin(scene, project.dir));
    CHECK(gameplay.ScriptCount() == 2);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(a)->position.x, 1.0f)); // on_start ran before the first frame
    for (int i = 0; i < 10; ++i) gameplay.Update(scene, 0.1f, {});
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(a)->position.z, -1.0f, 1e-3f));
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(a)->rotation.y, 10.0f));        // state is per actor ...
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(b)->rotation.y, 10.0f));        // ... and both counted their own 10 steps
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(b)->position.x, 1.0f));
    gameplay.End();
    CHECK(!gameplay.Running());
}

void ScriptsAreSandboxedAndFailuresAreContained() {
    ScriptProject project;
    project.Write("sandbox.lua", "assert(io == nil and os == nil and require == nil and dofile == nil and load == nil)\n"
                                 "function on_update(self, dt) self.position = vec3(7, 0, 0) end\n");
    project.Write("syntax.lua", "function on_update(self dt) end\n");
    project.Write("crash.lua", "function on_update(self, dt) local x = nil; x.y = 1 end\n");
    project.Write("good.lua", "function on_update(self, dt) self.position = vec3(3, 0, 0) end\n");
    project.Write("loop.lua", "function on_update(self, dt) while true do end end\n");

    magnum::Scene scene;
    const uint64_t sandbox = AddScripted(scene, "Sandbox", "Content/Scripts/sandbox.lua");
    AddScripted(scene, "Syntax", "Content/Scripts/syntax.lua");
    AddScripted(scene, "Missing", "Content/Scripts/nope.lua");
    AddScripted(scene, "Crash", "Content/Scripts/crash.lua");
    const uint64_t good = AddScripted(scene, "Good", "Content/Scripts/good.lua");
    AddScripted(scene, "Loop", "Content/Scripts/loop.lua");

    magnum::Gameplay gameplay;
    CHECK(gameplay.Begin(scene, project.dir)); // broken scripts do not stop play
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i) gameplay.Update(scene, 0.016f, {}); // the endless loop is cut off, the others still run
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 20.0);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(sandbox)->position.x, 7.0f)); // the sandbox script is fine
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(good)->position.x, 3.0f));
    CHECK(gameplay.ScriptCount() == 4); // sandbox, crash, good, loop (syntax error and missing file have no instance)
}

void ScriptsSeeInputWorldAndHud() {
    ScriptProject project;
    project.Write("player.lua",
                  "function on_update(self, dt)\n"
                  "  if input.key('w') then self.position = self.position + vec3(0, 0, -1) end\n"
                  "  if input.pressed('space') then self.state.jumps = (self.state.jumps or 0) + 1; self.rotation = vec3(0, self.state.jumps, 0) end\n"
                  "  local dx, dy = input.mouse_delta()\n"
                  "  self.scale = vec3(1 + dx, 1 + dy, 1)\n"
                  "  hud.text(0.1, 0.2, 'score ' .. 5, 30, 1, 0, 0)\n"
                  "  hud.rect(0, 0, 0.5, 0.1, 0, 1, 0, 1)\n"
                  "  if world.find('Target') and world.find('Target').name == 'Target' then self.state.found = true end\n"
                  "  if self.state.found and input.key('e') then game.stop() end\n"
                  "end\n");
    magnum::Scene scene;
    const uint64_t player = AddScripted(scene, "Player", "Content/Scripts/player.lua");
    scene.CreateActor("Target");

    bool w = false, space = false, e = false;
    magnum::GameInput input;
    input.keyDown = [&](int key) { return (key == 87 && w) || (key == 32 && space) || (key == 69 && e); };
    input.mouseDelta = {2.0f, 3.0f};

    magnum::Gameplay gameplay;
    gameplay.Begin(scene, project.dir);
    gameplay.Update(scene, 0.016f, input);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(player)->position.z, 0.0f)); // W not held
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(player)->scale.x, 3.0f) && Near(scene.TryGet<magnum::TransformComponent>(player)->scale.y, 4.0f));
    CHECK(gameplay.Hud().size() == 2 && gameplay.Hud()[0].text == "score 5" && Near(gameplay.Hud()[0].size, 30.0f));
    CHECK(gameplay.Hud()[1].kind == magnum::HudItem::Kind::Rect);

    w = true;
    gameplay.Update(scene, 0.016f, input);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(player)->position.z, -1.0f));

    space = true; // pressed fires once while the key is held
    gameplay.Update(scene, 0.016f, input);
    gameplay.Update(scene, 0.016f, input);
    gameplay.Update(scene, 0.016f, input);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(player)->rotation.y, 1.0f));

    CHECK(!gameplay.StopRequested());
    e = true;
    gameplay.Update(scene, 0.016f, input);
    CHECK(gameplay.StopRequested());
}

void ScriptsReceiveCollisionAndTriggerEvents() {
    ScriptProject project;
    project.Write("sensor.lua",
                  "function on_trigger(self, other) self.state.entered = (self.state.entered or 0) + 1; self.position = vec3(self.state.entered, 0, 0)\n"
                  "  if other.name == 'Ball' then other:destroy() end end\n"
                  "function on_trigger_exit(self, other) self.rotation = vec3(0, 90, 0) end\n");
    project.Write("ball.lua", "function on_hit(self, other) self.state.hits = (self.state.hits or 0) + 1; self.scale = vec3(self.state.hits + 1, 1, 1) end\n");
    magnum::Scene scene;
    AddBox(scene, "Ground", {0, -0.5f, 0}, {20, 1, 20}, false);
    const uint64_t ball = AddBox(scene, "Ball", {0, 3, 0}, {1, 1, 1}, true);
    scene.Registry().emplace<magnum::ScriptComponent>(scene.Find(ball), magnum::ScriptComponent{"Content/Scripts/ball.lua"});
    const uint64_t sensor = AddBox(scene, "Sensor", {5, 0.5f, 0}, {2, 2, 2}, false, true);
    scene.Registry().emplace<magnum::ScriptComponent>(scene.Find(sensor), magnum::ScriptComponent{"Content/Scripts/sensor.lua"});

    magnum::Gameplay gameplay;
    gameplay.Begin(scene, project.dir);
    for (int i = 0; i < 120; ++i) gameplay.Update(scene, 1.0f / 60.0f, {});
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(ball)->scale.x, 2.0f)); // on_hit once: the landing

    // Push the ball into the sensor: the sensor reports it, destroys it, and the actor is gone at the end of the frame.
    gameplay.GetPhysics().SetVelocity(ball, {8, 0, 0});
    for (int i = 0; i < 60 && scene.Exists(ball); ++i) gameplay.Update(scene, 1.0f / 60.0f, {});
    CHECK(!scene.Exists(ball));
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(sensor)->position.x, 1.0f));
}

void ScriptsCanSpawnAndDestroyActors() {
    ScriptProject project;
    project.Write("spawner.lua",
                  "function on_start(self)\n"
                  "  for i = 1, 3 do\n"
                  "    local a = world.spawn('sphere', i, 2, 0, 'Orb')\n"
                  "    a:add_collider('sphere', true)\n"
                  "    a:add_script('Content/Scripts/orb.lua')\n"
                  "  end\n"
                  "  world.spawn('nonsense', 0, 0, 0)\n"  // reported, returns nil
                  "end\n"
                  "function on_update(self, dt)\n"
                  "  self.state.count = #world.find_all('Orb')\n"
                  "  self.position = vec3(self.state.count, 0, 0)\n"
                  "  local hit = world.raycast(vec3(1, 10, 0), vec3(0, -1, 0), 50)\n"
                  "  self.scale = vec3(1, hit and hit.distance or -1, 1)\n"
                  "end\n");
    project.Write("orb.lua", "function on_start(self) self.rotation = vec3(0, 45, 0) end\nfunction on_update(self, dt) if self.position.x == 2 then self:destroy() end end\n");
    magnum::Scene scene;
    const uint64_t spawner = AddScripted(scene, "Spawner", "Content/Scripts/spawner.lua");
    magnum::Gameplay gameplay;
    gameplay.Begin(scene, project.dir);
    CHECK(scene.Order().size() == 4);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(scene.Order()[1])->rotation.y, 45.0f)); // the spawned actor's script started
    gameplay.Update(scene, 0.016f, {});
    gameplay.Update(scene, 0.016f, {});
    CHECK(scene.Order().size() == 3);                                             // the middle orb destroyed itself
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(spawner)->position.x, 2.0f)); // the spawner saw two orbs
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(spawner)->scale.y, -1.0f)); // the orbs are triggers, so the ray finds nothing solid
}

void ScriptsReloadWhileRunning() {
    ScriptProject project;
    project.Write("mover.lua", "function on_update(self, dt) self.position = vec3(1, 0, 0) end\n");
    magnum::Scene scene;
    const uint64_t id = AddScripted(scene, "Mover", "Content/Scripts/mover.lua");
    magnum::Gameplay gameplay;
    gameplay.Begin(scene, project.dir);
    gameplay.Update(scene, 0.6f, {});
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(id)->position.x, 1.0f));

    const std::filesystem::path file = project.dir / "Content" / "Scripts" / "mover.lua";
    project.Write("mover.lua", "function on_update(self, dt) self.position = vec3(2, 0, 0) end\n");
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(5));
    gameplay.Update(scene, 0.6f, {}); // the file is checked every half second of play
    gameplay.Update(scene, 0.016f, {});
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(id)->position.x, 2.0f));

    // A broken edit keeps the last working version.
    project.Write("mover.lua", "function on_update(self dt)\n");
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(5));
    gameplay.Update(scene, 0.6f, {});
    scene.TryGet<magnum::TransformComponent>(id)->position.x = 0.0f;
    gameplay.Update(scene, 0.016f, {});
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(id)->position.x, 2.0f));
}

void CameraAndGameplayComponentsRoundTripThroughSceneFiles() {
    magnum::Scene scene;
    const uint64_t id = scene.CreateActor("Hero");
    entt::registry& reg = scene.Registry();
    const entt::entity e = scene.Find(id);
    magnum::CameraComponent camera;
    camera.fov = 75.0f;
    camera.primary = false;
    reg.emplace<magnum::CameraComponent>(e, camera);
    magnum::ColliderComponent collider;
    collider.shape = magnum::ColliderShape::Sphere;
    collider.size = {2, 3, 4};
    collider.trigger = true;
    reg.emplace<magnum::ColliderComponent>(e, collider);
    magnum::RigidbodyComponent body;
    body.gravityScale = 0.25f;
    body.kinematic = true;
    reg.emplace<magnum::RigidbodyComponent>(e, body);
    reg.emplace<magnum::ScriptComponent>(e, magnum::ScriptComponent{"Content/Scripts/hero.lua"});

    std::string text, error;
    magnum::SceneEditorState editor;
    CHECK(magnum::SerializeScene(scene, editor, text, error));
    magnum::Scene loaded;
    CHECK(magnum::DeserializeScene(loaded, editor, text, error));
    const magnum::CameraComponent* c = loaded.TryGet<magnum::CameraComponent>(id);
    const magnum::ColliderComponent* col = loaded.TryGet<magnum::ColliderComponent>(id);
    const magnum::RigidbodyComponent* rb = loaded.TryGet<magnum::RigidbodyComponent>(id);
    const magnum::ScriptComponent* sc = loaded.TryGet<magnum::ScriptComponent>(id);
    CHECK(c && Near(c->fov, 75.0f) && !c->primary);
    CHECK(col && col->shape == magnum::ColliderShape::Sphere && col->trigger && Near(col->size.z, 4.0f));
    CHECK(rb && Near(rb->gravityScale, 0.25f) && rb->kinematic && rb->useGravity);
    CHECK(sc && sc->script == "Content/Scripts/hero.lua");

    const magnum::CameraView view = magnum::Gameplay::FindCamera(loaded);
    CHECK(view.valid && Near(view.fov, 75.0f)); // a lone camera is used even when it is not marked primary
}

// The scripts that ship with the sample project must actually play: walk, look, jump and collect.
void ShippedCharacterScriptPlays() {
    magnum::Scene scene;
    AddBox(scene, "Ground", {0, -0.5f, 0}, {60, 1, 60}, false);
    const uint64_t player = scene.CreateActor("Player");
    {
        entt::registry& reg = scene.Registry();
        const entt::entity e = scene.Find(player);
        reg.get<magnum::TransformComponent>(e).position = {0, 1.0f, 0};
        reg.get<magnum::TransformComponent>(e).scale = {1.6f, 1.8f, 1.6f};
        reg.emplace<magnum::MeshComponent>(e, magnum::MeshComponent{magnum::Primitive::Capsule, ""});
        reg.emplace<magnum::ColliderComponent>(e);
        reg.emplace<magnum::RigidbodyComponent>(e);
        reg.emplace<magnum::ScriptComponent>(e, magnum::ScriptComponent{"Content/Scripts/character.lua"});
    }
    const uint64_t camera = scene.CreateActor("Camera");
    scene.Registry().emplace<magnum::CameraComponent>(scene.Find(camera));
    const uint64_t orb = scene.CreateActor("Orb");
    {
        entt::registry& reg = scene.Registry();
        const entt::entity e = scene.Find(orb);
        reg.get<magnum::TransformComponent>(e).position = {0, 0.9f, -3};
        reg.get<magnum::TransformComponent>(e).scale = {0.5f, 0.5f, 0.5f};
        reg.emplace<magnum::MeshComponent>(e, magnum::MeshComponent{magnum::Primitive::Sphere, ""});
        magnum::ColliderComponent collider;
        collider.shape = magnum::ColliderShape::Sphere;
        collider.trigger = true;
        reg.emplace<magnum::ColliderComponent>(e, collider);
        reg.emplace<magnum::ScriptComponent>(e, magnum::ScriptComponent{"Content/Scripts/collectible.lua"});
    }

    bool w = false, space = false;
    glm::vec2 mouse(0.0f);
    magnum::GameInput input;
    input.keyDown = [&](int key) { return (key == 87 && w) || (key == 32 && space); };
    auto frame = [&](magnum::Gameplay& g, int count) {
        for (int i = 0; i < count; ++i) {
            input.mouseDelta = mouse;
            g.Update(scene, 1.0f / 60.0f, input);
            mouse = glm::vec2(0.0f);
        }
    };

    magnum::Gameplay gameplay;
    gameplay.Begin(scene, MAGNUM_PROJECT_DIR);
    CHECK(gameplay.ScriptCount() == 2);
    frame(gameplay, 60); // settle on the ground
    std::printf("  player y=%.3f vy=%.3f grounded=%d\n", scene.TryGet<magnum::TransformComponent>(player)->position.y,
                gameplay.GetPhysics().Velocity(player).y, gameplay.GetPhysics().Grounded(player) ? 1 : 0);
    CHECK(gameplay.GetPhysics().Grounded(player));
    const float startY = scene.TryGet<magnum::TransformComponent>(player)->position.y;

    // Mouse look turns the body and tilts the camera (100 px * 0.12 deg).
    mouse = {100.0f, 50.0f};
    frame(gameplay, 1);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(player)->rotation.y, -12.0f, 0.01f));
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(camera)->rotation.x, -6.0f, 0.01f));
    mouse = {-100.0f, -50.0f}; // and back
    frame(gameplay, 1);

    // Jump: Space while standing.
    space = true;
    frame(gameplay, 1);
    space = false;
    float peak = startY;
    for (int i = 0; i < 130; ++i) {
        frame(gameplay, 1);
        peak = std::max(peak, scene.TryGet<magnum::TransformComponent>(player)->position.y);
    }
    CHECK(peak > startY + 2.0f && peak < startY + 3.5f);
    CHECK(gameplay.GetPhysics().Grounded(player)); // and lands again

    // Walk forward through the orb: it is collected, the HUD says so, the camera follows the head.
    CHECK(scene.Exists(orb));
    w = true;
    frame(gameplay, 90);
    CHECK(!scene.Exists(orb));
    const magnum::TransformComponent* p = scene.TryGet<magnum::TransformComponent>(player);
    CHECK(p->position.z < -5.0f && std::abs(p->position.x) < 0.2f);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(camera)->position.y, p->position.y + 0.7f, 0.01f));
    bool won = false;
    for (const magnum::HudItem& item : gameplay.Hud()) won = won || item.text.find("collected them all") != std::string::npos;
    CHECK(won);
}

