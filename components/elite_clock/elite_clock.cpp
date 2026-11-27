#include "elite_clock.h"
#include "font.h"

#include <cmath>
#include <cstring>

#include "esphome/core/log.h"

namespace esphome {
namespace elite_clock {

static const char *const TAG = "elite_clock";

// ---------------------------------------------------------------------------
// Screen layout (320 x 240 landscape)
// ---------------------------------------------------------------------------
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int DIVIDER_Y = 177;               // line between space view and clock
static const int VIEW_X = 1, VIEW_Y = 1;        // space view (inside the frame)
static const int VIEW_W = SCREEN_W - 2, VIEW_H = DIVIDER_Y - 1;
static const int SHIP_CX = SCREEN_W / 2;        // ship projection centre
static const int SHIP_CY = 90;
static const float FOCAL = 256.0f;              // perspective focal length (pixels)
static const float SHIP_SCREEN_RADIUS = 60.0f;  // resting projected radius of a ship
static const float FLY_IN_SECONDS = 1.6f;

// ---------------------------------------------------------------------------
// Colour themes. Order must match the "Colour Theme" select in the YAML.
// ---------------------------------------------------------------------------
static const Theme THEMES[] = {
    // name                ship                      text                      accent                    frame                     stars
    {"Classic", Color(255, 255, 255), Color(255, 255, 255), Color(255, 255, 255), Color(255, 255, 255), Color(170, 170, 170)},
    {"Second Processor", Color(0, 255, 255), Color(255, 255, 0), Color(255, 0, 0), Color(255, 255, 0), Color(255, 255, 255)},
    {"Amber", Color(255, 176, 0), Color(255, 176, 0), Color(255, 220, 100), Color(180, 110, 0), Color(150, 100, 0)},
    {"Green Screen", Color(51, 255, 51), Color(51, 255, 51), Color(180, 255, 180), Color(0, 150, 0), Color(0, 140, 0)},
    {"Dangerous", Color(255, 113, 0), Color(255, 140, 30), Color(90, 200, 255), Color(255, 113, 0), Color(200, 200, 200)},
};
static const int NUM_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);

// Laser power -> description (Elite's laser power units)
static const char *laser_name(uint8_t p) {
  if (p == 0)
    return "None";
  if (p <= 2)
    return "Pulse";
  if (p <= 4)
    return "Beam";
  return "Military";
}

// ---------------------------------------------------------------------------
// Small matrix helpers
// ---------------------------------------------------------------------------
static void mat_identity(float m[3][3]) {
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      m[i][j] = (i == j) ? 1.0f : 0.0f;
}

// m = R(axis, angle) * m, rotating about a world axis (0=x, 1=y, 2=z)
static void mat_rotate(float m[3][3], int axis, float a) {
  float c = cosf(a), s = sinf(a);
  int p = (axis + 1) % 3, q = (axis + 2) % 3;
  for (int j = 0; j < 3; j++) {
    float u = m[p][j], v = m[q][j];
    m[p][j] = c * u - s * v;
    m[q][j] = s * u + c * v;
  }
}

// Re-orthonormalise the rows (stops floating-point drift squashing the ship)
static void mat_orthonormalise(float m[3][3]) {
  auto norm = [](float *v) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) {
      v[0] /= l;
      v[1] /= l;
      v[2] /= l;
    }
  };
  norm(m[0]);
  float d = m[0][0] * m[1][0] + m[0][1] * m[1][1] + m[0][2] * m[1][2];
  for (int i = 0; i < 3; i++)
    m[1][i] -= d * m[0][i];
  norm(m[1]);
  m[2][0] = m[0][1] * m[1][2] - m[0][2] * m[1][1];
  m[2][1] = m[0][2] * m[1][0] - m[0][0] * m[1][2];
  m[2][2] = m[0][0] * m[1][1] - m[0][1] * m[1][0];
}

static inline void mat_apply(const float m[3][3], float x, float y, float z, float *out) {
  out[0] = m[0][0] * x + m[0][1] * y + m[0][2] * z;
  out[1] = m[1][0] * x + m[1][1] * y + m[1][2] * z;
  out[2] = m[2][0] * x + m[2][1] * y + m[2][2] * z;
}

