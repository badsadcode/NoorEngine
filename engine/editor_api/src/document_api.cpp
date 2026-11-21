// C ABI for documents (ADR-025). Thin wrappers: parse/serialize JSON, turn errors into
// noor_last_error(), never let a C++ exception cross the DLL boundary.

#include "noor/asset/environment_asset.h"
#include "noor/asset/project_template.h"
#include "noor/asset/water_asset.h"
#include "noor/fx/presets.h"
#include "noor_editor_api.h"

#include "api_internal.h"
#include "noor/procgraph/procgraph.h"
#include "noor/foundation/log.h"
#include "document.h"
#include "noor/scripting/csharp_scripts.h"

#include <exception>
#include <memory>
#include <string>

struct noor_doc {
    std::unique_ptr<noor::editor::Document> doc;
};

namespace {
thread_local std::string t_result;

const char* give(const noor::asset::Json& json) {
    t_result = json.dump();
    return t_result.c_str();
}

int32_t fail_with(const noor::Error& e) {
    noor::editor_api::set_last_error(e.describe());
    return 0;
}

// Runs `fn`, converting any escaping exception into an error result.
template <class Fn, class R>
R guarded(R on_error, Fn&& fn) {
    try {
        return fn();
    } catch (const std::exception& e) {
        noor::editor_api::set_last_error(std::string("internal error: ") + e.what());
    } catch (...) {
        noor::editor_api::set_last_error("internal error");
    }
    return on_error;
}
} // namespace

std::optional<bool> noor_doc_tick_internal(noor_doc* doc, double seconds, const noor::runtime::InputState& input) {
    if (!doc) return std::nullopt;
    try {
        doc->doc->tick(seconds, input);
        return doc->doc->take_mouse_capture_request();
    } catch (const std::exception& e) {
        NOOR_LOG_ERROR("Editor", "Play frame failed: {}", e.what());
        return std::nullopt;
    }
}

extern "C" {

NOOR_API noor_doc* noor_doc_open_project(const char* project_path) {
    return guarded<>(static_cast<noor_doc*>(nullptr), [&]() -> noor_doc* {
        auto d = noor::editor::Document::open(project_path ? project_path : "");
        if (!d) {
            fail_with(d.error());
            return nullptr;
        }
        return new noor_doc{std::move(*d)};
    });
}

NOOR_API noor_doc* noor_doc_create_project(const char* folder, const char* name) {
    return guarded<>(static_cast<noor_doc*>(nullptr), [&]() -> noor_doc* {
        auto d = noor::editor::Document::create(folder ? folder : "", name ? name : "");
        if (!d) {
            fail_with(d.error());
            return nullptr;
        }
        return new noor_doc{std::move(*d)};
    });
}

NOOR_API const char* noor_templates(const char* dir) {
    return guarded<>(static_cast<const char*>(nullptr), [&] {
        noor::asset::Json list = noor::asset::Json::array();
        for (const auto& t : noor::asset::list_templates(dir ? dir : ""))
            list.push_back({{"id", t.id}, {"name", t.name}, {"description", t.description}, {"folder", t.folder.generic_string()}});
        return give(list);
    });
}

NOOR_API noor_doc* noor_doc_create_project_from_template(const char* folder, const char* name, const char* template_folder) {
    return guarded<>(static_cast<noor_doc*>(nullptr), [&]() -> noor_doc* {
        auto d = noor::editor::Document::create(folder ? folder : "", name ? name : "", template_folder ? template_folder : "");
        if (!d) {
            fail_with(d.error());
            return nullptr;
        }
        return new noor_doc{std::move(*d)};
    });
}

NOOR_API void noor_doc_close(noor_doc* doc) { delete doc; }

NOOR_API const char* noor_doc_project_info(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&] { return give(doc->doc->project_info()); });
}

NOOR_API const char* noor_doc_assets(noor_doc* doc, int32_t rescan) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto a = doc->doc->assets(rescan != 0);
        if (!a) {
            fail_with(a.error());
            return nullptr;
        }
        return give(*a);
    });
}

