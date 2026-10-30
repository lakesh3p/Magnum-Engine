// ---- UI core: layout, interaction, tweens, text, drawing ---------------------------------------------

namespace {

// A UI system with one 1280 x 720 canvas at scale 1.
struct UiFixture {
    magnum::UiSystem ui;
    magnum::UiCanvas* canvas = nullptr;
    magnum::UiId root = 0;
    UiFixture() {
        magnum::SetEngineRoot(MAGNUM_ENGINE_DIR);
        magnum::SetProjectRoot(MAGNUM_PROJECT_DIR);
        ui.SetViewSize(1280, 720);
        canvas = ui.AddCanvas("main", 0);
        root = canvas->root;
    }
    magnum::UiId Add(magnum::UiKind kind, const char* name, magnum::UiId parent = 0) {
        const magnum::UiId id = ui.Create(kind, name);
        ui.Attach(parent ? parent : root, id);
        return id;
    }
    magnum::UiNode& N(magnum::UiId id) { return *ui.Get(id); }
    void Settle(int frames = 1, magnum::UiPointer pointer = {}) {
        for (int i = 0; i < frames; ++i) ui.Update(1.0f / 60.0f, pointer);
    }
};

bool RectIs(const magnum::UiRect& r, float x, float y, float w, float h) {
    return Near(r.x, x, 0.01f) && Near(r.y, y, 0.01f) && Near(r.w, w, 0.01f) && Near(r.h, h, 0.01f);
}

magnum::UiPointer At(float x, float y, bool down = false, float wheel = 0.0f) {
    magnum::UiPointer p;
    p.position = {x, y};
    p.inView = true;
    p.down[0] = down;
    p.wheel = wheel;
    return p;
}

int CountEvents(const std::vector<magnum::UiEvent>& events, magnum::UiEventType type, magnum::UiId node) {
    int n = 0;
    for (const magnum::UiEvent& e : events) n += e.type == type && e.node == node;
    return n;
}

} // namespace

void UiAnchorsAndPresetsPlaceNodes() {
    UiFixture f;
    const magnum::UiId center = f.Add(magnum::UiKind::Panel, "Center");
    magnum::UiSystem::ApplyAnchorPreset(f.N(center), "center");
    f.N(center).size = {200, 100};
    const magnum::UiId corner = f.Add(magnum::UiKind::Panel, "Corner");
    magnum::UiSystem::ApplyAnchorPreset(f.N(corner), "bottom-right");
    f.N(corner).size = {100, 40};
    f.N(corner).pos = {-16, -16};
    const magnum::UiId fill = f.Add(magnum::UiKind::Panel, "Fill");
    magnum::UiSystem::ApplyAnchorPreset(f.N(fill), "fill");
    f.N(fill).marginMin = {10, 20};
    f.N(fill).marginMax = {30, 40};
    const magnum::UiId top = f.Add(magnum::UiKind::Panel, "Top");
    magnum::UiSystem::ApplyAnchorPreset(f.N(top), "top_wide");
    f.N(top).size = {-1, 50};
    f.N(top).marginMin = {8, 0};
    f.N(top).marginMax = {8, 0};
    CHECK(!magnum::UiSystem::ApplyAnchorPreset(f.N(top), "nonsense"));
    f.Settle();
    CHECK(RectIs(f.N(center).rect, 540, 310, 200, 100));
    CHECK(RectIs(f.N(corner).rect, 1280 - 16 - 100, 720 - 16 - 40, 100, 40));
    CHECK(RectIs(f.N(fill).rect, 10, 20, 1280 - 40, 720 - 60));
    CHECK(RectIs(f.N(top).rect, 8, 0, 1264, 50)); // stretched in x, fixed height at the top edge

    // The canvas follows the view: at 1440p the design space is still 1280 x 720.
    f.ui.SetViewSize(2560, 1440);
    f.Settle();
    CHECK(Near(f.canvas->scale, 2.0f) && RectIs(f.N(center).rect, 540, 310, 200, 100));
    const magnum::UiRect px = f.ui.PixelRect(center);
    CHECK(Near(px.x, 1080.0f, 0.01f) && Near(px.w, 400.0f, 0.01f));
    f.canvas->scaleMode = magnum::UiScaleMode::Constant;
    f.Settle();
    CHECK(Near(f.canvas->scale, 1.0f) && Near(f.canvas->size.x, 2560.0f)); // a constant canvas is as big as the view
    f.canvas->scaleMode = magnum::UiScaleMode::Fit;
    f.canvas->reference = {1000.0f, 1000.0f};
    f.Settle();
    CHECK(Near(f.canvas->scale, 1.44f, 0.001f));
}

