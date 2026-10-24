#include "as/Player.h"

#include <cstdio>
#include <functional>

#include "as/Audio.h"
#include "as/Builtins.h"

namespace as {

using flash::DisplayObject;
using flash::MovieClip;

namespace {

const char* methodFor(uint32_t event) {
    switch (event) {
        case clipevent::Load: return "onLoad";
        case clipevent::EnterFrame: return "onEnterFrame";
        case clipevent::Unload: return "onUnload";
        case clipevent::MouseMove: return "onMouseMove";
        case clipevent::MouseDown: return "onMouseDown";
        case clipevent::MouseUp: return "onMouseUp";
        case clipevent::Data: return "onData";
        case clipevent::Press: return "onPress";
        case clipevent::Release: return "onRelease";
        case clipevent::ReleaseOutside: return "onReleaseOutside";
        case clipevent::RollOver: return "onRollOver";
        case clipevent::RollOut: return "onRollOut";
        case clipevent::DragOver: return "onDragOver";
        case clipevent::DragOut: return "onDragOut";
        default: return nullptr;
    }
}

bool installed = false;

}  // namespace

Player::Player(const flash::Movie& movie) : movie_(movie) {
    Runtime& rt = Runtime::get();
    if (!installed) {
        installStageBuiltins(rt);
        installed = true;
    }
    flash::timelineHooks = this;
    stage().movie = &movie;
    rt.rootProviders.push_back([this](Gc& gc) { markRoots(gc); });

    root_ = std::make_shared<MovieClip>(movie, const_cast<flash::Sprite*>(&movie.root));
    stage().root = root_;
    rt.global->defineSlot(internLiteral("_level0"), valueOf(root_.get()), DontEnum);
    onCreated(*root_, nullptr, 0);
    root_->initialize();
    runActions();
}

Player::~Player() {
    if (flash::timelineHooks == this) flash::timelineHooks = nullptr;
    Runtime& rt = Runtime::get();
    rt.rootProviders.pop_back();
    stage().root.reset();
    stage().movie = nullptr;
}

// ---------------------------------------------------------------- hooks

void Player::onCreated(DisplayObject& obj, const flash::PlaceCommand* place, uint32_t placeIndex) {
    bool named = dynamic_cast<MovieClip*>(&obj) || dynamic_cast<flash::ButtonObject*>(&obj) ||
                 dynamic_cast<flash::TextField*>(&obj);
    if (named && obj.name.empty() && &obj != root_.get()) obj.name = "instance" + std::to_string(instanceCounter_++);

    if (auto* tf = dynamic_cast<flash::TextField*>(&obj)) {
        if (!tf->variable.empty()) {
            // Bind at creation, like Flash: the timeline's scripts for this frame run
            // afterwards, so they overwrite the placeholder text they find in the variable.
            bindings_.push_back({std::static_pointer_cast<flash::TextField>(tf->shared_from_this()), "", false});
            syncBinding(bindings_.back());
        }
        return;
    }
    auto* mc = dynamic_cast<MovieClip*>(&obj);
    if (!mc) return;
    StageObject* so = stageObject(mc);
    if (place && place->hasClipActions && mc->parent())
        so->handlers = findClipEvents(mc->parent()->sprite()->id, placeIndex);
    execList_.push_front(std::static_pointer_cast<MovieClip>(mc->shared_from_this()));

    uint32_t mask = so->handlerMask();
    if (mask & clipevent::Initialize) queue(obj, Kind::Event, clipevent::Initialize, Initialize);
    if (mask & clipevent::Construct) queue(obj, Kind::Event, clipevent::Construct, Construct);
    queue(obj, Kind::Event, clipevent::Load);
}

void Player::onRemoved(DisplayObject& obj) {
    // Children unload before their parent.
    obj.forEachChild([this](DisplayObject& c) { onRemoved(c); });
    if (auto* so = static_cast<StageObject*>(obj.scriptObject)) so->path = targetPath(&obj);
    if (dynamic_cast<MovieClip*>(&obj)) queue(obj, Kind::Event, clipevent::Unload);
}

void Player::onFrameAction(MovieClip& clip, uint32_t actionIndex) {
    queue(clip, Kind::Frame, actionIndex);
}

void Player::onStartSound(MovieClip& clip, const flash::SoundStart& sound) {
    if (audio_) audio_->startSound(clip, sound);
}

void Player::onStreamBlock(MovieClip& clip, uint32_t blockIndex) {
    if (audio_) audio_->streamBlock(clip, blockIndex);
}

// ---------------------------------------------------------------- action queue

void Player::queue(DisplayObject& obj, Kind kind, uint32_t value, Priority p) {
    queue_[p].push_back({obj.shared_from_this(), kind, value});
}

void Player::runActions() {
    for (int guard = 0; guard < 1000000; guard++) {
        int p = 0;
        while (p < PriorityCount && queue_[p].empty()) p++;
        if (p == PriorityCount) return;
        Action a = std::move(queue_[p].front());
        queue_[p].pop_front();
        execute(a);
    }
    std::fprintf(stderr, "action queue did not drain; dropping remaining actions\n");
    for (auto& q : queue_) q.clear();
}

void Player::execute(const Action& a) {
    DisplayObject& d = *a.obj;
    bool unload = a.kind == Kind::Event && a.value == clipevent::Unload;
    if (isRemoved(&d) && !unload) return;  // removed clips run nothing but onUnload
    switch (a.kind) {
        case Kind::Frame: {
            auto* mc = static_cast<MovieClip*>(&d);
            uint16_t spriteId = mc->sprite()->id;
            ScriptFn fn = findFrameScript(spriteId, a.value);
            if (!fn) {
                missing_.insert({spriteId, a.value});
                return;
            }
            StageObject* so = stageObject(&d);
            Activation act(Value(so), {}, so->targetScope(), so);
            fn(act);
            break;
        }
        case Kind::Event:
            runEvent(d, a.value);
            break;
        case Kind::Button:
            runButton(d, a.value);
            break;
    }
}

void Player::runEvent(DisplayObject& d, uint32_t event) {
    StageObject* so = stageObject(&d);
    if (!so) return;
    std::vector<ClipHandler> handlers = so->handlers;
    for (const ClipHandler& h : handlers) {
        if (!(h.events & event)) continue;
        Activation act(Value(so), {}, so->targetScope(), so);
        h.fn(act);
    }
    if (const char* m = methodFor(event)) {
        Value fn = getMember(Value(so), intern(m));
        if (fn.isObject() && fn.object()->isCallable()) callFunction(fn, Value(so), {});
    }
}

void Player::runButton(DisplayObject& button, uint32_t condition) {
    // on(...) code on a button runs in the timeline that contains the button.
    MovieClip* parent = button.parent();
    if (!parent) return;
    StageObject* target = stageObject(parent);
    for (const ClipHandler& h : findButtonActions(button.definition()->id)) {
        if (!(h.events & condition)) continue;
        Activation act(Value(target), {}, target->targetScope(), target);
        h.fn(act);
    }
}

std::vector<std::shared_ptr<MovieClip>> Player::liveClips() {
    std::vector<std::shared_ptr<MovieClip>> out;
    out.reserve(execList_.size());
    for (auto it = execList_.begin(); it != execList_.end();) {
        auto mc = it->lock();
        if (!mc || isRemoved(mc.get())) {
            it = execList_.erase(it);
            continue;
        }
        out.push_back(std::move(mc));
        ++it;
    }
    return out;
}

void Player::broadcastEvent(uint32_t event) {
    const char* method = methodFor(event);
    Atom m = method ? intern(method) : nullptr;
    for (auto& mc : liveClips()) {
        auto* so = static_cast<StageObject*>(mc->scriptObject);
        if (!so) continue;
        bool wants = (so->handlerMask() & event) != 0;
        if (!wants && m) {
            Value fn = getMember(Value(so), m);
            wants = fn.isObject() && fn.object()->isCallable();
        }
        if (wants) queue(*mc, Kind::Event, event);
    }
}

// ---------------------------------------------------------------- frame

void Player::tick() {
    Runtime& rt = Runtime::get();
    for (auto& mc : liveClips()) {
        if (isRemoved(mc.get())) continue;  // removed earlier in this pass
        queue(*mc, Kind::Event, clipevent::EnterFrame);
        mc->runFrameSelf();
    }
    runActions();
    updateBindings();
    runIntervals(rt.timeMs());
    if (audio_) audio_->update(frames_);  // may call onSoundComplete
    runActions();
    frames_++;
    rt.maybeCollect();
}

void Player::render(flash::Rasterizer& r, flash::Surface& dst) {
    flash::Color bg = movie_.background;
    dst.clear(0xFF000000u | bg.r << 16 | bg.g << 8 | bg.b);
    flash::renderDisplayObject(*root_, r, dst, {0, 0, dst.width, dst.height}, flash::Matrix::scale(0.05, 0.05),
                               flash::ColorTransform{}, movie_);
}

// ---------------------------------------------------------------- text bindings

void Player::updateBindings() {
    for (auto it = bindings_.begin(); it != bindings_.end();) {
        auto tf = it->field.lock();
        if (!tf || isRemoved(tf.get())) {
            it = bindings_.erase(it);
            continue;
        }
        syncBinding(*it);
        ++it;
    }
}

void Player::syncBinding(Binding& b) {
    auto tf = b.field.lock();
    if (!tf) return;
    // "path.to.var" or "path:var" relative to the field's parent timeline.
    std::string var = tf->variable;
    DisplayObject* base = tf->parent();
    size_t cut = var.find_last_of(".:");
    if (cut != std::string::npos) {
        base = resolvePath(base, var.substr(0, cut));
        var = var.substr(cut + 1);
    }
    if (!base || var.empty()) return;
    Value owner = valueOf(base);
    Atom name = intern(var);
    Value v = getMember(owner, name);
    if (!b.initialized) {
        b.initialized = true;
        // An undefined variable takes the field's initial text.
        if (v.isUndefined()) {
            setMember(owner, name, Value(tf->text()));
            b.last = tf->text();
            return;
        }
    }
    std::string text = v.isUndefined() ? "" : v.toString();
    if (text != b.last) {
        b.last = text;
        if (tf->html()) tf->setHtmlText(text);
        else tf->setText(text);
    }
}

// ---------------------------------------------------------------- input

void Player::keyDown(int code, int ascii) {
    keyEvent(code, ascii, true);
    broadcastEvent(clipevent::KeyDown);
    runActions();
}

void Player::keyUp(int code) {
    keyEvent(code, 0, false);
    broadcastEvent(clipevent::KeyUp);
    runActions();
}

bool Player::isInteractive(DisplayObject& d) {
    if (dynamic_cast<flash::ButtonObject*>(&d)) return true;
    auto* so = static_cast<StageObject*>(d.scriptObject);
    if (!so || !dynamic_cast<MovieClip*>(&d)) return false;
    if (so->handlerMask() & clipevent::ButtonEvents) return true;
    for (const char* m : {"onPress", "onRelease", "onReleaseOutside", "onRollOver", "onRollOut"}) {
        Value fn = getMember(Value(so), intern(m));
        if (fn.isObject() && fn.object()->isCallable()) return true;
    }
    return false;
}

DisplayObject* Player::pick(DisplayObject& d, const flash::Matrix& m, flash::Point p) {
    auto* mc = dynamic_cast<MovieClip*>(&d);
    if (!mc) return nullptr;
    const auto& kids = mc->children();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        DisplayObject& c = *it->second;
        if (!c.visible || c.clipDepth) continue;
        if (mc->maskBlocks(it->first, m, p)) continue;  // masked away at this point
        flash::Matrix cm = m * c.matrix;
        if (isInteractive(c)) {
            if (c.hitTest(cm, p)) return &c;
            continue;  // an interactive clip owns its subtree's mouse events
        }
        if (DisplayObject* hit = pick(c, cm, p)) return hit;
    }
    return nullptr;
}

