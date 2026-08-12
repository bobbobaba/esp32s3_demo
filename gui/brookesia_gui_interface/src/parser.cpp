/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/parser_impl.hpp"

namespace esp_brookesia::gui {
using namespace parser_detail;


static std::expected<ParsedDocument, std::string> parse_document_impl(
    std::string_view json,
    std::string_view base_dir,
    const Environment &environment,
    std::vector<std::string> dependency_files)
{
    BROOKESIA_LOG_TRACE_GUARD();

    BROOKESIA_LOGD(
        "Params: json(size=%1%), base_dir(%2%), environment(%3%)",
        json.size(), base_dir, environment
    );

    const auto total_start = ParserProfileClock::now();
    auto stage_start = total_start;
    boost::system::error_code error_code;
    boost::json::value json_value = boost::json::parse(json, error_code);
    auto stage_end = ParserProfileClock::now();
    if (error_code) {
        BROOKESIA_LOGE("Failed to parse GUI JSON: %1%", error_code.message());
        return std::unexpected("Failed to parse GUI JSON: " + error_code.message());
    }
    if (!json_value.is_object()) {
        return std::unexpected("GUI root document must be a JSON object");
    }
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(parse_root_json), bytes(%2%), elapsed_ms(%3%), total_ms(%4%)",
        base_dir,
        json.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    const auto &root_object = json_value.as_object();
    const auto version = parse_string_field(root_object, "version", std::string(CURRENT_DOCUMENT_VERSION));
    if (!version) {
        return std::unexpected(version.error());
    }

    if (find_child_value(root_object, "constants") != nullptr || find_child_value(root_object, "nodes") != nullptr) {
        return std::unexpected("Legacy root fields 'constants'/'nodes' are no longer supported; use 'assets'");
    }

    ParsedDocument parsed_document;
    auto &document = parsed_document.document;
    document.version = *version;
    parsed_document.dependency_files = std::move(dependency_files);

    const std::filesystem::path resolved_base_dir = std::filesystem::path(base_dir).lexically_normal();
    boost::json::value constants((boost::json::object()));
    boost::unordered_flat_set<std::string> loaded_asset_paths;
    std::vector<RootAssetEntry> asset_entries;

    if (const auto *root_screen_flow_value = find_child_value(root_object, "screenFlow");
            root_screen_flow_value != nullptr) {
        (void)root_screen_flow_value;
        return std::unexpected("Root field 'screenFlow' is no longer supported; add a screenFlow asset to assets[]");
    }

    stage_start = ParserProfileClock::now();
    auto append_result = append_root_asset_entries(
                             root_object,
                             "assets",
                             resolved_base_dir,
                             loaded_asset_paths,
                             &parsed_document.dependency_files,
                             asset_entries,
                             "inline asset"
                         );
    if (!append_result) {
        return std::unexpected(append_result.error());
    }

    const auto *variants_value = find_child_value(root_object, "variants");
    if (variants_value != nullptr) {
        if (!variants_value->is_array()) {
            return std::unexpected("Field 'variants' must be an array");
        }

        for (const auto &variant_value : variants_value->as_array()) {
            if (!variant_value.is_object()) {
                return std::unexpected("Variant entries must be objects");
            }

            const auto &variant_object = variant_value.as_object();
            if (find_child_value(variant_object, "constants") != nullptr ||
                    find_child_value(variant_object, "nodes") != nullptr) {
                return std::unexpected(
                           "Legacy variant fields 'constants'/'nodes' are no longer supported; use 'assets'"
                       );
            }

            auto when = parse_string_field(variant_object, "when", "${expr(true)}");
            if (!when) {
                return std::unexpected(when.error());
            }
            if (!is_expression_string(*when)) {
                return std::unexpected(
                           "Variant field 'when' must use '${expr(...)}'; migrate old expressions such as "
                           "'WidthDp == 1024' to '${expr(${env.widthDp} == 1024dp)}'"
                       );
            }
            if (when->find("${env.theme}") != std::string::npos) {
                document.environment_dependencies.theme = true;
            }
            if (when->find("${env.language}") != std::string::npos) {
                document.environment_dependencies.language = true;
            }
            if (when->find("${env.widthPx}") != std::string::npos ||
                    when->find("${env.heightPx}") != std::string::npos ||
                    when->find("${env.widthDp}") != std::string::npos ||
                    when->find("${env.heightDp}") != std::string::npos ||
                    when->find("${env.density}") != std::string::npos ||
                    when->find("${env.fontScale}") != std::string::npos) {
                document.environment_dependencies.metrics = true;
            }
            document.theme_sensitive = document.environment_dependencies.any();

            auto matched_value = evaluate_expression_string(*when, constants, environment);
            if (!matched_value) {
                return std::unexpected(matched_value.error());
            }
            if (!matched_value->is_bool()) {
                return std::unexpected("Variant field 'when' expression must evaluate to boolean");
            }
            const bool matched = matched_value->as_bool();
            if (!matched) {
                continue;
            }

            if (const auto *variant_screen_flow_value = find_child_value(variant_object, "screenFlow");
                    variant_screen_flow_value != nullptr) {
                (void)variant_screen_flow_value;
                return std::unexpected(
                           "Variant field 'screenFlow' is no longer supported; add a screenFlow asset to assets[]"
                       );
            }

            append_result = append_root_asset_entries(
                                variant_object,
                                "assets",
                                resolved_base_dir,
                                loaded_asset_paths,
                                &parsed_document.dependency_files,
                                asset_entries,
                                "inline variant asset"
                            );
            if (!append_result) {
                return std::unexpected(append_result.error());
            }
        }
    }
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(load_asset_entries), assets(%2%), dependencies(%3%), "
        "elapsed_ms(%4%), total_ms(%5%)",
        base_dir,
        asset_entries.size(),
        parsed_document.dependency_files.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    stage_start = ParserProfileClock::now();
    for (auto &asset_entry : asset_entries) {
        const auto &asset_object = asset_entry.value.as_object();
        auto asset_type = parse_string_field(asset_object, "type");
        if (!asset_type) {
            return std::unexpected(
                       "Failed to parse asset '" + asset_entry.source_label + "': " + asset_type.error()
                   );
        }
        if (*asset_type != "constant") {
            continue;
        }

        auto constant_asset = asset_entry.value;
        auto replace_result = substitute_references(constant_asset, constants, environment);
        if (!replace_result) {
            return std::unexpected(
                       "Failed to resolve asset '" + asset_entry.source_label + "': " + replace_result.error()
                   );
        }

        const auto &constant_object = constant_asset.as_object();
        const auto *data_value = find_child_value(constant_object, "data");
        if (data_value == nullptr || !data_value->is_object()) {
            return std::unexpected(
                       "Failed to parse constant asset '" + asset_entry.source_label +
                       "': Constant asset must contain an object field 'data'"
                   );
        }
        merge_json(constants, *data_value);
    }

    document.constants = constants;
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(resolve_constants), assets(%2%), elapsed_ms(%3%), total_ms(%4%)",
        base_dir,
        asset_entries.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    stage_start = ParserProfileClock::now();
    std::vector<ResolvedAssetEntry> resolved_assets;
    for (const auto &asset_entry : asset_entries) {
        const auto &asset_object = asset_entry.value.as_object();
        auto asset_type = parse_string_field(asset_object, "type");
        if (!asset_type) {
            return std::unexpected(
                       "Failed to parse asset '" + asset_entry.source_label + "': " + asset_type.error()
                   );
        }
        if (*asset_type == "constant") {
            continue;
        }

        auto resolved_asset = asset_entry.value;
        auto replace_result = substitute_references(resolved_asset, constants, environment);
        if (!replace_result) {
            return std::unexpected(
                       "Failed to resolve asset '" + asset_entry.source_label + "': " + replace_result.error()
                   );
        }

        resolved_assets.push_back(ResolvedAssetEntry{
            .value = std::move(resolved_asset),
            .base_dir = asset_entry.base_dir,
            .source_label = asset_entry.source_label,
            .type = *asset_type,
        });
    }
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(resolve_assets), resolved_assets(%2%), elapsed_ms(%3%), "
        "total_ms(%4%)",
        base_dir,
        resolved_assets.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    stage_start = ParserProfileClock::now();
    for (const auto &asset_entry : resolved_assets) {
        const auto &resolved_asset_object = asset_entry.value.as_object();

        if (asset_entry.type == "font" || asset_entry.type == "fontSet") {
            return std::unexpected(
                       "Document asset type '" + asset_entry.type +
                       "' is not supported; register fonts globally via Runtime"
                   );
        }

        if (asset_entry.type == "image") {
            return std::unexpected(
                       "Document asset type 'image' is no longer supported; use type='imageSet' with images[]"
                   );
        }

        if (asset_entry.type == "imageSet") {
            auto images = parse_image_asset_set(resolved_asset_object, asset_entry.base_dir);
            if (!images) {
                return std::unexpected(
                           "Failed to parse imageSet asset '" + asset_entry.source_label + "': " + images.error()
                       );
            }
            std::move(images->begin(), images->end(), std::back_inserter(document.images));
            continue;
        }

        if (asset_entry.type == "theme") {
            return std::unexpected(
                       "Document asset type 'theme' is no longer supported; load themes globally via Runtime"
                   );
        }
    }
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(parse_image_assets), images(%2%), elapsed_ms(%3%), "
        "total_ms(%4%)",
        base_dir,
        document.images.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    stage_start = ParserProfileClock::now();
    for (const auto &asset_entry : resolved_assets) {
        const auto &resolved_asset_object = asset_entry.value.as_object();
        if (asset_entry.type != "styleSet") {
            continue;
        }
        const auto *styles_value = find_child_value(resolved_asset_object, "styles");
        if (styles_value == nullptr || !styles_value->is_object()) {
            return std::unexpected(
                       "Failed to parse styleSet asset '" + asset_entry.source_label +
                       "': styleSet must contain object field 'styles'"
                   );
        }
        auto styles = parse_named_style_map(styles_value->as_object(), constants, environment, false);
        if (!styles) {
            return std::unexpected(
                       "Failed to parse styleSet asset '" + asset_entry.source_label + "': " + styles.error()
                   );
        }
        for (auto &[key, style] : *styles) {
            document.styles.insert_or_assign(std::move(key), std::move(style));
        }
    }
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(parse_style_assets), styles(%2%), elapsed_ms(%3%), total_ms(%4%)",
        base_dir,
        document.styles.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    stage_start = ParserProfileClock::now();
    InteractionTemplateRawMap raw_interactions;
    for (const auto &asset_entry : resolved_assets) {
        const auto &resolved_asset_object = asset_entry.value.as_object();
        if (asset_entry.type != "interactionTemplate") {
            continue;
        }
        auto id = parse_string_field(resolved_asset_object, "id");
        if (!id) {
            return std::unexpected(
                       "Failed to parse interactionTemplate asset '" + asset_entry.source_label + "': " + id.error()
                   );
        }
        auto interaction_object = resolved_asset_object;
        for (const auto &entry : interaction_object) {
            const auto normalized_key = normalize_object_key(entry.key());
            if (normalized_key == "type" || normalized_key == "id" || normalized_key == "common_props" ||
                    normalized_key == "events" || normalized_key == "animations" || normalized_key == "state_styles") {
                continue;
            }
            return std::unexpected(
                       "Failed to parse interactionTemplate asset '" + asset_entry.source_label +
                       "': unsupported field '" + std::string(entry.key()) + "'"
                   );
        }
        interaction_object.erase("type");
        interaction_object.erase("id");
        auto [unused_it, inserted] = raw_interactions.emplace(*id, std::move(interaction_object));
        if (!inserted) {
            return std::unexpected("Duplicate interactionTemplate id: " + *id);
        }
    }
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(parse_interaction_templates), interactions(%2%), "
        "elapsed_ms(%3%), total_ms(%4%)",
        base_dir,
        raw_interactions.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    stage_start = ParserProfileClock::now();
    TemplateRawMap raw_templates;
    for (const auto &asset_entry : resolved_assets) {
        const auto &resolved_asset_object = asset_entry.value.as_object();
        if (asset_entry.type == "imageSet" || asset_entry.type == "interactionTemplate" ||
                asset_entry.type == "styleSet") {
            continue;
        }
        if (asset_entry.type == "view") {
            return std::unexpected(
                       "Document asset type 'view' is no longer supported; use viewScreen or viewTemplate"
                   );
        }
        if (asset_entry.type != "viewTemplate") {
            continue;
        }
        auto id = parse_string_field(resolved_asset_object, "id");
        if (!id) {
            return std::unexpected(
                       "Failed to parse viewTemplate asset '" + asset_entry.source_label + "': " + id.error()
                   );
        }
        const auto *node_value = find_child_value(resolved_asset_object, "node");
        if (node_value == nullptr || !node_value->is_object()) {
            return std::unexpected(
                       "Failed to parse viewTemplate asset '" + asset_entry.source_label +
                       "': viewTemplate must contain object field 'node'"
                   );
        }
        auto node_object = node_value->as_object();
        if (find_child_value(node_object, "id") != nullptr) {
            return std::unexpected(
                       "Failed to parse viewTemplate asset '" + asset_entry.source_label +
                       "': viewTemplate.node must not contain field 'id'"
                   );
        }
        node_object.insert_or_assign("id", *id);
        auto [unused_it, inserted] = raw_templates.emplace(*id, node_object);
        if (!inserted) {
            return std::unexpected("Duplicate viewTemplate id: " + *id);
        }
    }

    for (const auto &asset_entry : resolved_assets) {
        const auto &resolved_asset_object = asset_entry.value.as_object();
        if (asset_entry.type == "imageSet" || asset_entry.type == "interactionTemplate" ||
                asset_entry.type == "styleSet") {
            continue;
        }
        if (asset_entry.type != "viewTemplate") {
            continue;
        }
        std::vector<std::string> template_stack;
        auto template_object = raw_templates.at(std::string(resolved_asset_object.at("id").as_string().c_str()));
        auto slot_result = apply_template_slots(template_object, nullptr);
        if (!slot_result) {
            return std::unexpected(
                       "Failed to parse viewTemplate asset '" + asset_entry.source_label + "': " + slot_result.error()
                   );
        }
        auto node = parse_view_node(template_object, environment, raw_templates, raw_interactions, template_stack);
        if (!node) {
            return std::unexpected(
                       "Failed to parse viewTemplate asset '" + asset_entry.source_label + "': " + node.error()
                   );
        }
        if (node->type == NodeType::Screen) {
            return std::unexpected("viewTemplate root node must not be a screen");
        }
        document.templates.push_back(std::move(*node));
    }
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(parse_templates), raw_templates(%2%), templates(%3%), "
        "elapsed_ms(%4%), total_ms(%5%)",
        base_dir,
        raw_templates.size(),
        document.templates.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    stage_start = ParserProfileClock::now();
    for (const auto &asset_entry : resolved_assets) {
        const auto &resolved_asset_object = asset_entry.value.as_object();
        if (asset_entry.type == "imageSet" || asset_entry.type == "interactionTemplate" ||
                asset_entry.type == "styleSet") {
            continue;
        }
        if (asset_entry.type == "screenFlow") {
            auto flow = parse_screen_flow(resolved_asset_object);
            if (!flow) {
                return std::unexpected(
                           "Failed to parse screenFlow asset '" + asset_entry.source_label + "': " + flow.error()
                       );
            }
            document.screen_flows.push_back(std::move(*flow));
            continue;
        }
        if (asset_entry.type == "viewTemplate") {
            continue;
        }
        if (asset_entry.type != "viewScreen") {
            return std::unexpected(
                       "Unsupported asset type in '" + asset_entry.source_label + "': " + asset_entry.type
                   );
        }

        std::vector<std::string> template_stack;
        auto node = parse_view_node(
                        resolved_asset_object,
                        environment,
                        raw_templates,
                        raw_interactions,
                        template_stack,
                        NodeType::Screen
                    );
        if (!node) {
            return std::unexpected(
                       "Failed to parse viewScreen asset '" + asset_entry.source_label + "': " + node.error()
                   );
        }
        document.screens.push_back(std::move(*node));
    }
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(parse_flows_screens), flows(%2%), screens(%3%), "
        "elapsed_ms(%4%), total_ms(%5%)",
        base_dir,
        document.screen_flows.size(),
        document.screens.size(),
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );

    stage_start = ParserProfileClock::now();
    auto validation = validate_document(document);
    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(validate_document), elapsed_ms(%2%), total_ms(%3%)",
        base_dir,
        parser_profile_elapsed_ms(stage_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );
    if (!validation.success) {
        return std::unexpected(validation.errors.empty() ? "Invalid GUI document" : validation.errors.front());
    }

    stage_end = ParserProfileClock::now();
    GUI_INTERFACE_PROFILE_LOGI(
        "GUI parser profile: base_dir(%1%), stage(total), dependencies(%2%), images(%3%), styles(%4%), "
        "templates(%5%), flows(%6%), screens(%7%), elapsed_ms(%8%), total_ms(%9%)",
        base_dir,
        parsed_document.dependency_files.size(),
        document.images.size(),
        document.styles.size(),
        document.templates.size(),
        document.screen_flows.size(),
        document.screens.size(),
        parser_profile_elapsed_ms(total_start, stage_end),
        parser_profile_elapsed_ms(total_start, stage_end)
    );
    return parsed_document;
}

std::expected<Document, std::string> parse_document(
    std::string_view json, std::string_view base_dir, const Environment &environment)
{
    auto parsed_document = parse_document_impl(json, base_dir, environment, {});
    if (!parsed_document) {
        return std::unexpected(parsed_document.error());
    }
    return parsed_document->document;
}