void UiRowsColumnsAndGridsFlow() {
    UiFixture f;
    const magnum::UiId col = f.Add(magnum::UiKind::Panel, "Column");
    f.N(col).layout = magnum::UiLayout::Column;
    f.N(col).size = {300, 400};
    f.N(col).padding = {10, 20, 10, 20};
    f.N(col).spacing = {0, 5};
    magnum::UiId kids[3];
    const float heights[3] = {20, 30, 40};
    for (int i = 0; i < 3; ++i) {
        kids[i] = f.Add(magnum::UiKind::Panel, "K", col);
        f.N(kids[i]).size = {100.0f + 10.0f * static_cast<float>(i), heights[i]};
    }
    f.Settle();
    CHECK(RectIs(f.N(kids[0]).rect, 10, 20, 100, 20));
    CHECK(RectIs(f.N(kids[1]).rect, 10, 45, 110, 30));
    CHECK(RectIs(f.N(kids[2]).rect, 10, 80, 120, 40));
    f.N(col).items = magnum::UiItems::Center;
    f.Settle();
    CHECK(Near(f.N(kids[0]).rect.x, 10 + (280 - 100) * 0.5f));
    f.N(col).items = magnum::UiItems::Stretch;
    f.N(kids[1]).size.x = -1; // only auto-width children stretch
    f.Settle();
    CHECK(Near(f.N(kids[1]).rect.w, 280.0f) && Near(f.N(kids[0]).rect.w, 100.0f));

    // Justify along the column: 400 - 40 padding = 360 of room for 20 + 30 + 40 + 2 * 5 = 100.
    f.N(col).justify = magnum::UiJustify::Center;
    f.Settle();
    CHECK(Near(f.N(kids[0]).rect.y, 20 + 130.0f));
    f.N(col).justify = magnum::UiJustify::End;
    f.Settle();
    CHECK(Near(f.N(kids[2]).rect.Bottom(), 400 - 20.0f));
    f.N(col).justify = magnum::UiJustify::SpaceBetween;
    f.Settle();
    CHECK(Near(f.N(kids[0]).rect.y, 20.0f) && Near(f.N(kids[2]).rect.Bottom(), 380.0f));
    f.N(col).justify = magnum::UiJustify::Start;

    // grow shares the free space in proportion.
    f.N(kids[0]).grow = 1;
    f.N(kids[2]).grow = 3;
    f.Settle();
    CHECK(Near(f.N(kids[0]).rect.h, 20 + 260.0f * 0.25f) && Near(f.N(kids[2]).rect.h, 40 + 260.0f * 0.75f));
    CHECK(Near(f.N(kids[2]).rect.Bottom(), 380.0f));

    // A row sizes to its content.
    const magnum::UiId row = f.Add(magnum::UiKind::Panel, "Row");
    f.N(row).layout = magnum::UiLayout::Row;
    f.N(row).spacing = {8, 0};
    f.N(row).pos = {500, 100};
    for (int i = 0; i < 3; ++i) {
        const magnum::UiId c = f.Add(magnum::UiKind::Panel, "R", row);
        f.N(c).size = {40, 30.0f + 5.0f * static_cast<float>(i)};
    }
    f.Settle();
    CHECK(RectIs(f.N(row).rect, 500, 100, 3 * 40 + 2 * 8, 40));

    // A grid of equal columns.
    const magnum::UiId grid = f.Add(magnum::UiKind::Panel, "Grid");
    f.N(grid).layout = magnum::UiLayout::Grid;
    f.N(grid).columns = 3;
    f.N(grid).size = {330, 300};
    f.N(grid).spacing = {15, 10};
    f.N(grid).pos = {600, 300};
    magnum::UiId cells[7];
    for (int i = 0; i < 7; ++i) {
        cells[i] = f.Add(magnum::UiKind::Panel, "C", grid);
        f.N(cells[i]).size = {20, 50};
    }
    f.N(grid).items = magnum::UiItems::Stretch;
    f.N(cells[0]).size.x = -1;
    f.Settle();
    CHECK(Near(f.N(cells[0]).rect.w, 100.0f)); // (330 - 2 * 15) / 3
    CHECK(Near(f.N(cells[4]).rect.x, 600 + 115.0f) && Near(f.N(cells[4]).rect.y, 300 + 60.0f));
    CHECK(Near(f.N(cells[6]).rect.x, 600.0f) && Near(f.N(cells[6]).rect.y, 300 + 120.0f));

    // Hidden children take no room.
    f.N(kids[1]).visible = false;
    f.N(kids[0]).grow = f.N(kids[2]).grow = 0;
    f.Settle();
    CHECK(Near(f.N(kids[2]).rect.y, 20 + 20 + 5.0f));
}