static float frand() { return (float) (random_uint32() & 0xFFFF) / 65535.0f; }

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------
void EliteClock::setup() {
  for (auto &s : this->stars_) {
    s.x = frand() * 2.0f - 1.0f;
    s.y = frand() * 2.0f - 1.0f;
    s.z = 0.05f + frand() * 0.95f;
  }
  this->set_ship(0, true);
  this->last_frame_ = millis();
}

void EliteClock::dump_config() {
  ESP_LOGCONFIG(TAG, "Elite Clock:");
  ESP_LOGCONFIG(TAG, "  Ships: %d", NUM_SHIPS);
  ESP_LOGCONFIG(TAG, "  Commander: %s", this->commander_.c_str());
}

void EliteClock::loop() {
  // Automatic ship cycling
  if (this->auto_cycle_ && this->cycle_ms_ > 0 && millis() - this->last_change_ >= this->cycle_ms_) {
    if (this->cycle_random_)
      this->random_ship();
    else
      this->next_ship();
  }
  // Long press on the touchscreen toggles the ship info line
  if (this->touching_ && !this->long_fired_ && millis() - this->touch_t0_ > 900) {
    int dx = this->touch_x_ - this->touch_x0_, dy = this->touch_y_ - this->touch_y0_;
    if (dx * dx + dy * dy < 25 * 25) {
      this->long_fired_ = true;
      this->show_info_ = !this->show_info_;
      ESP_LOGD(TAG, "Long press: ship info %s", this->show_info_ ? "on" : "off");
    }
  }
}

void EliteClock::set_ship(int index, bool animate) {
  if (index < 0)
    index = NUM_SHIPS - 1;
  if (index >= NUM_SHIPS)
    index = 0;
  this->ship_ = index;
  this->last_change_ = millis();
  this->prepare_ship_();
  // Start pointing towards the viewer, nose slightly down, like the title screen
  mat_identity(this->m_);
  mat_rotate(this->m_, 1, 3.14159f);  // face the camera
  mat_rotate(this->m_, 0, -0.35f);
  this->fly_t_ = animate ? 0.0f : 1.0f;
  this->fly_start_ = millis();
  ESP_LOGD(TAG, "Ship: %s", SHIPS[index].name);
}

bool EliteClock::set_ship_by_name(const std::string &name) {
  for (int i = 0; i < NUM_SHIPS; i++) {
    if (name == SHIPS[i].name) {
      if (i != this->ship_)
        this->set_ship(i);
      return true;
    }
  }
  return false;
}

void EliteClock::next_ship() { this->set_ship(this->ship_ + 1); }
void EliteClock::prev_ship() { this->set_ship(this->ship_ - 1); }
void EliteClock::random_ship() {
  int n = this->ship_;
  if (NUM_SHIPS > 1)
    while (n == this->ship_)
      n = random_uint32() % NUM_SHIPS;
  this->set_ship(n);
}

bool EliteClock::set_theme_by_name(const std::string &name) {
  for (int i = 0; i < NUM_THEMES; i++) {
    if (name == THEMES[i].name) {
      this->theme_ = i;
      return true;
    }
  }
  return false;
}

void EliteClock::show_message(const std::string &msg, float seconds) {
  this->message_ = msg;
  if (this->message_.size() > 38)
    this->message_.resize(38);
  this->message_until_ = millis() + (uint32_t) (seconds * 1000.0f);
}

void EliteClock::prepare_ship_() {
  const ShipBlueprint &s = SHIPS[this->ship_];
  // A point on each face: the average of every vertex on an edge bordering it
  int count[32] = {0};
  memset(this->face_pt_, 0, sizeof(this->face_pt_));
  for (int i = 0; i < s.num_edges; i++) {
    const ShipEdge &e = s.edges[i];
    const uint8_t faces[2] = {e.f1, e.f2};
    for (uint8_t f : faces) {
      if (f >= 32)
        continue;
      for (uint8_t vi : {e.v1, e.v2}) {
        this->face_pt_[f][0] += s.verts[vi].x;
        this->face_pt_[f][1] += s.verts[vi].y;
        this->face_pt_[f][2] += s.verts[vi].z;
        count[f]++;
      }
    }
  }
  for (int f = 0; f < 32; f++) {
    if (count[f]) {
      for (int k = 0; k < 3; k++)
        this->face_pt_[f][k] /= count[f];
    }
  }
  this->target_dist_ = s.radius * FOCAL / SHIP_SCREEN_RADIUS;
}

