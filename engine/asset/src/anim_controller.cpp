#include "noor/asset/anim_controller.h"

#include "noor/asset/pose_blend.h"
#include "noor/foundation/file_io.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace noor::asset {
namespace {

std::string text(const Json& j, const char* key) { return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string(); }

// "from" a number, or a parameter's name (which must exist).
Result<AnimValue> anim_value(const Json& j, const AnimController& c, float fallback, std::string_view where) {
    AnimValue v{fallback, {}};
    if (j.is_null()) return v;
    if (j.is_number()) {
        v.value = j.get<float>();
        return v;
    }
    if (j.is_string()) {
        v.parameter = j.get<std::string>();
        if (!c.parameter(v.parameter)) return fail(ErrorCode::ParseError, std::format("{}: there's no parameter '{}'", where, v.parameter));
        return v;
    }
    return fail(ErrorCode::ParseError, std::format("{}: expected a number or a parameter's name", where));
}

int state_index(const AnimLayer& layer, std::string_view name) {
    for (std::size_t i = 0; i < layer.states.size(); ++i)
        if (layer.states[i].name == name) return static_cast<int>(i);
    return -1;
}

double frac_time(double phase, double duration, bool loop) {
    if (duration <= 0.0) return 0.0;
    if (!loop) return std::clamp(phase, 0.0, 1.0) * duration;
    return (phase - std::floor(phase)) * duration;
}

} // namespace

const AnimParameter* AnimController::parameter(std::string_view name) const {
    for (const auto& p : parameters)
        if (p.name == name) return &p;
    return nullptr;
}

