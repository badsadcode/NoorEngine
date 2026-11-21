// A running game's UI documents (world_ui.h).
#include "noor/ui/world_ui.h"

#include "noor/foundation/log.h"

namespace noor::ui {

WorldUi::WorldUi(UiDesc desc) : desc_(std::move(desc)) {}

WorldUi::~WorldUi() {
    if (world_ && world_->ui_documents() == this) world_->set_ui_documents(nullptr);
}

void WorldUi::attach(runtime::World& world) {
    world_ = &world;
    world.set_ui_documents(this);
    for (const auto& e : world.scene().entities) {
        if (!e.has("ui_document")) continue;
        const asset::Json& c = e.components.at("ui_document");
        if (!c.is_object()) continue;
        const std::string path = c.value("document", std::string());
        if (path.empty()) {
            NOOR_LOG_WARN("UI", "{}: its ui_document has no \"document\"", e.name);
            continue;
        }
        auto d = open(path);
        if (!d) {
            NOOR_LOG_ERROR("UI", "{}: {}", e.name, d.error().describe());
            continue;
        }
        world.own_ui_document(*d, e.id);
        if (!c.value("visible", true)) set_visible(*d, false);
    }
}

void WorldUi::input(const Input& in) {
    if (!ui_) return;
    ui_->set_viewport(x_, y_, width_, height_);
    if (in.mouse) {
        ui_->mouse_move(in.mouse_x, in.mouse_y);
        for (int b = 0; b < 3; ++b) {
            if (in.pressed[static_cast<std::size_t>(b)]) ui_->mouse_button(b, true);
            if (in.released[static_cast<std::size_t>(b)]) ui_->mouse_button(b, false);
        }
        if (in.wheel != 0.0f) ui_->mouse_wheel(in.wheel);
    }
    for (const auto& [key, down] : in.keys) ui_->key(key, down, in.modifiers);
    ui_->text(in.text);
    forward_events();
}

render::UiFrame WorldUi::draw(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    x_ = x, y_ = y, width_ = width, height_ = height;
    if (!ui_) return {};
    ui_->set_viewport(x, y, width, height);
    ui_->update();
    forward_events();
    return ui_->draw();
}

void WorldUi::forward_events() {
    if (!ui_ || !world_) {
        if (ui_) (void)ui_->take_events();
        return;
    }
    for (auto& e : ui_->take_events()) {
        const Guid from = world_->ui_emitter(e.document.id);
        const std::string named = e.element.empty() ? std::string() : e.type + "#" + e.element;
        // The mouse moving over things makes many; only those someone listens to are sent.
        const bool hovering = e.type == "mouseover" || e.type == "mouseout";
        const bool any = !hovering || world_->has_listeners(e.type);
        const bool one = !named.empty() && (!hovering || world_->has_listeners(named));
        if (!any && !one) continue;
        asset::Json data{{"type", e.type}, {"element", e.element}, {"value", std::move(e.value)}};
        if (any) world_->queue_message(from, e.type, data);
        if (one) world_->queue_message(from, named, data);
    }
}

Result<std::uint32_t> WorldUi::open(std::string_view path) {
    if (!ui_) {
        UiDesc d = desc_;
        auto made = Ui::create(std::move(d));
        if (!made) return std::unexpected(made.error());
        ui_ = std::move(*made);
        ui_->set_viewport(x_, y_, width_, height_);
    }
    auto d = ui_->open(path);
    if (!d) return std::unexpected(d.error());
    return d->id;
}

void WorldUi::close(std::uint32_t document) {
    if (ui_) ui_->close({document});
}
bool WorldUi::is_open(std::uint32_t document) const { return ui_ && ui_->is_open({document}); }
void WorldUi::set_visible(std::uint32_t document, bool visible) {
    if (ui_) ui_->set_visible({document}, visible);
}
bool WorldUi::visible(std::uint32_t document) const { return ui_ && ui_->visible({document}); }
std::optional<std::uint32_t> WorldUi::find(std::string_view path) const {
    if (!ui_) return std::nullopt;
    const auto d = ui_->find(path);
    return d ? std::optional<std::uint32_t>(d->id) : std::nullopt;
}
bool WorldUi::set_text(std::uint32_t document, std::string_view element, std::string_view text) {
    return ui_ && ui_->set_text({document}, element, text);
}
bool WorldUi::set_rml(std::uint32_t document, std::string_view element, std::string_view rml) {
    return ui_ && ui_->set_rml({document}, element, rml);
}
bool WorldUi::set_class(std::uint32_t document, std::string_view element, std::string_view name, bool on) {
    return ui_ && ui_->set_class({document}, element, name, on);
}
bool WorldUi::set_attribute(std::uint32_t document, std::string_view element, std::string_view name, const asset::Json& value) {
    return ui_ && ui_->set_attribute({document}, element, name, value);
}
bool WorldUi::set_style(std::uint32_t document, std::string_view element, std::string_view property, std::string_view value) {
    return ui_ && ui_->set_style({document}, element, property, value);
}
std::optional<asset::Json> WorldUi::value(std::uint32_t document, std::string_view element) const {
    if (!ui_) return std::nullopt;
    return ui_->value({document}, element);
}
bool WorldUi::set_value(std::uint32_t document, std::string_view element, const asset::Json& value) {
    return ui_ && ui_->set_value({document}, element, value);
}
bool WorldUi::focus(std::uint32_t document, std::string_view element) { return ui_ && ui_->focus({document}, element); }
bool WorldUi::click(std::uint32_t document, std::string_view element) { return ui_ && ui_->click({document}, element); }

} // namespace noor::ui