void UiTextMeasuresWrapsAndSizesNodes() {
    UiFixture f;
    magnum::UiFonts& fonts = magnum::UiFonts::Get();
    const int sans = fonts.FontId("Sans");
    CHECK(sans >= 0);
    const magnum::UiTextLayout latin = fonts.Layout(sans, 24, "Hello", 0, magnum::UiTextAlign::Left, 1.0f);
    CHECK(latin.glyphs.size() == 5 && latin.size.x > 30.0f && latin.size.y > 20.0f && latin.lines == 1 && latin.atlas >= 0);
    const magnum::UiTextLayout cyrillic = fonts.Layout(sans, 24, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", 0, magnum::UiTextAlign::Left, 1.0f); // "Привет"
    CHECK(cyrillic.glyphs.size() == 6 && cyrillic.size.x > 40.0f);
    const magnum::UiTextLayout two = fonts.Layout(sans, 24, "one\ntwo", 0, magnum::UiTextAlign::Left, 1.0f);
    CHECK(two.lines == 2 && two.size.y > latin.size.y * 1.8f);
    const magnum::UiTextLayout wrapped = fonts.Layout(sans, 24, "the quick brown fox jumps over", 100.0f, magnum::UiTextAlign::Left, 1.0f);
    CHECK(wrapped.lines >= 3 && wrapped.size.x <= 100.0f + 0.01f);
    const magnum::UiTextLayout centered = fonts.Layout(sans, 24, "ab\nabcdef", 0, magnum::UiTextAlign::Center, 1.0f);
    CHECK(centered.glyphs[0].x0 > fonts.Layout(sans, 24, "abcdef", 0, magnum::UiTextAlign::Left, 1.0f).glyphs[0].x0 + 5.0f); // the short line is moved right
    CHECK(fonts.Layout(sans, 24, "", 0, magnum::UiTextAlign::Left, 1.0f).glyphs.empty());
    // A missing font file falls back to the default one.
    CHECK(fonts.FontId("Content/Fonts/nothing.ttf") == sans);

    // Nodes take the size of their text plus padding.
    const magnum::UiId label = f.Add(magnum::UiKind::Text, "Label");
    f.N(label).text = "Score: 1200";
    f.N(label).fontSize = 24.0f;
    const magnum::UiId button = f.Add(magnum::UiKind::Button, "Button");
    f.N(button).text = "Play";
    f.N(button).pos = {0, 100};
    f.Settle();
    CHECK(f.N(label).rect.w > 90.0f && f.N(label).rect.h > 24.0f);
    CHECK(Near(f.N(button).rect.w, f.ui.MeasureText(f.N(button), 0, 1.0f).x + 28.0f, 0.01f)); // padding 14 + 14
    const float oneLine = f.N(label).rect.h;

    // A wrapping text takes the width it is given and grows tall instead.
    f.N(label).wrap = true;
    f.N(label).size = {80, -1};
    f.Settle();
    CHECK(Near(f.N(label).rect.w, 80.0f) && f.N(label).rect.h > oneLine * 1.8f);
}

void UiScrollViewsClipAndClamp() {
    UiFixture f;
    const magnum::UiId scroll = f.Add(magnum::UiKind::Scroll, "List");
    f.N(scroll).size = {200, 100};
    f.N(scroll).items = magnum::UiItems::Stretch;
    for (int i = 0; i < 10; ++i) {
        const magnum::UiId row = f.Add(magnum::UiKind::Panel, ("Row" + std::to_string(i)).c_str(), scroll);
        f.N(row).size = {-1, 30};
    }
    f.Settle();
    CHECK(Near(f.N(scroll).contentSize.y, 300.0f) && Near(f.N(scroll).scroll.y, 0.0f));
    const magnum::UiId row5 = f.ui.Find("Row5");
    CHECK(Near(f.N(row5).rect.y, 150.0f) && Near(f.N(row5).rect.w, 200.0f));

    // The wheel moves the content, never past its ends.
    f.Settle(1, At(100, 50, false, -1.0f)); // one notch down
    CHECK(f.N(scroll).scroll.y > 20.0f);
    f.Settle(1, At(100, 50, false, -50.0f));
    CHECK(Near(f.N(scroll).scroll.y, 200.0f)); // 300 of content in 100: at most 200
    f.Settle();
    CHECK(Near(f.N(row5).rect.y, 150.0f - 200.0f));
    f.Settle(1, At(100, 50, false, 50.0f));
    CHECK(Near(f.N(scroll).scroll.y, 0.0f));

    // What is outside the view cannot be hit; what is inside can.
    const magnum::UiId button = f.Add(magnum::UiKind::Button, "Inside", f.ui.Find("Row0"));
    f.N(button).size = {50, 20};
    f.Settle();
    f.ui.TakeEvents();
    f.Settle(1, At(10, 10, true));
    f.Settle(1, At(10, 10, false));
    CHECK(CountEvents(f.ui.TakeEvents(), magnum::UiEventType::Click, button) == 1);
    f.Settle(1, At(10, 150, true)); // below the view (the scroll view ends at y = 100)
    CHECK(!f.ui.PointerOverUi());
    f.Settle(1, At(10, 150, false));

    // The scroll bar thumb can be dragged.
    f.Settle(1, At(195, 10));
    CHECK(f.ui.PointerOverUi());
    f.Settle(1, At(196, 12, true));
    f.Settle(1, At(196, 62, true));
    CHECK(f.N(scroll).scroll.y > 80.0f);
    f.Settle(1, At(196, 62, false));
}

void UiPointerEventsClicksTogglesSliders() {
    UiFixture f;
    const magnum::UiId button = f.Add(magnum::UiKind::Button, "Go");
    f.N(button).size = {100, 40};
    f.N(button).pos = {100, 100};
    const magnum::UiId toggle = f.Add(magnum::UiKind::Toggle, "Sound");
    f.N(toggle).text = "Sound";
    f.N(toggle).pos = {100, 200};
    const magnum::UiId slider = f.Add(magnum::UiKind::Slider, "Volume");
    f.N(slider).size = {220, 20};
    f.N(slider).pos = {100, 300};
    f.N(slider).maxValue = 10.0f;
    f.N(slider).step = 1.0f;
    const magnum::UiId panel = f.Add(magnum::UiKind::Panel, "Plain");
    f.N(panel).size = {100, 100};
    f.N(panel).pos = {600, 100};
    f.Settle();
    f.ui.TakeEvents();

    // Hover.
    f.Settle(1, At(150, 120));
    auto events = f.ui.TakeEvents();
    CHECK(CountEvents(events, magnum::UiEventType::Enter, button) == 1 && f.ui.PointerOverUi() && f.N(button).hovered);
    f.Settle(1, At(151, 121));
    CHECK(f.ui.TakeEvents().empty()); // moving inside raises nothing
    // A plain panel lets the pointer through, unless it asks to block it.
    f.Settle(1, At(650, 150));
    CHECK(!f.ui.PointerOverUi());
    f.N(panel).block = 1;
    f.Settle(1, At(651, 150));
    CHECK(f.ui.PointerOverUi() && f.ui.HoveredNode() == panel);
    f.N(panel).block = -1;
    f.ui.TakeEvents();

    // Click: press and release on it.
    f.Settle(1, At(150, 120));
    f.ui.TakeEvents();
    f.Settle(1, At(150, 120, true));
    CHECK(f.N(button).pressed);
    f.Settle(1, At(150, 120, false));
    events = f.ui.TakeEvents();
    CHECK(CountEvents(events, magnum::UiEventType::Press, button) == 1 && CountEvents(events, magnum::UiEventType::Release, button) == 1 &&
          CountEvents(events, magnum::UiEventType::Click, button) == 1 && !f.N(button).pressed);
    // Press on it, drag away, release: no click.
    f.Settle(1, At(150, 120, true));
    f.Settle(1, At(400, 400, true));
    f.Settle(1, At(400, 400, false));
    events = f.ui.TakeEvents();
    CHECK(CountEvents(events, magnum::UiEventType::Click, button) == 0 && CountEvents(events, magnum::UiEventType::Release, button) == 1);
    // A press that starts elsewhere and ends on it is not a click either.
    f.Settle(1, At(400, 400, true));
    f.Settle(1, At(150, 120, true));
    f.Settle(1, At(150, 120, false));
    CHECK(CountEvents(f.ui.TakeEvents(), magnum::UiEventType::Click, button) == 0);
    // A disabled button is dead.
    f.N(button).enabled = false;
    f.Settle(1, At(150, 120));
    f.Settle(1, At(150, 120, true));
    f.Settle(1, At(150, 120, false));
    CHECK(CountEvents(f.ui.TakeEvents(), magnum::UiEventType::Click, button) == 0 && f.ui.PointerOverUi());
    f.N(button).enabled = true;

    // Toggle.
    f.Settle(1, At(110, 209));
    f.Settle(1, At(110, 209, true));
    f.Settle(1, At(110, 209, false));
    events = f.ui.TakeEvents();
    CHECK(Near(f.N(toggle).value, 1.0f) && CountEvents(events, magnum::UiEventType::Change, toggle) == 1);
    f.Settle(1, At(110, 209, true));
    f.Settle(1, At(110, 209, false));
    CHECK(Near(f.N(toggle).value, 0.0f));
    f.ui.TakeEvents();

    // Slider: the press sets the value, dragging follows, the step snaps.
    f.Settle(1, At(210, 310));
    f.Settle(1, At(210, 310, true)); // pad 10, length 200: x = 210 is the middle
    CHECK(Near(f.N(slider).value, 5.0f));
    f.Settle(1, At(260, 500, true)); // far below the slider still drags it; x = 260 is 3/4 of the way: 7.5 snaps to 8
    CHECK(Near(f.N(slider).value, 8.0f));
    f.Settle(1, At(2000, 310, true)); // clamps at the end
    CHECK(Near(f.N(slider).value, 10.0f));
    f.Settle(1, At(2000, 310, false));
    events = f.ui.TakeEvents();
    CHECK(CountEvents(events, magnum::UiEventType::Change, slider) == 3);

    f.ui.SetValue(slider, 4.4f, true); // programmatic changes snap and report too
    CHECK(Near(f.N(slider).value, 4.0f) && CountEvents(f.ui.TakeEvents(), magnum::UiEventType::Change, slider) == 1);
    f.ui.SetValue(slider, 4.0f, true);
    CHECK(f.ui.TakeEvents().empty()); // nothing changed, nothing reported

    // Leaving the view raises Leave.
    f.Settle(1, At(150, 120));
    f.ui.TakeEvents();
    magnum::UiPointer away;
    away.inView = false;
    f.Settle(1, away);
    CHECK(CountEvents(f.ui.TakeEvents(), magnum::UiEventType::Leave, button) == 1 && !f.ui.PointerOverUi());
}

void UiCanvasesOrderPointerAndLayers() {
    UiFixture f;
    magnum::UiCanvas* top = f.ui.AddCanvas("top", 10);
    const magnum::UiId low = f.Add(magnum::UiKind::Button, "Low");
    f.N(low).size = {200, 200};
    const magnum::UiId high = f.ui.Create(magnum::UiKind::Button, "High");
    f.ui.Attach(top->root, high);
    f.N(high).size = {100, 100};
    f.Settle();
    f.Settle(1, At(50, 50));
    CHECK(f.ui.HoveredNode() == high); // the higher canvas gets the pointer first
    f.Settle(1, At(150, 150));
    CHECK(f.ui.HoveredNode() == low);
    CHECK(f.ui.Find("High") == high && f.ui.Find("Nope") == 0);
    CHECK(f.ui.CanvasOf(high) == f.ui.FindCanvas("top"));
    f.ui.FindCanvas("top")->visible = false;
    f.Settle(1, At(50, 50));
    CHECK(f.ui.HoveredNode() == low);

    // Attaching builds the tree; a node cannot go below itself; destroying removes a subtree.
    const magnum::UiId group = f.Add(magnum::UiKind::Panel, "Group");
    const magnum::UiId a = f.Add(magnum::UiKind::Panel, "A", group);
    const magnum::UiId b = f.Add(magnum::UiKind::Panel, "B", a);
    CHECK(!f.ui.Attach(b, group) && !f.ui.Attach(a, a));
    CHECK(f.ui.Attach(group, b, 0) && f.N(group).children.front() == b);
    f.ui.Destroy(group);
    CHECK(!f.ui.Exists(group) && !f.ui.Exists(a) && !f.ui.Exists(b));
    CHECK(f.ui.FindAll("Hi").size() == 1);
    f.ui.Destroy(f.root); // canvas roots stay
    CHECK(f.ui.Exists(f.root));
    f.ui.RemoveCanvas("top");
    CHECK(f.ui.FindCanvas("top") == nullptr && !f.ui.Exists(high));
}

void UiTweensAnimateAndFinish() {
    UiFixture f;
    const magnum::UiId node = f.Add(magnum::UiKind::Panel, "Fader");
    f.N(node).size = {100, 100};
    magnum::UiTween t;
    t.node = node;
    t.prop = magnum::UiProp::Opacity;
    t.from = glm::vec4(0.0f);
    t.to = glm::vec4(1.0f);
    t.hasFrom = true;
    t.duration = 1.0f;
    t.ease = magnum::UiEase::Linear;
    t.token = 77;
    CHECK(f.ui.Animate(t));
    f.Settle(30);
    CHECK(Near(f.N(node).opacity, 0.5f, 0.02f) && f.ui.HasTweens(node));
    f.Settle(40);
    CHECK(Near(f.N(node).opacity, 1.0f) && !f.ui.HasTweens(node));
    bool done = false;
    for (const magnum::UiEvent& e : f.ui.TakeEvents()) done = done || (e.type == magnum::UiEventType::TweenDone && e.token == 77);
    CHECK(done);

    // `from` defaults to the current value, a delay holds the start, a new tween replaces the old one.
    t.hasFrom = false;
    t.token = 0;
    t.prop = magnum::UiProp::Pos;
    t.from = glm::vec4(0.0f);
    t.to = glm::vec4(100.0f, 50.0f, 0, 0);
    t.delay = 0.5f;
    t.duration = 0.5f;
    f.N(node).pos = {10, 10};
    f.ui.Animate(t);
    f.Settle(15);
    CHECK(Near(f.N(node).pos.x, 10.0f)); // still waiting
    f.Settle(46);
    CHECK(Near(f.N(node).pos.x, 100.0f) && Near(f.N(node).pos.y, 50.0f));

    // Eases: endpoints and monotonic shape.
    for (int e = 0; e < static_cast<int>(magnum::UiEase::Count); ++e) {
        const auto ease = static_cast<magnum::UiEase>(e);
        CHECK(Near(magnum::UiApplyEase(ease, 0.0f), 0.0f, 1e-3f) && Near(magnum::UiApplyEase(ease, 1.0f), 1.0f, 1e-3f));
        magnum::UiEase parsed;
        CHECK(magnum::UiEaseFromName(magnum::UiEaseName(ease), parsed) && parsed == ease);
    }
    magnum::UiEase named;
    CHECK(magnum::UiEaseFromName("Out-Cubic", named) && named == magnum::UiEase::OutCubic && !magnum::UiEaseFromName("wobbly", named));
    CHECK(magnum::UiApplyEase(magnum::UiEase::OutBack, 0.8f) > 1.0f); // overshoots

    // Loop + yoyo go back and forth until stopped; remove_node deletes the node when the tween ends.
    magnum::UiTween pulse;
    pulse.node = node;
    pulse.prop = magnum::UiProp::Scale;
    pulse.from = glm::vec4(1.0f);
    pulse.to = glm::vec4(2.0f);
    pulse.hasFrom = true;
    pulse.duration = 0.5f;
    pulse.ease = magnum::UiEase::Linear;
    pulse.loop = true;
    pulse.yoyo = true;
    f.ui.Animate(pulse);
    f.Settle(30);
    CHECK(Near(f.N(node).scale, 2.0f, 0.05f));
    f.Settle(30);
    CHECK(Near(f.N(node).scale, 1.0f, 0.05f) && f.ui.HasTweens(node));
    f.ui.StopTweens(node);
    CHECK(!f.ui.HasTweens(node));

    const magnum::UiId popup = f.Add(magnum::UiKind::Text, "Popup");
    magnum::UiTween fade;
    fade.node = popup;
    fade.prop = magnum::UiProp::Opacity;
    fade.to = glm::vec4(0.0f);
    fade.duration = 0.2f;
    fade.removeNode = true;
    f.ui.Animate(fade);
    f.Settle(20);
    CHECK(!f.ui.Exists(popup));
    CHECK(!f.ui.Animate(fade)); // a dead node cannot be animated
}

void UiVisualTransformsAndStates() {
    UiFixture f;
    const magnum::UiId button = f.Add(magnum::UiKind::Button, "Styled");
    f.N(button).size = {100, 40};
    f.N(button).style.base.bg = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
    f.N(button).style.hover.bg = glm::vec4(0.9f, 0.8f, 0.7f, 1.0f);
    f.N(button).style.transition = 0.0f; // jump straight to the new look
    f.Settle(2);
    CHECK(Near(f.N(button).visual.bg.r, 0.1f));
    f.Settle(1, At(50, 20));
    CHECK(Near(f.N(button).visual.bg.r, 0.9f));
    f.Settle(1, At(500, 500));
    CHECK(Near(f.N(button).visual.bg.r, 0.1f));
    // Without a hover color the button brightens by itself; pressing darkens it; disabled fades.
    f.N(button).style.hover.bg.reset();
    f.Settle(1, At(50, 20));
    const float hovered = f.N(button).visual.bg.r;
    CHECK(hovered > 0.1f && hovered < 0.9f);
    f.Settle(1, At(50, 20, true));
    CHECK(f.N(button).visual.bg.r < hovered);
    f.Settle(1, At(50, 20, false));
    f.N(button).enabled = false;
    f.Settle(1, At(500, 500));
    CHECK(Near(f.N(button).visual.bg.a, 0.5f));

    // Easing takes more than one frame.
    f.N(button).enabled = true;
    f.N(button).style.transition = 0.2f;
    f.Settle(30);
    f.Settle(1, At(50, 20));
    CHECK(f.N(button).visual.bg.r > 0.1f && f.N(button).visual.bg.r < hovered);

    // Scale and rotation move the hit area with the picture.
    const magnum::UiId box = f.Add(magnum::UiKind::Button, "Spinner");
    f.N(box).size = {100, 20};
    f.N(box).pos = {400, 300};
    f.N(box).pivot = {0.5f, 0.5f};
    f.N(box).rotation = 90.0f; // a 100 x 20 box (centered on (400, 300)) turned upright
    f.Settle();
    f.Settle(1, At(400, 340)); // inside the rotated box, outside the unrotated one
    CHECK(f.ui.HoveredNode() == box);
    f.Settle(1, At(440, 300)); // inside the unrotated box only
    CHECK(f.ui.HoveredNode() != box);
    f.N(box).rotation = 0.0f;
    f.N(box).scale = 2.0f;
    f.Settle();
    f.Settle(1, At(310, 300)); // the scaled box reaches 100 units to each side of its center
    CHECK(f.ui.HoveredNode() == box);
    f.Settle(1, At(290, 300));
    CHECK(f.ui.HoveredNode() != box);

    // Opacity multiplies down the tree.
    const magnum::UiId parent = f.Add(magnum::UiKind::Panel, "Dim");
    const magnum::UiId child = f.Add(magnum::UiKind::Panel, "Dimmed", parent);
    f.N(parent).opacity = 0.5f;
    f.N(child).opacity = 0.5f;
    f.Settle();
    CHECK(Near(f.N(child).worldOpacity, 0.25f));
}

void UiDrawListHasShapesTextImagesAndClips() {
    UiFixture f;
    const magnum::UiId panel = f.Add(magnum::UiKind::Panel, "Panel");
    f.N(panel).size = {300, 200};
    f.N(panel).style.base.bg = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    f.N(panel).style.base.border = glm::vec4(1.0f);
    f.N(panel).style.base.borderWidth = 2.0f;
    f.N(panel).style.base.radius = 8.0f;
    const magnum::UiId text = f.Add(magnum::UiKind::Text, "Text", panel);
    f.N(text).text = "Hi";
    const magnum::UiId image = f.Add(magnum::UiKind::Image, "Image", panel);
    f.N(image).image = "Content/Textures/checker.png";
    f.N(image).size = {64, 64};
    f.N(image).pos = {10, 50};
    const magnum::UiId nine = f.Add(magnum::UiKind::Image, "Nine", panel);
    f.N(nine).image = "Content/Textures/checker.png";
    f.N(nine).slice = {32, 32, 32, 32};
    f.N(nine).size = {100, 100};
    f.N(nine).pos = {100, 50};
    f.Settle(2);

    magnum::UiDrawList list;
    f.ui.BuildDrawList(list);
    CHECK(!list.Empty() && list.viewWidth == 1280 && list.viewHeight == 720);
    CHECK(list.images.size() == 1 && list.images[0] == "Content/Textures/checker.png");
    CHECK(list.indices.size() == list.vertices.size() / 4 * 6);
    int textCommands = 0, imageCommands = 0;
    uint32_t covered = 0;
    for (const magnum::UiDrawCommand& c : list.commands) {
        textCommands += c.atlas >= 0;
        imageCommands += c.image >= 0;
        covered += c.indexCount;
    }
    CHECK(textCommands == 1 && imageCommands == 1 && covered == list.indices.size());
    // 1 panel + 2 glyphs + 1 image + 9 slices
    CHECK(list.vertices.size() == 4 * (1 + 2 + 1 + 9));
    // The panel quad: 300 x 200 at the origin with its radius and border.
    const magnum::UiVertex& first = list.vertices[0];
    CHECK(Near(first.pos.x, 0.0f) && Near(first.shape.x, 150.0f) && Near(first.shape.y, 100.0f) && Near(first.shape.z, 8.0f) && Near(first.shape.w, 2.0f));
    CHECK(Near(list.vertices[2].pos.x, 300.0f) && Near(list.vertices[2].pos.y, 200.0f));

    // Hidden things are not drawn; transparent things neither.
    f.N(nine).visible = false;
    f.N(image).opacity = 0.0f;
    f.Settle();
    f.ui.BuildDrawList(list);
    CHECK(list.vertices.size() == 4 * (1 + 2) && list.images.empty());

    // A clipping node cuts its children: the commands inside carry a scissor rectangle.
    f.N(panel).clip = true;
    f.Settle();
    f.ui.BuildDrawList(list);
    bool clipped = !list.commands.empty();
    for (const magnum::UiDrawCommand& c : list.commands) clipped = clipped && c.clipped && c.clip[2] == 300 && c.clip[3] == 200;
    CHECK(clipped);

    // The scale of the canvas multiplies every size.
    f.ui.SetViewSize(2560, 1440);
    f.Settle();
    f.ui.BuildDrawList(list);
    CHECK(Near(list.vertices[2].pos.x, 600.0f) && Near(list.vertices[0].shape.z, 16.0f));
}

void UiSwitchProgressAndSlidersDraw() {
    UiFixture f;
    const magnum::UiId bar = f.Add(magnum::UiKind::Progress, "Bar");
    f.N(bar).size = {200, 12};
    f.N(bar).value = 0.5f;
    const magnum::UiId toggle = f.Add(magnum::UiKind::Toggle, "Switch");
    f.N(toggle).look = magnum::UiToggleLook::Switch;
    f.N(toggle).value = 1.0f;
    f.N(toggle).pos = {0, 50};
    const magnum::UiId slider = f.Add(magnum::UiKind::Slider, "Slider");
    f.N(slider).pos = {0, 100};
    f.N(slider).value = 0.25f;
    f.Settle(2);
    magnum::UiDrawList list;
    f.ui.BuildDrawList(list);
    // progress: track + fill; switch: track + knob; slider: track + fill + knob
    CHECK(list.vertices.size() == 4 * (2 + 2 + 3));
    // Progress at 50%: the fill is half of the inner width (200 - 2 * 1 border).
    const magnum::UiVertex& fill = list.vertices[4];
    CHECK(Near(fill.shape.x, 49.5f, 0.01f));
    // A progress bar at 0 draws only its track.
    f.N(bar).value = 0.0f;
    f.Settle();
    f.ui.BuildDrawList(list);
    CHECK(list.vertices.size() == 4 * (1 + 2 + 3));
    CHECK(Near(f.N(toggle).rect.w, 34.0f) && Near(f.N(slider).rect.w, 160.0f));
}