void EliteClock::update_motion_(float dt) {
  // Tumble: a steady pitch and roll, like the title-screen Cobra
  float k = this->rot_speed_ / 5.0f;
  mat_rotate(this->m_, 0, 0.55f * k * dt);
  mat_rotate(this->m_, 2, 0.30f * k * dt);
  mat_rotate(this->m_, 1, 0.12f * k * dt);
  if ((++this->frame_count_ & 31) == 0)
    mat_orthonormalise(this->m_);

  // Fly-in from the distance, easing out (time based, so slow frames don't stall it)
  if (this->fly_t_ < 1.0f) {
    this->fly_t_ = (millis() - this->fly_start_) / (FLY_IN_SECONDS * 1000.0f);
    if (this->fly_t_ > 1.0f)
      this->fly_t_ = 1.0f;
  }
  float u = 1.0f - this->fly_t_;
  float ease = u * u * u;
  this->dist_ = this->target_dist_ * (1.0f + 14.0f * ease);
}

void EliteClock::draw_stars_(display::Display &it, float dt) {
  const Theme &th = THEMES[this->theme_];
  const float speed = 0.06f * dt;  // gentle forward drift
  for (auto &s : this->stars_) {
    s.z -= speed;
    int sx = SHIP_CX + (int) (s.x / s.z * 60.0f);
    int sy = SHIP_CY + (int) (s.y / s.z * 60.0f);
    if (s.z <= 0.04f || sx < VIEW_X || sx >= VIEW_X + VIEW_W || sy < VIEW_Y || sy >= VIEW_Y + VIEW_H) {
      s.x = frand() * 2.0f - 1.0f;
      s.y = frand() * 2.0f - 1.0f;
      s.z = 0.6f + frand() * 0.4f;
      continue;
    }
    if (s.z < 0.25f)
      it.filled_rectangle(sx, sy, 2, 2, th.stars);
    else
      it.draw_pixel_at(sx, sy, th.stars);
  }
}

void EliteClock::draw_ship_(display::Display &it) {
  const ShipBlueprint &s = SHIPS[this->ship_];
  const Theme &th = THEMES[this->theme_];
  static int16_t px[64], py[64];
  static bool ok[64];

  // Transform and project the vertices
  for (int i = 0; i < s.num_verts && i < 64; i++) {
    float v[3];
    mat_apply(this->m_, s.verts[i].x, s.verts[i].y, s.verts[i].z, v);
    float z = v[2] + this->dist_;
    ok[i] = z > 1.0f;
    if (ok[i]) {
      px[i] = (int16_t) (SHIP_CX + v[0] * FOCAL / z);
      py[i] = (int16_t) (SHIP_CY - v[1] * FOCAL / z);
    }
  }

  // Back-face culling (Elite-style hidden line removal)
  uint32_t visible = 0xFFFFFFFF;
  bool cull = this->hidden_lines_ && s.hidden_lines;
  if (cull) {
    visible = 0;
    for (int f = 0; f < s.num_faces && f < 32; f++) {
      float n[3], p[3];
      mat_apply(this->m_, s.faces[f].x, s.faces[f].y, s.faces[f].z, n);
      mat_apply(this->m_, this->face_pt_[f][0], this->face_pt_[f][1], this->face_pt_[f][2], p);
      p[2] += this->dist_;
      // visible if the normal points back towards the camera at the origin
      if (n[0] * -p[0] + n[1] * -p[1] + n[2] * -p[2] > 0.0f)
        visible |= (1UL << f);
    }
  }

  it.start_clipping(display::Rect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H));
  for (int i = 0; i < s.num_edges; i++) {
    const ShipEdge &e = s.edges[i];
    if (e.v1 >= 64 || e.v2 >= 64 || !ok[e.v1] || !ok[e.v2])
      continue;
    if (cull) {
      bool v1 = e.f1 >= s.num_faces || (visible >> e.f1) & 1;
      bool v2 = e.f2 >= s.num_faces || (visible >> e.f2) & 1;
      if (!v1 && !v2)
        continue;
    }
    it.line(px[e.v1], py[e.v1], px[e.v2], py[e.v2], th.ship);
  }
  it.end_clipping();
}

