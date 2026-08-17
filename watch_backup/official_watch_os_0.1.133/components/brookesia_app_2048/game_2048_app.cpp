#include "game_2048_app.hpp"

#include <cstdlib>
#include <cstdio>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "esp_random.h"
#include "lvgl.h"
#include "watch_app_icons.hpp"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Game2048"

namespace esp_brookesia::apps {
namespace {
constexpr const char *APP_NAME = "2048";
constexpr uint32_t COLORS[] = {0x252A34, 0xE8D9C5, 0xEBCB8B, 0xE59A5C, 0xDA6B56, 0xC95D75, 0x8E6BBE, 0x4BA3C7, 0x2D8394, 0x2A697B, 0x1F4E5F};

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}
} // namespace

Game2048App *Game2048App::_instance = nullptr;
Game2048App *Game2048App::requestInstance()
{
    if (_instance == nullptr) _instance = new Game2048App();
    return _instance;
}

Game2048App::Game2048App() : systems::phone::App(APP_NAME, watch_app_icon_game_48(), true, true, true) {}

bool Game2048App::run(void)
{
    auto *screen = lv_scr_act();
    ESP_UTILS_CHECK_NULL_RETURN(screen, false, "Invalid active screen");
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10131A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    auto *root = lv_obj_create(screen);
    ESP_UTILS_CHECK_NULL_RETURN(root, false, "Create root failed");
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_left(root, 12, 0);
    lv_obj_set_style_pad_right(root, 12, 0);
    lv_obj_set_style_pad_top(root, 10, 0);
    lv_obj_set_style_pad_bottom(root, 4, 0);
    lv_obj_set_style_pad_row(root, 4, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

    auto *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 42);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(header, "2048", &lv_font_montserrat_24, 0xF5F6F8);
    _score_label = make_label(header, "Score 0", &lv_font_montserrat_14, 0xB8C1D1);

    // Keep the whole display for play rather than dedicating a row to hints.
    _state_label = nullptr;
    auto *board = lv_obj_create(root);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, LV_PCT(100), 310);
    lv_obj_set_style_bg_color(board, lv_color_hex(0x1D222C), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(board, 8, 0);
    lv_obj_set_style_pad_all(board, 5, 0);
    lv_obj_set_style_pad_row(board, 5, 0);
    lv_obj_set_style_pad_column(board, 5, 0);
    lv_obj_set_flex_flow(board, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_add_flag(board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board, onPressed, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(board, onPressing, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(board, onReleased, LV_EVENT_RELEASED, this);
    for (size_t i = 0; i < _tiles.size(); ++i) {
        auto *tile = lv_obj_create(board);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, LV_PCT(23), 45);
        lv_obj_set_style_radius(tile, 6, 0);
        // Tile touches must reach the board's official LVGL gesture handler.
        lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE);
        _tiles[i] = make_label(tile, "", &lv_font_montserrat_18, 0x1B202A);
        lv_obj_add_flag(_tiles[i], LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_center(_tiles[i]);
    }
    auto *controls = lv_obj_create(root);
    lv_obj_remove_style_all(controls);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, 36);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(controls, 8, 0);
    auto *restart = lv_button_create(controls);
    lv_obj_set_size(restart, LV_PCT(48), 34);
    lv_obj_set_style_radius(restart, 16, 0);
    lv_obj_set_style_bg_color(restart, lv_color_hex(0x315C9E), 0);
    lv_obj_set_style_bg_opa(restart, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(restart, onRestart, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(restart, "New game", &lv_font_montserrat_14, 0xFFFFFF));
    auto *back = lv_button_create(controls);
    lv_obj_set_size(back, LV_PCT(48), 34);
    lv_obj_set_style_radius(back, 16, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2B303B), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, this);
    lv_obj_center(make_label(back, "Back", &lv_font_montserrat_14, 0xFFFFFF));
    reset();
    return true;
}

bool Game2048App::back(void)
{
    _score_label = nullptr;
    _state_label = nullptr;
    _tiles.fill(nullptr);
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

void Game2048App::reset()
{
    if (_score > _best_score) _best_score = _score;
    _board.fill(0); _score = 0; addTile(); addTile(); render();
}

void Game2048App::addTile()
{
    uint8_t empty[CELLS]; uint8_t count = 0;
    for (uint8_t i = 0; i < CELLS; ++i) if (_board[i] == 0) empty[count++] = i;
    if (count) _board[empty[esp_random() % count]] = (esp_random() % 10 == 0) ? 4 : 2;
}

bool Game2048App::move(Direction direction)
{
    bool changed = false;
    const int line_count = (direction == Direction::Left || direction == Direction::Right) ? ROWS : COLS;
    const int line_length = (direction == Direction::Left || direction == Direction::Right) ? COLS : ROWS;
    for (int line = 0; line < line_count; ++line) {
        uint16_t input[ROWS] = {}, output[ROWS] = {};
        for (int pos = 0; pos < line_length; ++pos) {
            int r = line, c = pos;
            if (direction == Direction::Left) { r = line; c = pos; }
            if (direction == Direction::Right) { r = line; c = COLS - 1 - pos; }
            if (direction == Direction::Up) { r = pos; c = line; }
            if (direction == Direction::Down) { r = ROWS - 1 - pos; c = line; }
            input[pos] = _board[r * COLS + c];
        }
        int write = 0;
        for (int i = 0; i < line_length;) {
            if (!input[i]) { ++i; continue; }
            if (i + 1 < line_length && input[i] == input[i + 1]) { output[write++] = input[i] * 2; _score += input[i] * 2; i += 2; }
            else { output[write++] = input[i]; ++i; }
        }
        for (int pos = 0; pos < line_length; ++pos) {
            int r = line, c = pos;
            if (direction == Direction::Left) { r = line; c = pos; }
            if (direction == Direction::Right) { r = line; c = COLS - 1 - pos; }
            if (direction == Direction::Up) { r = pos; c = line; }
            if (direction == Direction::Down) { r = ROWS - 1 - pos; c = line; }
            const int idx = r * COLS + c;
            if (_board[idx] != output[pos]) changed = true;
            _board[idx] = output[pos];
        }
    }
    if (changed) {
        if (_score > _best_score) _best_score = _score;
        addTile();
        render();
    } else if (!canMove() && _state_label) {
        lv_label_set_text(_state_label, "No moves. Start a new game.");
    }
    return changed;
}

bool Game2048App::canMove() const
{
    for (int i = 0; i < CELLS; ++i) {
        if (_board[i] == 0) return true;
        if (i % COLS < COLS - 1 && _board[i] == _board[i + 1]) return true;
        if (i < CELLS - COLS && _board[i] == _board[i + COLS]) return true;
    }
    return false;
}

void Game2048App::render()
{
    if (_score_label) {
        char score[44];
        std::snprintf(score, sizeof(score), "Score %lu\nBest %lu", static_cast<unsigned long>(_score),
                      static_cast<unsigned long>(_best_score));
        lv_label_set_text(_score_label, score);
        lv_obj_set_style_text_align(_score_label, LV_TEXT_ALIGN_RIGHT, 0);
    }
    bool won = false;
    for (size_t i = 0; i < _board.size(); ++i) {
        if (!_tiles[i]) continue;
        const uint16_t value = _board[i];
        const int index = value == 0 ? 0 : (value <= 2 ? 1 : value <= 4 ? 2 : value <= 8 ? 3 : value <= 16 ? 4 : value <= 32 ? 5 : value <= 64 ? 6 : value <= 128 ? 7 : value <= 256 ? 8 : value <= 512 ? 9 : 10);
        auto *tile = lv_obj_get_parent(_tiles[i]);
        lv_obj_set_style_bg_color(tile, lv_color_hex(COLORS[index]), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        char text[8] = {}; if (value) std::snprintf(text, sizeof(text), "%u", value);
        lv_label_set_text(_tiles[i], text);
        lv_obj_set_style_text_color(_tiles[i], lv_color_hex(value <= 4 ? 0x1B202A : 0xFFFFFF), 0);
        lv_obj_set_style_text_font(_tiles[i], value >= 1024 ? &lv_font_montserrat_14 : &lv_font_montserrat_18, 0);
        lv_obj_center(_tiles[i]);
        if (value >= 2048) won = true;
    }
    if (_state_label) lv_label_set_text(_state_label, won ? "2048 reached! Keep playing." : (canMove() ? "Swipe to move" : "No moves. Start a new game."));
}

void Game2048App::onPressed(lv_event_t *event)
{
    auto *app = static_cast<Game2048App *>(lv_event_get_user_data(event));
    auto *indev = lv_indev_active();
    if (!app || indev == nullptr) return;
    lv_point_t point = {};
    lv_indev_get_point(indev, &point);
    app->_touch_x = point.x;
    app->_touch_y = point.y;
    app->_moved_this_touch = false;
}

void Game2048App::onPressing(lv_event_t *event)
{
    auto *app = static_cast<Game2048App *>(lv_event_get_user_data(event));
    auto *indev = lv_indev_active();
    if (!app || indev == nullptr || app->_moved_this_touch) return;
    lv_point_t point = {};
    lv_indev_get_point(indev, &point);
    const int dx = point.x - app->_touch_x;
    const int dy = point.y - app->_touch_y;
    if (std::abs(dx) < 12 && std::abs(dy) < 12) return;
    app->_moved_this_touch = true;
    if (std::abs(dx) >= std::abs(dy)) app->move(dx > 0 ? Direction::Right : Direction::Left);
    else app->move(dy > 0 ? Direction::Down : Direction::Up);
}

void Game2048App::onReleased(lv_event_t *event)
{
    auto *app = static_cast<Game2048App *>(lv_event_get_user_data(event));
    if (app != nullptr) {
        // A new press starts a fresh drag; keep this explicit for touch drivers
        // that coalesce PRESSING notifications.
        app->_moved_this_touch = false;
    }
}
void Game2048App::onRestart(lv_event_t *event) { if (auto *app = static_cast<Game2048App *>(lv_event_get_user_data(event))) app->reset(); }
void Game2048App::onBack(lv_event_t *event) { if (auto *app = static_cast<Game2048App *>(lv_event_get_user_data(event))) app->back(); }

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, Game2048App, APP_NAME, []()
{
    return std::shared_ptr<Game2048App>(Game2048App::requestInstance(), [](Game2048App *) {});
})

} // namespace esp_brookesia::apps