std::expected<std::vector<FontAsset>, std::string> parse_font_asset_set_json(
    std::string_view json,
    std::string_view base_dir)
{
    auto value = boost::json::parse(json);
    if (!value.is_object()) {
        return std::unexpected("Font asset set JSON must be an object");
    }
    return parse_font_asset_set(value.as_object(), std::filesystem::path(base_dir));
}

std::expected<std::vector<FontAsset>, std::string> parse_font_asset_set_file(std::string_view path)
{
    const std::filesystem::path file_path = std::filesystem::path(path).lexically_normal();
    auto text = read_text_file(file_path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return parse_font_asset_set_json(*text, file_path.parent_path().string());
}

std::expected<std::vector<ImageAsset>, std::string> parse_image_asset_set_json(
    std::string_view json,
    std::string_view base_dir)
{
    auto value = boost::json::parse(json);
    if (!value.is_object()) {
        return std::unexpected("Image asset set JSON must be an object");
    }
    return parse_image_asset_set(value.as_object(), std::filesystem::path(base_dir));
}

std::expected<std::vector<ImageAsset>, std::string> parse_image_asset_set_file(std::string_view path)
{
    const std::filesystem::path file_path = std::filesystem::path(path).lexically_normal();
    auto text = read_text_file(file_path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return parse_image_asset_set_json(*text, file_path.parent_path().string());
}

std::expected<ThemeAsset, std::string> parse_theme_asset_json(
    std::string_view json,
    std::string_view base_dir,
    const Environment &environment)
{
    auto value = boost::json::parse(json);
    if (!value.is_object()) {
        return std::unexpected("Theme asset JSON must be an object");
    }
    const auto &object = value.as_object();
    auto type = parse_string_field(object, "type");
    if (!type) {
        return std::unexpected(type.error());
    }
    if (*type != "theme") {
        return std::unexpected("Theme asset must use type='theme'");
    }
    return parse_theme_asset(object, std::filesystem::path(base_dir).lexically_normal(), environment);
}

std::expected<ThemeAsset, std::string> parse_theme_asset_file(
    std::string_view path,
    const Environment &environment)
{
    const std::filesystem::path file_path = std::filesystem::path(path).lexically_normal();
    auto text = read_text_file(file_path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return parse_theme_asset_json(*text, file_path.parent_path().string(), environment);
}

std::expected<ParsedDocument, std::string> parse_document_file_with_metadata(
    std::string_view path,
    const Environment &environment)
{
    BROOKESIA_LOG_TRACE_GUARD();

    BROOKESIA_LOGD("Params: path(%1%), environment(%2%)", path, environment);

    const std::filesystem::path file_path = std::filesystem::path(path).lexically_normal();
    auto text = read_text_file(file_path);
    if (!text) {
        return std::unexpected(text.error());
    }
    GUI_INTERFACE_PROFILE_LOGI("Parsing GUI root file '%1%'", file_path.string());
    std::vector<std::string> dependency_files;
    dependency_files.push_back(file_path.string());
    return parse_document_impl(*text, file_path.parent_path().string(), environment, std::move(dependency_files));
}

std::expected<Document, std::string> parse_document_file(std::string_view path, const Environment &environment)
{
    auto parsed_document = parse_document_file_with_metadata(path, environment);
    if (!parsed_document) {
        return std::unexpected(parsed_document.error());
    }
    return parsed_document->document;
}

} // namespace esp_brookesia::gui