Result<AnimController> parse_anim_controller(const Json& json, std::string_view label) {
    auto bad = [&](std::string message) { return fail(ErrorCode::ParseError, std::format("{}: {}", label, message)); };
    if (!json.is_object() || !json.contains("noor_anim")) return bad("not an animation controller (no \"noor_anim\")");
    if (!json["noor_anim"].is_number_integer() || json["noor_anim"].get<int>() != 1)
        return bad("written by a newer NoorEngine (\"noor_anim\" isn't 1)");
    AnimController c;
    if (json.contains("parameters")) {
        if (!json["parameters"].is_object()) return bad("\"parameters\" should be an object of names");
        for (const auto& [name, p] : json["parameters"].items()) {
            AnimParameter param;
            param.name = name;
            const std::string type = p.is_object() ? text(p, "type") : std::string("float");
            if (type == "float" || type.empty()) param.type = AnimParameter::Type::Float;
            else if (type == "bool") param.type = AnimParameter::Type::Bool;
            else if (type == "trigger") param.type = AnimParameter::Type::Trigger;
            else return bad(std::format("parameter '{}': type '{}' isn't float, bool or trigger", name, type));
            if (p.is_object() && p.contains("default")) {
                const Json& d = p["default"];
                param.initial = d.is_boolean() ? (d.get<bool>() ? 1.0f : 0.0f) : d.is_number() ? d.get<float>() : 0.0f;
            }
            c.parameters.push_back(std::move(param));
        }
    }
    if (!json.contains("layers") || !json["layers"].is_array() || json["layers"].empty()) return bad("no \"layers\"");
    for (const auto& lj : json["layers"]) {
        AnimLayer layer;
        layer.name = lj.is_object() ? text(lj, "name") : std::string();
        if (layer.name.empty()) layer.name = std::format("layer {}", c.layers.size() + 1);
        const std::string where = std::format("layer '{}'", layer.name);
        if (!lj.is_object() || !lj.contains("states") || !lj["states"].is_array() || lj["states"].empty())
            return bad(where + ": no \"states\"");
        const std::string mode = text(lj, "mode");
        if (mode == "additive") layer.mode = AnimLayer::Mode::Additive;
        else if (!mode.empty() && mode != "override") return bad(std::format("{}: mode '{}' isn't override or additive", where, mode));
        auto weight = anim_value(lj.value("weight", Json()), c, 1.0f, where + ", weight");
        if (!weight) return bad(weight.error().message);
        layer.weight = *weight;
        if (lj.contains("mask")) {
            if (!lj["mask"].is_array()) return bad(where + ": \"mask\" should be a list of bone names");
            for (const auto& m : lj["mask"])
                if (m.is_string()) layer.mask.push_back(m.get<std::string>());
        }
        for (const auto& sj : lj["states"]) {
            AnimState s;
            s.name = sj.is_object() ? text(sj, "name") : std::string();
            if (s.name.empty()) return bad(where + ": a state without a \"name\"");
            if (state_index(layer, s.name) >= 0) return bad(std::format("{}: two states called '{}'", where, s.name));
            const std::string swhere = std::format("{}, state '{}'", where, s.name);
            s.clip = text(sj, "clip");
            if (sj.contains("blend")) {
                const Json& b = sj["blend"];
                if (!s.clip.empty()) return bad(swhere + ": a \"clip\" and a \"blend\" (one or the other)");
                if (!b.is_object() || !b.contains("samples") || !b["samples"].is_array() || b["samples"].empty())
                    return bad(swhere + ": a blend needs \"samples\"");
                s.x = text(b, "x");
                s.y = text(b, "y");
                if (s.x.empty() || !c.parameter(s.x)) return bad(std::format("{}: the blend's \"x\" '{}' isn't a parameter", swhere, s.x));
                if (!s.y.empty() && !c.parameter(s.y)) return bad(std::format("{}: the blend's \"y\" '{}' isn't a parameter", swhere, s.y));
                for (const auto& smp : b["samples"]) {
                    AnimState::Sample sample;
                    sample.clip = smp.is_object() ? text(smp, "clip") : std::string();
                    if (sample.clip.empty()) return bad(swhere + ": a sample without a \"clip\"");
                    sample.at = {smp.value("x", 0.0f), smp.value("y", 0.0f)};
                    s.samples.push_back(std::move(sample));
                }
            }
            s.loop = sj.value("loop", true);
            if (sj.contains("events")) {
                if (!sj["events"].is_array()) return bad(swhere + ": \"events\" should be a list");
                for (const auto& ev : sj["events"]) {
                    const std::string name = ev.is_object() ? text(ev, "name") : std::string();
                    if (name.empty()) return bad(swhere + ": an event without a \"name\"");
                    const float at = ev.value("at", 0.0f);
                    if (at < 0.0f || at > 1.0f) return bad(std::format("{}: event '{}' is at {} (0 to 1: how far through the state)", swhere, name, at));
                    s.events.push_back({at, name});
                }
            }
            auto speed = anim_value(sj.value("speed", Json()), c, 1.0f, swhere + ", speed");
            if (!speed) return bad(speed.error().message);
            s.speed = *speed;
            layer.states.push_back(std::move(s));
        }
        if (lj.contains("entry")) {
            layer.entry = state_index(layer, text(lj, "entry"));
            if (layer.entry < 0) return bad(std::format("{}: the entry state '{}' isn't one of its states", where, text(lj, "entry")));
        }
        if (lj.contains("transitions")) {
            if (!lj["transitions"].is_array()) return bad(where + ": \"transitions\" should be a list");
            for (const auto& tj : lj["transitions"]) {
                AnimTransition t;
                const std::string from = tj.is_object() ? text(tj, "from") : std::string(), to = tj.is_object() ? text(tj, "to") : std::string();
                t.from = from == "*" ? -1 : state_index(layer, from);
                t.to = state_index(layer, to);
                const std::string twhere = std::format("{}, transition '{}' -> '{}'", where, from, to);
                if (from != "*" && t.from < 0) return bad(twhere + ": no such \"from\" state (or \"*\" for any)");
                if (t.to < 0) return bad(twhere + ": no such \"to\" state");
                t.fade = std::max(0.0f, tj.value("fade", 0.2f));
                if (tj.contains("exit_time") && tj["exit_time"].is_number()) t.exit_time = tj["exit_time"].get<float>();
                if (tj.contains("when")) {
                    if (!tj["when"].is_array()) return bad(twhere + ": \"when\" should be a list of conditions");
                    for (const auto& cj : tj["when"]) {
                        AnimCondition cond;
                        cond.parameter = cj.is_object() ? text(cj, "parameter") : std::string();
                        if (!c.parameter(cond.parameter)) return bad(std::format("{}: there's no parameter '{}'", twhere, cond.parameter));
                        auto number = [](const Json& v) { return v.is_boolean() ? (v.get<bool>() ? 1.0f : 0.0f) : v.is_number() ? v.get<float>() : 0.0f; };
                        if (cj.contains("greater")) cond.op = AnimCondition::Op::Greater, cond.value = number(cj["greater"]);
                        else if (cj.contains("less")) cond.op = AnimCondition::Op::Less, cond.value = number(cj["less"]);
                        else if (cj.contains("equals")) cond.op = AnimCondition::Op::Equals, cond.value = number(cj["equals"]);
                        else if (cj.contains("not_equals")) cond.op = AnimCondition::Op::NotEquals, cond.value = number(cj["not_equals"]);
                        t.when.push_back(std::move(cond));
                    }
                }
                layer.transitions.push_back(std::move(t));
            }
        }
        c.layers.push_back(std::move(layer));
    }
    return c;
}