NOOR_API const char* noor_doc_scatter_edit(noor_doc* doc, const char* command) {
    if (!doc || !command) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto json = noor::asset::parse_json(command, "scatter command");
        if (!json) {
            fail_with(json.error());
            return nullptr;
        }
        auto r = doc->doc->scatter_edit(*json);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_doc_asset_details(noor_doc* doc, const char* asset_id) {
    if (!doc || !asset_id) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto id = noor::Guid::parse(asset_id);
        if (!id) {
            fail_with(id.error());
            return nullptr;
        }
        auto d = doc->doc->asset_details(*id);
        if (!d) {
            fail_with(d.error());
            return nullptr;
        }
        return give(*d);
    });
}

NOOR_API const char* noor_doc_export_csg(noor_doc* doc, const char* model_id, const char* glb_path) {
    if (!doc || !model_id || !glb_path) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        const auto id = noor::Guid::parse(model_id);
        if (!id) {
            fail_with(id.error());
            return nullptr;
        }
        auto r = doc->doc->export_csg(*id, std::filesystem::path(reinterpret_cast<const char8_t*>(glb_path)));
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_doc_csg(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* { return give(doc->doc->csg_info()); });
}

NOOR_API const char* noor_doc_texture(noor_doc* doc, const char* texture_path) {
    if (!doc || !texture_path) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto r = doc->doc->texture(texture_path);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API int32_t noor_doc_set_texture_settings(noor_doc* doc, const char* texture_path, const char* settings_json) {
    if (!doc || !texture_path || !settings_json) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto parsed = noor::asset::parse_json(settings_json, "texture settings");
        if (!parsed) return fail_with(parsed.error());
        auto s = doc->doc->set_texture_settings(texture_path, *parsed);
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API int32_t noor_doc_set_preview(noor_doc* doc, const char* json) {
    if (!doc || !json) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto parsed = noor::asset::parse_json(json, "preview");
        if (!parsed) return fail_with(parsed.error());
        auto s = doc->doc->set_preview(*parsed);
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API const char* noor_doc_make_material(noor_doc* doc, const char* texture_path) {
    if (!doc || !texture_path) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto r = doc->doc->make_material(texture_path);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_environment_presets(void) {
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        noor::asset::Json list = noor::asset::Json::array();
        for (const auto& p : noor::asset::environment_presets())
            list.push_back({{"name", p.name}, {"description", p.description}, {"environment", p.environment}});
        return give(list);
    });
}

NOOR_API const char* noor_water_presets(void) {
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* { return give(noor::asset::water_presets()); });
}

NOOR_API const char* noor_effect_presets(void) {
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        noor::asset::Json list = noor::asset::Json::array();
        for (const auto& p : noor::fx::effect_presets()) list.push_back({{"name", p.name}, {"description", p.description}, {"emitter", p.emitter}});
        return give(list);
    });
}