void EliteClock::draw_text_(display::Display &it, int x, int y, const char *s, int scale, Color c) {
  const uint8_t *font = this->exec_font_ ? FONT_EXEC : FONT_BBC;
  for (; *s; s++, x += 8 * scale) {
    uint8_t ch = (uint8_t) *s;
    if (ch < 32 || ch > 127)
      ch = '?';
    if (ch == ' ')
      continue;
    const uint8_t *g = font + (ch - 32) * 8;
    for (int row = 0; row < 8; row++) {
      uint8_t bits = g[row];
      int col = 0;
      while (col < 8) {
        if (!(bits & (0x80 >> col))) {
          col++;
          continue;
        }
        int start = col;
        while (col < 8 && (bits & (0x80 >> col)))
          col++;
        // draw a horizontal run of lit pixels in one go
        if (scale == 1 && col - start == 1)
          it.draw_pixel_at(x + start, y + row, c);
        else
          it.filled_rectangle(x + start * scale, y + row * scale, (col - start) * scale, scale, c);
      }
    }
  }
}

void EliteClock::draw_text_centered_(display::Display &it, int cx, int y, const char *s, int scale, Color c) {
  this->draw_text_(it, cx - (int) strlen(s) * 4 * scale, y, s, scale, c);
}

void EliteClock::draw_text_right_(display::Display &it, int rx, int y, const char *s, int scale, Color c) {
  this->draw_text_(it, rx - (int) strlen(s) * 8 * scale, y, s, scale, c);
}

void EliteClock::info_line_(char *buf, size_t len) {
  const ShipBlueprint &s = SHIPS[this->ship_];
  // Rotate through the ship's stats every 4 seconds
  switch ((millis() / 4000) % 4) {
    case 0:
      snprintf(buf, len, "Bounty %u.%uCr", s.bounty / 10, s.bounty % 10);
      break;
    case 1:
      snprintf(buf, len, "Speed %u.%02uLM", s.speed / 100, s.speed % 100);
      break;
    case 2:
      snprintf(buf, len, "Energy %u", s.energy);
      break;
    default:
      if (s.laser == 0 && s.missiles == 0)
        snprintf(buf, len, "Unarmed");
      else
        snprintf(buf, len, "%s Laser M:%u", laser_name(s.laser), s.missiles);
      break;
  }
}

