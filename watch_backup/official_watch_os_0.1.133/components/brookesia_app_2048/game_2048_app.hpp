#pragma once

#include <array>

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class Game2048App : public systems::phone::App {
public:
    static Game2048App *requestInstance();
    ~Game2048App() override = default;

protected:
    Game2048App();
    bool run(void) override;
    bool back(void) override;

private:
    enum class Direction { Left, Right, Up, Down };
    static void onPressed(lv_event_t *event);
    static void onPressing(lv_event_t *event);
    static void onReleased(lv_event_t *event);
    static void onRestart(lv_event_t *event);
    static void onBack(lv_event_t *event);
    void reset();
    bool move(Direction direction);
    bool canMove() const;
    void addTile();
    void render();

    static Game2048App *_instance;
    static constexpr int COLS = 4;
    static constexpr int ROWS = 6;
    static constexpr int CELLS = COLS * ROWS;
    std::array<uint16_t, CELLS> _board{};
    std::array<lv_obj_t *, CELLS> _tiles{};
    lv_obj_t *_score_label = nullptr;
    lv_obj_t *_state_label = nullptr;
    int16_t _touch_x = 0;
    int16_t _touch_y = 0;
    bool _moved_this_touch = false;
    uint32_t _score = 0;
    uint32_t _best_score = 0;
};

} // namespace esp_brookesia::apps