void Player::setButtonState(DisplayObject* d, uint8_t state) {
    if (auto* b = dynamic_cast<flash::ButtonObject*>(d)) b->setState(state);
}

void Player::pointerEvent(DisplayObject* d, uint32_t event, uint32_t buttonCond) {
    if (!d) return;
    if (dynamic_cast<flash::ButtonObject*>(d)) queue(*d, Kind::Button, buttonCond);
    else queue(*d, Kind::Event, event);
}

void Player::mouseMove(double x, double y) {
    stage().mouseX = x;
    stage().mouseY = y;
    mouseEvent("onMouseMove");
    broadcastEvent(clipevent::MouseMove);

    DisplayObject* hit = pick(*root_, root_->matrix, {x * 20, y * 20});
    auto hover = hover_.lock();
    auto pressed = pressed_.lock();
    if (hit != hover.get()) {
        if (hover) {
            if (pressed && pressed == hover) {
                pointerEvent(hover.get(), clipevent::DragOut, buttoncond::DragOut);
                setButtonState(hover.get(), flash::ButtonCharacter::Over);
            } else {
                pointerEvent(hover.get(), clipevent::RollOut, buttoncond::RollOut);
                setButtonState(hover.get(), flash::ButtonCharacter::Up);
            }
        }
        if (hit) {
            if (pressed && pressed.get() == hit) {
                pointerEvent(hit, clipevent::DragOver, buttoncond::DragOver);
                setButtonState(hit, flash::ButtonCharacter::Down);
            } else if (!pressed) {
                pointerEvent(hit, clipevent::RollOver, buttoncond::RollOver);
                setButtonState(hit, flash::ButtonCharacter::Over);
            }
        }
        hover_ = hit ? hit->shared_from_this() : nullptr;
    }
    runActions();
}