Result<AnimController> load_anim_controller(const std::filesystem::path& path) {
    auto file = fs::read_file(path, 4 << 20);
    if (!file) return std::unexpected(file.error());
    auto json = parse_json(*file, path.generic_string());
    if (!json) return std::unexpected(json.error());
    return parse_anim_controller(*json, path.filename().string());
}

// ---- The player ----

AnimControllerPlayer::AnimControllerPlayer(const AnimController& controller, const ImportedModel& model,
                                           const std::unordered_map<std::string, int>* aliases)
    : controller_(&controller), model_(&model), rest_(rest_pose(model)) {
    const std::size_t nodes = model.nodes.size();
    for (const auto& layer : controller.layers) {
        auto& states = bound_.emplace_back();
        for (const auto& s : layer.states) {
            Bound b;
            auto bind = [&](const std::string& clip) {
                const ImportedAnimation* a = find_animation(model, clip);
                if (!a) warnings_.push_back(std::format("layer '{}', state '{}': the model has no clip '{}'", layer.name, s.name, clip));
                b.clips.push_back(a);
            };
            if (!s.clip.empty()) bind(s.clip);
            for (const auto& sample : s.samples) bind(sample.clip);
            states.push_back(std::move(b));
        }
        // The mask: the named bones and everything below them (nodes come parents first). None, or the base: all.
        std::vector<float> mask(nodes, 1.0f);
        if (!layer.mask.empty() && masks_.size() > 0) {
            std::fill(mask.begin(), mask.end(), 0.0f);
            for (const auto& name : layer.mask) {
                const auto it = std::ranges::find_if(model.nodes, [&](const ImportedNode& n) { return n.name == name; });
                if (it != model.nodes.end()) {
                    mask[static_cast<std::size_t>(it - model.nodes.begin())] = 1.0f;
                } else {
                    int alias = -1;
                    if (aliases)
                        if (const auto a = aliases->find(name); a != aliases->end()) alias = a->second;
                    if (alias >= 0 && static_cast<std::size_t>(alias) < nodes) mask[static_cast<std::size_t>(alias)] = 1.0f;
                    else warnings_.push_back(std::format("layer '{}': the mask's bone '{}' isn't in the model", layer.name, name));
                }
            }
            for (std::size_t i = 0; i < nodes; ++i)
                if (model.nodes[i].parent >= 0 && mask[static_cast<std::size_t>(model.nodes[i].parent)] > 0.0f) mask[i] = 1.0f;
        }
        masks_.push_back(std::move(mask));
        layers_.push_back({layer.entry, 0.0, -1, 0.0, 0.0, 0.0});
    }
}

float AnimControllerPlayer::value(const AnimValue& v, const AnimParameters& params) const {
    if (v.parameter.empty()) return v.value;
    if (const auto it = params.find(v.parameter); it != params.end()) return it->second;
    const auto* p = controller_->parameter(v.parameter);
    return p ? p->initial : 0.0f;
}

std::vector<float> AnimControllerPlayer::sample_weights(const AnimState& s, const AnimParameters& params) const {
    const float x = value({0.0f, s.x}, params);
    if (s.y.empty()) {
        std::vector<float> at;
        for (const auto& smp : s.samples) at.push_back(smp.at[0]);
        return blend_weights_1d(at, x);
    }
    std::vector<std::array<float, 2>> at;
    for (const auto& smp : s.samples) at.push_back(smp.at);
    return blend_weights_2d(at, {x, value({0.0f, s.y}, params)});
}

double AnimControllerPlayer::length(int layer, int state, const AnimParameters& params) const {
    const AnimState& s = controller_->layers[static_cast<std::size_t>(layer)].states[static_cast<std::size_t>(state)];
    const Bound& b = bound_[static_cast<std::size_t>(layer)][static_cast<std::size_t>(state)];
    double len = 0.0;
    if (!s.samples.empty()) {
        const auto w = sample_weights(s, params);
        for (std::size_t k = 0; k < w.size(); ++k)
            if (b.clips[k]) len += static_cast<double>(w[k]) * b.clips[k]->duration;
    } else if (!b.clips.empty() && b.clips[0]) {
        len = b.clips[0]->duration;
    }
    return len > 1e-6 ? len : 1.0;  // empty states (and single-key clips) count as a second long
}

