// The UI tab's side of the document (gameplay foundations part 6d): the project's `.noorui` pages, new ones from
// templates, a page's elements, and edits made as small changes to the page's text (noor::ui::edit_page), written at
// once. Each page has its own undo and redo: the text before each change (so an edit in NoorCode meanwhile is kept
// too: undo goes back to the text as it was just before the builder's change).
#include "document.h"

#include "noor/foundation/file_io.h"
#include "noor/ui/page_source.h"

#include <format>

namespace noor::editor {
namespace {
using asset::Json;
namespace stdfs = std::filesystem;

constexpr std::size_t kUndoDepth = 100;

// A page's path, checked: inside the project, a .noorui file.
Result<stdfs::path> page_file(const stdfs::path& root, const Json& command) {
    const std::string path = command.value("path", std::string());
    if (path.empty()) return fail(ErrorCode::InvalidArgument, "which page? (a path like \"UI/pause.noorui\")");
    const stdfs::path rel = stdfs::path(std::u8string(path.begin(), path.end())).lexically_normal();
    if (rel.is_absolute() || rel.has_root_name() || rel.empty() || *rel.begin() == "..")
        return fail(ErrorCode::InvalidArgument, std::format("{}: not a path inside the project", path));
    if (rel.extension() != ".noorui") return fail(ErrorCode::InvalidArgument, std::format("{}: a page's file ends in .noorui", path));
    return root / rel;
}

std::string page_path(const stdfs::path& root, const stdfs::path& file) {
    const auto u8 = file.lexically_relative(root).generic_u8string();
    return std::string(u8.begin(), u8.end());
}
} // namespace

Result<Json> Document::ui(const Json& command) {
    if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
        return fail(ErrorCode::InvalidArgument, "ui: the command needs an \"op\"");
    const std::string op = command["op"];
    const stdfs::path root = project_.root();

    if (op == "templates") return Json{{"templates", ui::page_templates()}, {"palette", ui::page_palette()}};
    if (op == "list") {
        Json pages = Json::array();
        std::error_code ec;
        for (auto it = stdfs::recursive_directory_iterator(root, ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
            const auto name = it->path().filename().string();
            if (it->is_directory() && (name == ".noor" || name == ".git" || name == "bin" || name == "obj")) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && it->path().extension() == ".noorui") pages.push_back(page_path(root, it->path()));
        }
        std::sort(pages.begin(), pages.end());
        return Json{{"pages", std::move(pages)}};
    }

    auto file = page_file(root, command);
    if (!file) return std::unexpected(file.error());
    const std::string path = page_path(root, *file);
    auto& history = ui_history_[path];
    const auto answer = [&](const std::string& text) -> Result<Json> {
        auto body = ui::parse_page(text);
        Json out{{"path", path}, {"can_undo", !history.undo.empty()}, {"can_redo", !history.redo.empty()}};
        if (body) out["tree"] = ui::page_tree(*body);
        else out["error"] = body.error().message;  // a page that doesn't read: its text still opens in NoorCode
        return out;
    };
    const auto write = [&](const std::string& text) -> Status {
        std::error_code ec;
        stdfs::create_directories(file->parent_path(), ec);
        return fs::write_file_atomic(*file, text);
    };

    if (op == "new") {
        std::error_code ec;
        if (stdfs::exists(*file, ec)) return fail(ErrorCode::AlreadyExists, std::format("{} exists already", path));
        auto text = ui::page_template(command.value("template", std::string("blank")),
                                      command.value("title", file->stem().string()));
        if (!text) return std::unexpected(text.error());
        if (auto st = write(*text); !st) return std::unexpected(st.error());
        history = {};
        return answer(*text);
    }

    auto current = fs::read_file(*file, 16u << 20);
    if (!current) return std::unexpected(current.error());
    if (op == "tree") return answer(*current);
    if (op == "source") return Json{{"path", path}, {"text", *current}};

    const auto change = [&](const std::string& text) -> Result<Json> {
        if (text == *current) return answer(text);
        if (auto st = write(text); !st) return std::unexpected(st.error());
        history.undo.push_back(*current);
        if (history.undo.size() > kUndoDepth) history.undo.erase(history.undo.begin());
        history.redo.clear();
        return answer(text);
    };
    if (op == "edit") {
        auto edited = ui::edit_page(*current, command.value("edit", Json()));
        if (!edited) return std::unexpected(edited.error());
        auto out = change(edited->text);
        if (out && !edited->inserted.empty()) (*out)["selected"] = edited->inserted;
        return out;
    }
    if (op == "set_source") {
        if (!command.contains("text") || !command["text"].is_string()) return fail(ErrorCode::InvalidArgument, "set_source: the page's \"text\"");
        return change(command["text"].get<std::string>());
    }
    if (op == "undo" || op == "redo") {
        auto& from = op == "undo" ? history.undo : history.redo;
        auto& to = op == "undo" ? history.redo : history.undo;
        if (from.empty()) return fail(ErrorCode::InvalidArgument, std::format("nothing to {}", op));
        std::string text = std::move(from.back());
        from.pop_back();
        if (auto st = write(text); !st) return std::unexpected(st.error());
        to.push_back(*current);
        return answer(text);
    }
    return fail(ErrorCode::InvalidArgument, std::format("ui: no operation \"{}\"", op));
}

} // namespace noor::editor