void Player::mouseDown() {
    mouseEvent("onMouseDown");
    broadcastEvent(clipevent::MouseDown);
    if (auto hover = hover_.lock()) {
        pressed_ = hover;
        setButtonState(hover.get(), flash::ButtonCharacter::Down);
        pointerEvent(hover.get(), clipevent::Press, buttoncond::Press);
    }
    runActions();
}

void Player::mouseUp() {
    mouseEvent("onMouseUp");
    broadcastEvent(clipevent::MouseUp);
    auto pressed = pressed_.lock();
    auto hover = hover_.lock();
    if (pressed) {
        if (pressed == hover) {
            setButtonState(pressed.get(), flash::ButtonCharacter::Over);
            pointerEvent(pressed.get(), clipevent::Release, buttoncond::Release);
        } else {
            setButtonState(pressed.get(), flash::ButtonCharacter::Up);
            pointerEvent(pressed.get(), clipevent::ReleaseOutside, buttoncond::ReleaseOutside);
        }
    }
    pressed_.reset();
    runActions();
}

std::vector<Player::Clickable> Player::clickables() {
    std::vector<Clickable> out;
    std::function<void(DisplayObject&, bool)> visit = [&](DisplayObject& d, bool visible) {
        visible = visible && d.visible;
        if (&d != root_.get() && isInteractive(d)) {
            flash::Rect b = d.worldMatrix().apply(d.localBounds());
            auto* so = static_cast<StageObject*>(d.scriptObject);
            std::string methods;
            if (so)
                for (const char* m : {"onPress", "onRelease", "onReleaseOutside", "onRollOver", "onRollOut"}) {
                    Value fn = getMember(Value(so), intern(m));
                    if (fn.isObject() && fn.object()->isCallable()) methods += std::string(methods.empty() ? "" : ",") + m;
                }
            out.push_back({targetPath(&d), {b.xMin / 20, b.yMin / 20, b.xMax / 20, b.yMax / 20}, visible,
                           d.definition() ? d.definition()->id : -1, so ? so->handlerMask() : 0, methods});
            return;  // an interactive object owns its subtree's mouse events
        }
        d.forEachChild([&](DisplayObject& c) { visit(c, visible); });
    };
    visit(*root_, true);
    return out;
}

