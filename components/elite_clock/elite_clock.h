#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/display/display.h"
#include "esphome/components/time/real_time_clock.h"

#include "ships.h"

namespace esphome {
namespace elite_clock {

struct Theme {
  const char *name;
  Color ship;    // ship wireframe
  Color text;    // clock + general text
  Color accent;  // ship name, messages
  Color frame;   // border lines
  Color stars;   // starfield
};

class EliteClock : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  void set_fallback_time(time::RealTimeClock *t) { this->fallback_time_ = t; }

  /// Call from the display lambda: id(elite).draw(it);
  void draw(display::Display &it);

  // ---- ships ----
  void set_ship(int index, bool animate = true);
  bool set_ship_by_name(const std::string &name);
  void next_ship();
  void prev_ship();
  void random_ship();
  int ship_index() const { return this->ship_; }
  std::string ship_name() const { return SHIPS[this->ship_].name; }
  static int ship_count() { return NUM_SHIPS; }

  // ---- settings (normally driven from Home Assistant entities) ----
  void set_commander(const std::string &name) { this->commander_ = name; }
  void set_auto_cycle(bool v) { this->auto_cycle_ = v; this->last_change_ = millis(); }
  void set_cycle_minutes(float m) { this->cycle_ms_ = (uint32_t) (m * 60000.0f); }
  void set_cycle_random(bool v) { this->cycle_random_ = v; }
  void set_rotation_speed(float s) { this->rot_speed_ = s; }
  bool set_theme_by_name(const std::string &name);
  void set_use_24h(bool v) { this->use_24h_ = v; }
  void set_hidden_lines(bool v) { this->hidden_lines_ = v; }
  void set_starfield(bool v) { this->starfield_ = v; }
  void set_show_info(bool v) { this->show_info_ = v; }
  void set_exec_font(bool v) { this->exec_font_ = v; }
  void set_show_date(bool v) { this->show_date_ = v; }
  void show_message(const std::string &msg, float seconds = 10.0f);
  void clear_message() { this->message_.clear(); }

  // ---- touch (wire up from the touchscreen triggers) ----
  void touch_start(int x, int y);
  void touch_move(int x, int y);
  void touch_end();

 protected:
  void prepare_ship_();
  void update_motion_(float dt);
  void draw_stars_(display::Display &it, float dt);
  void draw_ship_(display::Display &it);
  void draw_text_(display::Display &it, int x, int y, const char *s, int scale, Color c);
  void draw_text_centered_(display::Display &it, int cx, int y, const char *s, int scale, Color c);
  void draw_text_right_(display::Display &it, int rx, int y, const char *s, int scale, Color c);
  void info_line_(char *buf, size_t len);

  time::RealTimeClock *time_{nullptr};
  time::RealTimeClock *fallback_time_{nullptr};

  // state
  int ship_{0};
  float m_[3][3];               // ship orientation matrix
  float dist_{1000.0f};         // current camera distance
  float target_dist_{1000.0f};  // resting camera distance
  float fly_t_{1.0f};           // 0..1 fly-in animation progress
  float face_pt_[32][3];        // a point on each face (for back-face culling)
  uint32_t last_frame_{0};
  uint32_t fly_start_{0};
  uint32_t fps_t0_{0};
  uint32_t fps_frames_{0};
  uint32_t last_change_{0};
  uint16_t frame_count_{0};

  struct Star { float x, y, z; };
  static const int NUM_STARS = 48;
  Star stars_[NUM_STARS];

  // settings
  std::string commander_{"Jameson"};
  bool auto_cycle_{true};
  uint32_t cycle_ms_{5 * 60000};
  bool cycle_random_{false};
  float rot_speed_{5.0f};
  int theme_{0};
  bool use_24h_{true};
  bool hidden_lines_{true};
  bool starfield_{true};
  bool show_info_{true};
  bool exec_font_{false};
  bool show_date_{true};

  std::string message_;
  uint32_t message_until_{0};

  // touch tracking
  bool touching_{false};
  bool long_fired_{false};
  int touch_x0_{0}, touch_y0_{0}, touch_x_{0}, touch_y_{0};
  uint32_t touch_t0_{0};
};

}  // namespace elite_clock
}  // namespace esphome