NOOR_API const char* noor_doc_effect(noor_doc* doc, const char* command) {
    if (!doc || !command) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto json = noor::asset::parse_json(command, "effect command");
        if (!json) {
            fail_with(json.error());
            return nullptr;
        }
        auto r = doc->doc->effect(*json);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_doc_water(noor_doc* doc, const char* command) {
    if (!doc || !command) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto json = noor::asset::parse_json(command, "water command");
        if (!json) {
            fail_with(json.error());
            return nullptr;
        }
        auto r = doc->doc->water(*json);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_doc_tilemap(noor_doc* doc, const char* command) {
    if (!doc || !command) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto json = noor::asset::parse_json(command, "tilemap command");
        if (!json) {
            fail_with(json.error());
            return nullptr;
        }
        auto r = doc->doc->tilemap(*json);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_doc_sprite(noor_doc* doc, const char* command) {
    if (!doc || !command) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto json = noor::asset::parse_json(command, "sprite command");
        if (!json) {
            fail_with(json.error());
            return nullptr;
        }
        auto r = doc->doc->sprite(*json);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_doc_animation(noor_doc* doc, const char* command) {
    if (!doc || !command) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto json = noor::asset::parse_json(command, "animation command");
        if (!json) {
            fail_with(json.error());
            return nullptr;
        }
        auto r = doc->doc->animation(*json);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_doc_environment(noor_doc* doc, const char* command) {
    if (!doc || !command) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto json = noor::asset::parse_json(command, "environment command");
        if (!json) {
            fail_with(json.error());
            return nullptr;
        }
        auto r = doc->doc->environment(*json);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API const char* noor_doc_scenes(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&] { return give(doc->doc->scenes()); });
}

NOOR_API int32_t noor_doc_open_scene(noor_doc* doc, const char* relative_path) {
    if (!doc || !relative_path) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->open_scene(relative_path);
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API int32_t noor_doc_new_scene(noor_doc* doc, const char* relative_path, const char* name) {
    if (!doc || !relative_path) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->new_scene(relative_path, name ? name : "Untitled");
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API int32_t noor_doc_save_scene(noor_doc* doc) {
    if (!doc) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->save_scene();
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API const char* noor_doc_scene(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&] { return give(doc->doc->scene_snapshot()); });
}

NOOR_API const char* noor_doc_sculpt(noor_doc* doc, const char* command_json) {
    if (!doc || !command_json) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto cmd = noor::asset::parse_json(command_json, "sculpt");
        if (!cmd) {
            fail_with(cmd.error());
            return nullptr;
        }
        auto r = doc->doc->sculpt(*cmd);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

NOOR_API int32_t noor_doc_terrain_height(noor_doc* doc, const char* terrain_id, float x, float z, float* out_height) {
    if (!doc || !terrain_id || !out_height) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        const auto id = noor::Guid::parse(terrain_id);
        if (!id) return fail_with(id.error());
        auto h = doc->doc->terrain_height(*id, x, z);
        if (!h) return fail_with(h.error());
        *out_height = *h;
        return 1;
    });
}

NOOR_API int32_t noor_doc_generate_start(noor_doc* doc, const char* graph_path, const char* out_dir) {
    if (!doc || !graph_path) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->generate_start(graph_path, out_dir ? std::filesystem::path(reinterpret_cast<const char8_t*>(out_dir)) : std::filesystem::path{});
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API const char* noor_doc_generate_status(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* { return give(doc->doc->generate_status()); });
}

NOOR_API void noor_doc_generate_cancel(noor_doc* doc) {
    if (doc) doc->doc->generate_cancel();
}

NOOR_API const char* noor_doc_generate_preview(noor_doc* doc, const char* node, const char* pin, int32_t size) {
    if (!doc || !node || !pin) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto r = doc->doc->generate_preview(node, pin, size);
        if (!r) {
            fail_with(r.error());
            return nullptr;
        }
        return give(*r);
    });
}

// ---- Procedural graphs without a document (Milestone 7, part 4): what the graph editor needs ----

NOOR_API const char* noor_graph_node_types(void) {
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        using namespace noor::procgraph;
        noor::asset::Json list = noor::asset::Json::array();
        auto pins = [](const std::vector<PinSpec>& specs) {
            noor::asset::Json out = noor::asset::Json::array();
            for (const auto& p : specs)
                out.push_back({{"name", p.name}, {"type", std::string(type_name(p.type))}, {"unit", std::string(unit_name(p.unit))}, {"optional", p.optional}});
            return out;
        };
        for (const auto& [name, t] : Registry::builtin().all()) {
            noor::asset::Json params = noor::asset::Json::array();
            for (const auto& p : t.params) {
                const char* kind = p.kind == ParamSpec::Kind::Number ? "number" : p.kind == ParamSpec::Kind::Integer ? "integer" : p.kind == ParamSpec::Kind::Choice ? "choice"
                                 : p.kind == ParamSpec::Kind::Text ? "text" : "flag";
                noor::asset::Json pj{{"name", p.name}, {"kind", kind}, {"default", p.fallback}, {"help", p.help}};
                if (p.kind == ParamSpec::Kind::Number || p.kind == ParamSpec::Kind::Integer) pj["min"] = p.min, pj["max"] = p.max;
                if (p.kind == ParamSpec::Kind::Choice) pj["choices"] = p.choices;
                params.push_back(std::move(pj));
            }
            list.push_back({{"name", name}, {"version", t.version}, {"help", t.help}, {"inputs", pins(t.inputs)}, {"outputs", pins(t.outputs)},
                            {"params", std::move(params)}, {"tiled", static_cast<bool>(t.run_tile)}});
        }
        return give(list);
    });
}