std::vector<Player::TextHit> Player::textContaining(const std::string& needle) {
    // Only fields a viewer could see: visible chain, combined alpha > 5%, on the stage.
    std::vector<TextHit> out;
    std::function<void(DisplayObject&, float)> visit = [&](DisplayObject& d, float alpha) {
        if (!d.visible) return;
        alpha *= d.cxform.aMul;
        if (alpha <= 0.05f && d.cxform.aAdd <= 0) return;
        if (auto* tf = dynamic_cast<flash::TextField*>(&d)) {
            flash::Rect b = d.worldMatrix().apply(tf->bounds);
            bool onStage = b.xMax > 0 && b.yMax > 0 && b.xMin < 450 * 20 && b.yMin < 300 * 20;
            if (onStage && tf->text().find(needle) != std::string::npos)
                out.push_back({targetPath(&d), tf->variable, tf->text().substr(0, 60)});
        }
        d.forEachChild([&](DisplayObject& c) { visit(c, alpha); });
    };
    visit(*root_, 1.f);
    return out;
}

std::vector<Player::TextOverflow> Player::textOverflows() {
    std::vector<TextOverflow> out;
    std::function<void(DisplayObject&)> visit = [&](DisplayObject& d) {
        if (!d.visible) return;
        if (auto* tf = dynamic_cast<flash::TextField*>(&d)) {
            flash::Rect e = tf->textExtent(), b = tf->bounds;
            const double slack = 20;  // 1 px
            if (!e.empty() && (e.xMin < b.xMin - slack || e.xMax > b.xMax + slack || e.yMin < b.yMin - slack ||
                               e.yMax > b.yMax + slack))
                out.push_back({targetPath(&d), tf->text().substr(0, 40),
                               {b.xMin / 20, b.yMin / 20, b.xMax / 20, b.yMax / 20},
                               {e.xMin / 20, e.yMin / 20, e.xMax / 20, e.yMax / 20}, tf->edit()->autoSize});
        }
        d.forEachChild(visit);
    };
    visit(*root_);
    return out;
}

// ---------------------------------------------------------------- GC

void Player::markTree(Gc& gc, DisplayObject& d) {
    gc.mark(static_cast<Object*>(static_cast<StageObject*>(d.scriptObject)));
    d.forEachChild([&](DisplayObject& c) { markTree(gc, c); });
}

void Player::markRoots(Gc& gc) {
    if (root_) markTree(gc, *root_);
    if (audio_) audio_->trace(gc);
    for (auto& q : queue_)
        for (auto& a : q) gc.mark(static_cast<Object*>(static_cast<StageObject*>(a.obj->scriptObject)));
}

}  // namespace as