void EliteClock::draw(display::Display &it) {
  const Theme &th = THEMES[this->theme_];
  uint32_t now_ms = millis();
  float dt = (now_ms - this->last_frame_) / 1000.0f;
  if (dt > 0.25f)
    dt = 0.25f;
  this->last_frame_ = now_ms;
  this->fps_frames_++;
  if (now_ms - this->fps_t0_ >= 10000) {
    ESP_LOGD(TAG, "%.1f fps", this->fps_frames_ * 1000.0f / (now_ms - this->fps_t0_));
    this->fps_frames_ = 0;
    this->fps_t0_ = now_ms;
  }

  it.fill(Color::BLACK);
  this->update_motion_(dt);

  // ---- space view ----
  if (this->starfield_)
    this->draw_stars_(it, dt);
  this->draw_ship_(it);

  // header: Commander name, or the classic title if blank
  char buf[48];
  if (this->commander_.empty()) {
    snprintf(buf, sizeof(buf), "---- E L I T E ----");
  } else {
    snprintf(buf, sizeof(buf), "Commander %.28s", this->commander_.c_str());
  }
  this->draw_text_centered_(it, SCREEN_W / 2, 5, buf, 1, th.text);

  // ship name at the bottom of the space view
  this->draw_text_centered_(it, SCREEN_W / 2, DIVIDER_Y - 13, SHIPS[this->ship_].name, 1, th.accent);

  // flashing in-flight message
  if (!this->message_.empty()) {
    if ((int32_t) (this->message_until_ - now_ms) <= 0) {
      this->message_.clear();
    } else if ((now_ms / 500) % 2 == 0) {
      this->draw_text_centered_(it, SCREEN_W / 2, DIVIDER_Y - 30, this->message_.c_str(), 1, th.accent);
    }
  }

  // ---- frame ----
  it.rectangle(0, 0, SCREEN_W, SCREEN_H, th.frame);
  it.horizontal_line(0, DIVIDER_Y, SCREEN_W, th.frame);

  // ---- clock ----
  ESPTime t;
  bool valid = false;
  if (this->time_ != nullptr) {
    t = this->time_->now();
    valid = t.is_valid();
  }
  if (!valid && this->fallback_time_ != nullptr) {
    t = this->fallback_time_->now();
    valid = t.is_valid();
  }
  const int scale = 5;  // 40 px tall digits
  const int clock_y = DIVIDER_Y + 6;
  if (valid) {
    int hour = t.hour;
    const char *ampm = nullptr;
    if (!this->use_24h_) {
      ampm = hour < 12 ? "AM" : "PM";
      hour = hour % 12;
      if (hour == 0)
        hour = 12;
    }
    bool colon = (t.second % 2) == 0;
    if (this->use_24h_)
      snprintf(buf, sizeof(buf), "%02d%c%02d", hour, colon ? ':' : ' ', t.minute);
    else
      snprintf(buf, sizeof(buf), "%d%c%02d", hour, colon ? ':' : ' ', t.minute);
    int w = (int) strlen(buf) * 8 * scale;
    int ampm_w = ampm != nullptr ? 2 * 8 * 2 + 6 : 0;
    int x = (SCREEN_W - w - ampm_w) / 2;
    this->draw_text_(it, x, clock_y, buf, scale, th.text);
    if (ampm != nullptr)
      this->draw_text_(it, x + w + 6, clock_y + 8 * scale - 16, ampm, 2, th.text);
  } else {
    this->draw_text_centered_(it, SCREEN_W / 2, clock_y, "--:--", scale, th.text);
  }

  // bottom line: date on the left, ship stats on the right
  const int bottom_y = SCREEN_H - 11;
  if (this->show_date_ && valid) {
    size_t n = t.strftime(buf, sizeof(buf), "%a %d %b");
    buf[n] = 0;
    this->draw_text_(it, 6, bottom_y, buf, 1, th.text);
  }
  if (this->show_info_) {
    this->info_line_(buf, sizeof(buf));
    this->draw_text_right_(it, SCREEN_W - 6, bottom_y, buf, 1, th.accent);
  }
}

// ---------------------------------------------------------------------------
// Touch: tap = next ship, swipe left/right = next/previous,
//        long press = toggle ship info, any touch dismisses a message
// ---------------------------------------------------------------------------
void EliteClock::touch_start(int x, int y) {
  this->touching_ = true;
  this->long_fired_ = false;
  this->touch_x0_ = this->touch_x_ = x;
  this->touch_y0_ = this->touch_y_ = y;
  this->touch_t0_ = millis();
}

void EliteClock::touch_move(int x, int y) {
  this->touch_x_ = x;
  this->touch_y_ = y;
}

void EliteClock::touch_end() {
  if (!this->touching_)
    return;
  this->touching_ = false;
  if (this->long_fired_)
    return;
  if (!this->message_.empty()) {
    this->message_.clear();
    return;
  }
  int dx = this->touch_x_ - this->touch_x0_;
  int dy = this->touch_y_ - this->touch_y0_;
  if (dx > 50 && abs(dx) > abs(dy)) {
    this->prev_ship();  // swipe right -> previous
  } else if (dx < -50 && abs(dx) > abs(dy)) {
    this->next_ship();  // swipe left -> next
  } else if (dx * dx + dy * dy < 25 * 25) {
    this->next_ship();  // tap
  }
}

}  // namespace elite_clock
}  // namespace esphome