NOOR_API const char* noor_graph_check(const char* graph_json) {
    if (!graph_json) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&]() -> const char* {
        auto json = noor::asset::parse_json(graph_json, "graph");
        if (!json) {
            fail_with(json.error());
            return nullptr;
        }
        auto g = noor::procgraph::Graph::from_json(*json);
        if (!g) {
            fail_with(g.error());
            return nullptr;
        }
        noor::asset::Json list = noor::asset::Json::array();
        for (const auto& i : noor::procgraph::validate(*g, noor::procgraph::Registry::builtin()))
            list.push_back({{"node", i.node}, {"pin", i.pin}, {"message", i.message}});
        return give(noor::asset::Json{{"issues", std::move(list)}, {"text", noor::asset::to_text(g->to_json())}});
    });
}

NOOR_API int32_t noor_doc_execute(noor_doc* doc, const char* command_json) {
    if (!doc || !command_json) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto cmd = noor::asset::parse_json(command_json, "command");
        if (!cmd) return fail_with(cmd.error());
        auto s = doc->doc->execute(*cmd);
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API int32_t noor_doc_undo(noor_doc* doc) {
    if (!doc) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t { return doc->doc->undo() ? 1 : 0; });
}

NOOR_API int32_t noor_doc_redo(noor_doc* doc) {
    if (!doc) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t { return doc->doc->redo() ? 1 : 0; });
}

NOOR_API uint64_t noor_doc_revision(noor_doc* doc) { return doc ? doc->doc->revision() : 0; }

NOOR_API int32_t noor_doc_play(noor_doc* doc) {
    if (!doc) return 0;
    return guarded<>(int32_t{0}, [&]() -> int32_t {
        auto s = doc->doc->play();
        return s ? 1 : fail_with(s.error());
    });
}

NOOR_API void noor_doc_stop(noor_doc* doc) {
    if (!doc) return;
    guarded<>(0, [&] {
        doc->doc->stop();
        return 0;
    });
}

NOOR_API void noor_doc_tick(noor_doc* doc, double seconds, noor_viewport* input_viewport) {
    if (!doc) return;
    guarded<>(0, [&] {
        doc->doc->tick(seconds, noor_viewport_game_input(input_viewport));
        if (const auto want = doc->doc->take_mouse_capture_request(); want && input_viewport)
            noor_viewport_set_mouse_capture(input_viewport, *want ? 1 : 0);
        return 0;
    });
}

NOOR_API const void* noor_rt_get_api(void) { return noor::scripting::rt_api(); }

NOOR_API void noor_doc_set_script_host(noor_doc* doc, const void* host) {
    if (doc) doc->doc->set_script_host(host);
}

NOOR_API void noor_doc_set_audio_device(noor_doc* doc, int32_t enabled) {
    if (doc) doc->doc->set_audio_device(enabled != 0);
}

NOOR_API const char* noor_doc_poll_files(noor_doc* doc) {
    if (!doc) return nullptr;
    return guarded<>(static_cast<const char*>(nullptr), [&] { return give(doc->doc->poll_files()); });
}

} // extern "C"

// For viewport.cpp: the channel a document publishes to.
std::shared_ptr<noor::editor::SnapshotChannel> noor_doc_channel(noor_doc* doc) {
    return doc ? doc->doc->channel() : nullptr;
}

// For viewport.cpp: the material preview's channel.
std::shared_ptr<noor::editor::SnapshotChannel> noor_doc_preview_channel(noor_doc* doc) {
    return doc ? doc->doc->preview_channel() : nullptr;
}