math::Vec3 AnimControllerPlayer::root_delta_of(int layer, int state, double p0, double p1, const AnimParameters& params) const {
    const AnimState& s = controller_->layers[static_cast<std::size_t>(layer)].states[static_cast<std::size_t>(state)];
    const Bound& b = bound_[static_cast<std::size_t>(layer)][static_cast<std::size_t>(state)];
    const NodePose& rest = rest_[static_cast<std::size_t>(root_)];
    if (s.samples.empty()) {
        if (b.clips.empty() || !b.clips[0]) return {0, 0, 0};
        const double d = b.clips[0]->duration;
        return root_delta(*b.clips[0], root_, rest, p0 * d, p1 * d, s.loop);
    }
    const auto w = sample_weights(s, params);
    math::Vec3 sum{0, 0, 0};
    for (std::size_t k = 0; k < w.size(); ++k) {
        if (w[k] <= 0.0f || !b.clips[k]) continue;
        const double d = b.clips[k]->duration;
        const math::Vec3 m = root_delta(*b.clips[k], root_, rest, p0 * d, p1 * d, s.loop);
        sum = {sum.x + m.x * w[k], sum.y + m.y * w[k], sum.z + m.z * w[k]};
    }
    return sum;
}

void AnimControllerPlayer::step(double dt, const AnimParameters& params, std::vector<std::string>* consumed, std::vector<Ended>* ended,
                                std::vector<Ended>* events) {
    moved_ = {0, 0, 0};
    for (std::size_t l = 0; l < layers_.size(); ++l) {
        const AnimLayer& layer = controller_->layers[l];
        Running& r = layers_[l];
        // 1. Transitions (not mid-fade): the first that holds fires.
        if (r.from < 0) {
            for (const auto& t : layer.transitions) {
                if ((t.from >= 0 && t.from != r.state) || t.to == r.state) continue;
                if (t.exit_time && r.phase + 1e-9 < *t.exit_time) continue;
                const bool holds = std::ranges::all_of(t.when, [&](const AnimCondition& c) {
                    const float v = value({0.0f, c.parameter}, params);
                    switch (c.op) {
                    case AnimCondition::Op::True: return v != 0.0f;
                    case AnimCondition::Op::Greater: return v > c.value;
                    case AnimCondition::Op::Less: return v < c.value;
                    case AnimCondition::Op::Equals: return v == c.value;
                    case AnimCondition::Op::NotEquals: return v != c.value;
                    }
                    return false;
                });
                if (!holds) continue;
                if (consumed)
                    for (const auto& c : t.when)
                        if (const auto* p = controller_->parameter(c.parameter); p && p->type == AnimParameter::Type::Trigger &&
                                                                                 std::ranges::find(*consumed, c.parameter) == consumed->end())
                            consumed->push_back(c.parameter);
                r.from = t.fade > 0.0f ? r.state : -1;
                r.from_phase = r.phase;
                r.state = t.to;
                r.phase = 0.0;
                r.fade = t.fade;
                r.faded = 0.0;
                break;
            }
        }
        // 2. Time moves on.
        const bool root = l == 0 && root_ >= 0;
        auto advance = [&](int state, double& phase, bool report) {
            const AnimState& s = layer.states[static_cast<std::size_t>(state)];
            const double before = phase;
            phase += dt * value(s.speed, params) / length(static_cast<int>(l), state, params);
            if (report && events)
                for (const auto& ev : s.events)
                    for (int n = crossings(before, phase, ev.at, 1.0, s.loop); n > 0; --n) events->push_back({static_cast<int>(l), s.name, ev.name});
            if (report && ended && !s.loop && before < 1.0 - 1e-9 && phase >= 1.0 - 1e-9) ended->push_back({static_cast<int>(l), s.name, {}});
            return root ? root_delta_of(static_cast<int>(l), state, before, phase, params) : math::Vec3{0, 0, 0};
        };
        const math::Vec3 now = advance(r.state, r.phase, true);
        moved_ = root ? now : moved_;
        if (r.from >= 0) {
            const math::Vec3 old = advance(r.from, r.from_phase, false);
            r.faded += dt;
            if (root) {  // both moving, weighted by how far the fade is
                const float w = static_cast<float>(std::clamp(r.faded / r.fade, 0.0, 1.0));
                moved_ = {old.x + (now.x - old.x) * w, old.y + (now.y - old.y) * w, old.z + (now.z - old.z) * w};
            }
            if (r.faded >= r.fade - 1e-9) r.from = -1;
        }
    }
}

bool AnimControllerPlayer::play(std::string_view state, float fade, std::string_view layer) {
    std::size_t l = 0;
    for (std::size_t i = 0; i < controller_->layers.size(); ++i)
        if (!layer.empty() && controller_->layers[i].name == layer) l = i;
    const int s = state_index(controller_->layers[l], state);
    if (s < 0) return false;
    Running& r = layers_[l];
    r.from = fade > 0.0f ? r.state : -1;
    r.from_phase = r.phase;
    r.state = s;
    r.phase = 0.0;
    r.fade = fade;
    r.faded = 0.0;
    return true;
}

void AnimControllerPlayer::sample_state(int layer, int state, double phase, const AnimParameters& params, std::span<NodePose> out) {
    const AnimState& s = controller_->layers[static_cast<std::size_t>(layer)].states[static_cast<std::size_t>(state)];
    const Bound& b = bound_[static_cast<std::size_t>(layer)][static_cast<std::size_t>(state)];
    if (s.samples.empty()) {
        if (!b.clips.empty() && b.clips[0]) sample(*b.clips[0], frac_time(phase, b.clips[0]->duration, s.loop), out);
        return;  // an empty state leaves `out` as it is
    }
    const auto w = sample_weights(s, params);
    if (samples_.size() < w.size()) samples_.resize(w.size());
    std::vector<std::span<const NodePose>> poses;
    for (std::size_t k = 0; k < w.size(); ++k) {
        samples_[k].assign(out.begin(), out.end());
        if (w[k] > 0.0f && b.clips[k]) sample(*b.clips[k], frac_time(phase, b.clips[k]->duration, s.loop), samples_[k]);
        poses.emplace_back(samples_[k]);
    }
    mix(poses, w, out);
}

void AnimControllerPlayer::pose(const AnimParameters& params, std::span<NodePose> out) {
    std::copy(rest_.begin(), rest_.end(), out.begin());
    std::vector<float> weights;
    for (std::size_t l = 0; l < layers_.size(); ++l) {
        const AnimLayer& layer = controller_->layers[l];
        const Running& r = layers_[l];
        const int li = static_cast<int>(l);
        const float fade = r.from >= 0 && r.fade > 0.0 ? static_cast<float>(std::clamp(r.faded / r.fade, 0.0, 1.0)) : 1.0f;
        if (l > 0 && layer.mode == AnimLayer::Mode::Additive) {
            // How the state differs from its own first moment (from the rest pose, so untouched bones add nothing).
            auto delta = [&](int state, double phase, std::vector<NodePose>& d) {
                d = rest_;
                ref_ = rest_;
                sample_state(li, state, phase, params, d);
                sample_state(li, state, 0.0, params, ref_);
                for (std::size_t i = 0; i < d.size(); ++i) d[i] = additive_delta(ref_[i], d[i]);
            };
            delta(r.state, r.phase, a_);
            if (r.from >= 0) {
                delta(r.from, r.from_phase, b_);
                blend(b_, a_, fade, a_);
            }
        } else {
            // Override (and the base): the state over what's below it.
            a_.assign(out.begin(), out.end());
            sample_state(li, r.state, r.phase, params, a_);
            if (r.from >= 0) {
                b_.assign(out.begin(), out.end());
                sample_state(li, r.from, r.from_phase, params, b_);
                blend(b_, a_, fade, a_);
            }
        }
        if (l == 0) {
            std::copy(a_.begin(), a_.end(), out.begin());
            continue;
        }
        const float w = std::clamp(value(layer.weight, params), 0.0f, 1.0f);
        weights.assign(masks_[l].begin(), masks_[l].end());
        for (float& m : weights) m *= w;
        if (layer.mode == AnimLayer::Mode::Additive) apply_additive(out, a_, weights);
        else blend(std::span<const NodePose>(out.data(), out.size()), a_, weights, out);
    }
    // Root motion: the root stays where it rests horizontally; its movement went to the character (root_motion()).
    if (root_ >= 0 && static_cast<std::size_t>(root_) < out.size()) {
        out[static_cast<std::size_t>(root_)].translation.x = rest_[static_cast<std::size_t>(root_)].translation.x;
        out[static_cast<std::size_t>(root_)].translation.z = rest_[static_cast<std::size_t>(root_)].translation.z;
    }
}

std::string_view AnimControllerPlayer::state(std::size_t layer) const {
    if (layer >= layers_.size()) return {};
    return controller_->layers[layer].states[static_cast<std::size_t>(layers_[layer].state)].name;
}

double AnimControllerPlayer::phase(std::size_t layer) const { return layer < layers_.size() ? layers_[layer].phase : 0.0; }

float AnimControllerPlayer::fade_weight(std::size_t layer) const {
    if (layer >= layers_.size()) return 1.0f;
    const Running& r = layers_[layer];
    return r.from >= 0 && r.fade > 0.0 ? static_cast<float>(std::clamp(r.faded / r.fade, 0.0, 1.0)) : 1.0f;
}

} // namespace noor::asset
