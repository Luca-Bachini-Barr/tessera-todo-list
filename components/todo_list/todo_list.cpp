#include "todo_list.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace esphome::todo_list {

using tessera::Font;
namespace ui = tessera::ui;

static constexpr uint32_t ICON_PLUS = 0xF0415;
static constexpr uint32_t ICON_DONE = 0xF0133;    // checkbox-marked-circle
static constexpr uint32_t ICON_SHIFT = 0xF005D;   // arrow-up
static constexpr uint32_t ICON_BACK = 0xF004D;    // arrow-left, when the font has no ⌫
static constexpr uint32_t ICON_PREV = 0xF0141;    // chevron-left
static constexpr uint32_t ICON_NEXT = 0xF0142;    // chevron-right
static constexpr uint32_t ASK_EVERY_MS = 60000;   // a list changed elsewhere (the phone) shows within a minute

static void copy(char *to, size_t size, const char *from) {
  strncpy(to, from ? from : "", size - 1);
  to[size - 1] = '\0';
}

// ---- The items, asked of Home Assistant through the app ----

uint32_t TodoList::ask(const std::string &entity) {
  if (entity.empty()) return 0;
  JsonDocument request;
  request["ask"] = "call_service:todo.get_items";
  request["data"]["entity_id"] = entity;
  request["data"]["status"] = "needs_action";
  return tessera::send(this, request.as<JsonObjectConst>());
}

void TodoList::forget(Reader *reader) {
  readers_.erase(std::remove(readers_.begin(), readers_.end(), reader), readers_.end());
}

void TodoList::on_message(JsonObjectConst message) {
  // A copy: a reader may close its card, and so leave the list, while it reads.
  const std::vector<Reader *> readers = readers_;
  for (Reader *reader : readers)
    if (std::find(readers_.begin(), readers_.end(), reader) != readers_.end()) reader->answer(message);
}

// The answer: {"re", "ok", "result": {"todo.x": {"items": [{"summary", "uid", "status"}, ...]}}}.
int read_items(JsonObjectConst message, const std::string &entity, Item *items, int max) {
  if (!(message["ok"] | false)) return -1;
  JsonArrayConst list = message["result"][entity]["items"];
  int n = 0;
  for (JsonObjectConst item : list) {
    if (n >= max) break;
    const char *status = item["status"] | "needs_action";
    if (strcmp(status, "needs_action") != 0) continue;
    copy(items[n].summary, sizeof(items[n].summary), item["summary"] | "");
    copy(items[n].uid, sizeof(items[n].uid), item["uid"] | "");
    items[n].done = false;
    ++n;
  }
  return n;
}

bool complete(const std::string &entity, const Item &item) {
  JsonDocument fields;
  // A uid of 48 bytes or more may have been cut on its way (Microsoft To Do's are long): the summary finds it then.
  const size_t uid = strlen(item.uid);
  fields["item"] = uid > 0 && uid < 48 ? item.uid : item.summary;
  fields["status"] = "completed";
  return tessera::action("todo.update_item", entity, fields.as<JsonVariantConst>());
}

// ---- One line of a list: a ring to tick, its words ----

struct Line {
  lv_obj_t *ring{}, *check{}, *words{};

  void make(lv_obj_t *parent, Font font, int x, int y, int width, int height) {
    const int lh = ui::line_height(font);
    const int size = std::max(ui::px(12), lh * 4 / 5);
    ring = ui::block(parent, theme::CARD);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, std::max(1, ui::px(2)), 0);
    lv_obj_set_pos(ring, x, y + (height - size) / 2);
    check = ui::label(parent, Font::ICON_SMALL, theme::ACCENT);
    const int ch = ui::line_height(Font::ICON_SMALL);
    lv_obj_set_size(check, size + ui::px(8), ch);
    lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(check, x - ui::px(4), y + (height - ch) / 2);
    ui::set_text(check, ui::icon(ICON_DONE));
    words = ui::label(parent, font, theme::INK);
    const int indent = size + ui::px(10);
    lv_obj_set_size(words, std::max(1, width - indent), lh);
    lv_obj_set_pos(words, x + indent, y + (height - lh) / 2);
    recolor();
  }

  void recolor() {
    lv_obj_set_style_border_color(ring, ui::color(theme::MUTED), 0);
  }

  // An item, ticked or not; or nothing (the line hides).
  void paint(const Item *item) {
    const bool shown = item != nullptr, done = shown && item->done;
    show(ring, shown && !done);
    show(check, done);
    show(words, shown);
    if (!shown) return;
    ui::set_color(words, done ? theme::MUTED : theme::INK);
    ui::set_text(words, item->summary);
  }

  // A line of its own words, without a ring: "+3 more".
  void note(const std::string &text) {
    show(ring, false);
    show(check, false);
    show(words, true);
    ui::set_color(words, theme::MUTED);
    ui::set_text(words, text);
  }

  static void show(lv_obj_t *part, bool on) {
    if (on) lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
  }
};

// A round key with an icon: the tile's +.
static lv_obj_t *round_key(lv_obj_t *parent, int x, int y, int size, uint32_t icon, lv_obj_t **glyph) {
  lv_obj_t *key = ui::block(parent, theme::ACCENT);
  lv_obj_set_size(key, size, size);
  lv_obj_set_pos(key, x, y);
  lv_obj_set_style_radius(key, LV_RADIUS_CIRCLE, 0);
  *glyph = ui::label(key, Font::ICON_SMALL, theme::ON_ACCENT);
  const int gh = ui::line_height(Font::ICON_SMALL);
  lv_obj_set_size(*glyph, size, gh);
  lv_obj_set_pos(*glyph, 0, (size - gh) / 2);
  lv_obj_set_style_text_align(*glyph, LV_TEXT_ALIGN_CENTER, 0);
  ui::set_text(*glyph, ui::icon(icon));
  return key;
}

// What a list says while it has no items to show, by why.
enum class Why : uint8_t { NONE, CHOOSE, LOADING, FAILED, EMPTY };

static const char *why_key(Why why) {
  switch (why) {
    case Why::CHOOSE: return "choose";
    case Why::LOADING: return "loading";
    case Why::FAILED: return "failed";
    case Why::EMPTY: return "empty";
    default: return "";
  }
}

// What a tile and the list card share: the list's entity and name, its items, and asking for them.
class ListState : public Reader {
 public:
  explicit ListState(TodoList *plugin) : plugin_(plugin) { plugin_->listen(this); }
  ~ListState() override { plugin_->forget(this); }

  // The entity's state (the number of open items) or name changed: ask again. Also the first time.
  void entity_state(JsonObjectConst data) {
    const char *wait = data["wait"] | "";
    if (strcmp(wait, "wrong_entity") == 0) entity_.clear();
    const char *name = data["name"] | "";
    if (*name && !named_) copy(name_, sizeof(name_), name);
    const char *state = data["state"] | "";
    if (!entity_.empty() && (!asked_ || strcmp(state, state_) != 0)) {
      copy(state_, sizeof(state_), state);
      ask();
    }
  }

  void ask() {
    if (entity_.empty()) return;
    const uint32_t re = plugin_->ask(entity_);
    asked_at_ = millis();
    if (re) asked_ = re;
    else if (!asked_) failed_ = true;
  }

  // Once a second: ask again when the last answer is a minute old.
  void keep_fresh() {
    if (!entity_.empty() && asked_ && millis() - asked_at_ >= ASK_EVERY_MS) ask();
  }

  void answer(JsonObjectConst message) override {
    if (!asked_ || (message["re"] | 0u) != asked_) return;
    const int n = read_items(message, entity_, items_, MAX_ITEMS);
    answered_ = true;
    failed_ = n < 0;
    if (n >= 0) count_ = n;
    changed();
    tessera::refresh();
  }

  Why reason() const {
    if (entity_.empty()) return Why::CHOOSE;
    if (failed_ && !answered_) return Why::FAILED;
    if (!answered_) return Why::LOADING;
    if (failed_ && count_ == 0) return Why::FAILED;
    return count_ == 0 ? Why::EMPTY : Why::NONE;
  }

  // Tick item i off: it shows ticked at once, and leaves the list with Home Assistant's next answer.
  void tick(int i) {
    if (i < 0 || i >= count_ || items_[i].done) return;
    if (complete(entity_, items_[i])) items_[i].done = true;
  }

 protected:
  virtual void changed() = 0;

  TodoList *plugin_;
  std::string entity_;
  char name_[64] = "";
  char state_[16] = "";
  Item items_[MAX_ITEMS]{};
  int count_ = 0;
  uint32_t asked_ = 0, asked_at_ = 0;
  bool answered_ = false, failed_ = false;
  bool named_ = false;  // the tile has a name of its own, which stays over the entity's
};

// ---- The tile: the list's name, how many are open, the first items, and a + ----

class ListTile : public tessera::Tile, public ListState {
 public:
  explicit ListTile(TodoList *plugin) : ListState(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    entity_ = c.entity ? c.entity : "";
    copy(name_, sizeof(name_), c.name);
    named_ = name_[0] != '\0';
    font_ = ui::large() ? Font::BODY_LARGE : Font::BODY;

    head_ = std::max(ui::line_height(Font::TITLE), ui::mm(7));
    key_ = std::min(head_, ui::mm(9));
    plus_ = round_key(c.parent, width_ - key_, (head_ - key_) / 2, key_, ICON_PLUS, &plus_glyph_);
    count_label_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    lv_obj_set_pos(title_, 0, (head_ - ui::line_height(Font::TITLE)) / 2);

    const int top = head_ + ui::px(6);
    row_ = std::max(ui::line_height(font_) + ui::px(6), ui::mm(7));
    rows_ = std::max(0, std::min(MAX_ROWS, (height_ - top) / row_));
    for (int i = 0; i < rows_; ++i) lines_[i].make(c.parent, font_, 0, top + i * row_, width_, row_);
    top_ = top;
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(note_, width_);
    lv_obj_set_pos(note_, 0, top + std::max(0, (height_ - top - ui::line_height(Font::BODY)) / 2));
    paint();
  }

  void on_state(JsonObjectConst data) override { entity_state(data); paint(); }
  void on_tick(uint32_t) override { keep_fresh(); }

  void on_theme() override {
    for (int i = 0; i < rows_; ++i) lines_[i].recolor();
    ui::set_color(title_, theme::INK);
    ui::set_color(count_label_, theme::MUTED);
    ui::set_color(note_, theme::MUTED);
    ui::set_color(plus_glyph_, theme::ON_ACCENT);
    paint();
  }

  void on_tap() override {
    if (entity_.empty()) return;
    const tessera::Point p = tessera::tap_point();
    // The + and a finger's width around it: the keyboard.
    if (p.y >= 0 && p.y < head_ + ui::px(4) && p.x >= width_ - key_ - ui::mm(4)) {
      open_add();
      return;
    }
    // The ring of an item, and a finger's width beside it: tick it off.
    const int line = p.y >= top_ ? (p.y - top_) / row_ : -1;
    if (line >= 0 && line < shown_ && p.x <= row_ + ui::mm(3)) {
      tick(line);
      paint();
      return;
    }
    plugin_->set_opened_name(name_);
    tessera::open_card(plugin_->plugin_id(), "items", entity_, tile_, name_);
  }

 private:
  static constexpr int MAX_ROWS = 12;

  void changed() override { paint(); }

  void open_add() {
    tessera::open_card(plugin_->plugin_id(), "add", entity_, tile_,
                       tessera::fill(plugin_->text("add_to"), "list", name_));
  }

  void paint() {
    if (!title_) return;
    const Why why = reason();
    const std::string count = why == Why::NONE ? std::to_string(count_) : "";
    const int cw = count.empty() ? 0 : ui::text_width(count, Font::BODY_LARGE) + ui::px(4);
    lv_obj_set_size(count_label_, std::max(1, cw), ui::line_height(Font::BODY_LARGE));
    lv_obj_set_pos(count_label_, width_ - key_ - ui::px(8) - cw, (head_ - ui::line_height(Font::BODY_LARGE)) / 2);
    ui::set_text(count_label_, count);
    lv_obj_set_size(title_, std::max(1, width_ - key_ - cw - ui::px(14)), ui::line_height(Font::TITLE));
    ui::set_text(title_, name_);
    Line::show(plus_, !entity_.empty());

    ui::set_text(note_, plugin_->text(why_key(why)));
    Line::show(note_, why != Why::NONE);
    // The items that fit; the last line says how many more when they do not all fit.
    const int lines = why == Why::NONE ? rows_ : 0;
    const bool more = count_ > lines && lines > 0;
    shown_ = more ? lines - 1 : std::min(count_, lines);
    for (int i = 0; i < rows_; ++i) {
      if (i < shown_) lines_[i].paint(&items_[i]);
      else if (more && i == lines - 1) lines_[i].note(tessera::format(plugin_->text("more"), count_ - shown_));
      else lines_[i].paint(nullptr);
    }
  }

  int width_ = 0, height_ = 0, tile_ = -1, head_ = 0, key_ = 0, top_ = 0, row_ = 1, rows_ = 0, shown_ = 0;
  Font font_ = Font::BODY;
  Line lines_[MAX_ROWS];
  lv_obj_t *title_{}, *count_label_{}, *note_{}, *plus_{}, *plus_glyph_{};
};

// ---- The card: the whole list, a page at a time, with a key to add ----

class ItemsCard : public tessera::Card, public ListState {
 public:
  explicit ItemsCard(TodoList *plugin) : ListState(plugin) {}

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    entity_ = c.entity ? c.entity : "";

    // The key to add, at the top: a + and its words.
    head_ = std::max(ui::line_height(Font::BODY_LARGE) + ui::px(16), ui::mm(10));
    add_ = ui::block(c.parent, theme::ACCENT);
    const std::string words = plugin_->text("add_item");
    const int gh = ui::line_height(Font::ICON_SMALL), wh = ui::line_height(Font::BODY_LARGE);
    const int ww = ui::text_width(words, Font::BODY_LARGE) + ui::px(4);
    const int pad = ui::px(14), gw = gh;
    lv_obj_set_size(add_, std::min(width_, pad + gw + ui::px(8) + ww + pad), head_);
    add_glyph_ = ui::label(add_, Font::ICON_SMALL, theme::ON_ACCENT);
    lv_obj_set_size(add_glyph_, gw, gh);
    lv_obj_set_pos(add_glyph_, pad, (head_ - gh) / 2);
    ui::set_text(add_glyph_, ui::icon(ICON_PLUS));
    add_words_ = ui::label(add_, Font::BODY_LARGE, theme::ON_ACCENT);
    lv_obj_set_size(add_words_, ww, wh);
    lv_obj_set_pos(add_words_, pad + gw + ui::px(8), (head_ - wh) / 2);
    ui::set_text(add_words_, words);

    top_ = head_ + ui::px(10);
    row_ = std::max(ui::line_height(Font::BODY_LARGE) + ui::px(12), ui::mm(10));
    rows_ = std::max(1, std::min(MAX_ROWS, (height_ - top_) / row_));
    for (int i = 0; i < rows_; ++i) lines_[i].make(c.parent, Font::BODY_LARGE, 0, top_ + i * row_, width_, row_);

    // The last line pages when the list is longer than the card: ‹ 1 of 3 ›.
    const int py = top_ + (rows_ - 1) * row_, ih = ui::line_height(Font::ICON_SMALL);
    prev_ = ui::label(c.parent, Font::ICON_SMALL, theme::INK);
    next_ = ui::label(c.parent, Font::ICON_SMALL, theme::INK);
    lv_obj_set_size(prev_, ih * 2, ih);
    lv_obj_set_size(next_, ih * 2, ih);
    lv_obj_set_pos(prev_, 0, py + (row_ - ih) / 2);
    lv_obj_set_pos(next_, width_ - ih * 2, py + (row_ - ih) / 2);
    lv_obj_set_style_text_align(next_, LV_TEXT_ALIGN_RIGHT, 0);
    ui::set_text(prev_, ui::icon(ICON_PREV));
    ui::set_text(next_, ui::icon(ICON_NEXT));
    pager_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_size(pager_, width_ - ih * 4, ui::line_height(Font::BODY));
    lv_obj_set_pos(pager_, ih * 2, py + (row_ - ui::line_height(Font::BODY)) / 2);
    lv_obj_set_style_text_align(pager_, LV_TEXT_ALIGN_CENTER, 0);

    note_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_width(note_, width_);
    lv_obj_set_pos(note_, 0, top_);
    copy(name_, sizeof(name_), plugin_->opened_name());
    named_ = name_[0] != '\0';
    ask();
    paint();
  }

  void on_state(JsonObjectConst data) override { entity_state(data); }
  void on_tick(uint32_t) override { keep_fresh(); }

  void on_theme() override {
    for (int i = 0; i < rows_; ++i) lines_[i].recolor();
    ui::set_color(add_glyph_, theme::ON_ACCENT);
    ui::set_color(add_words_, theme::ON_ACCENT);
    ui::set_color(prev_, theme::INK);
    ui::set_color(next_, theme::INK);
    ui::set_color(pager_, theme::MUTED);
    ui::set_color(note_, theme::MUTED);
    paint();
  }

  void on_tap() override {
    if (entity_.empty()) return;
    const tessera::Point p = tessera::tap_point();
    if (p.y < 0) return;
    if (p.y < head_ + ui::px(5)) {
      if (p.x <= lv_obj_get_width(add_) + ui::mm(4))
        tessera::open_card(plugin_->plugin_id(), "add", entity_, tile_,
                           tessera::fill(plugin_->text("add_to"), "list", name_));
      return;
    }
    const int line = (p.y - top_) / row_;
    if (line < 0 || line >= rows_) return;
    if (paged() && line == rows_ - 1) {
      page_ += p.x < width_ / 2 ? -1 : 1;
      page_ = std::max(0, std::min(page_, pages() - 1));
      paint();
      return;
    }
    // A tap anywhere on an item's line ticks it off: the card's lines are wide and tall.
    const int i = page_ * per_page() + line;
    if (line < shown_) {
      tick(i);
      paint();
    }
  }

 private:
  static constexpr int MAX_ROWS = 14;

  void changed() override {
    page_ = std::max(0, std::min(page_, pages() - 1));
    paint();
  }

  bool paged() const { return count_ > rows_; }
  int per_page() const { return paged() ? std::max(1, rows_ - 1) : rows_; }
  int pages() const { return std::max(1, (count_ + per_page() - 1) / per_page()); }

  void paint() {
    if (!note_) return;
    const Why why = reason();
    ui::set_text(note_, plugin_->text(why_key(why)));
    Line::show(note_, why != Why::NONE);
    Line::show(add_, !entity_.empty());
    const int first = page_ * per_page();
    shown_ = why == Why::NONE ? std::max(0, std::min(per_page(), count_ - first)) : 0;
    for (int i = 0; i < rows_; ++i) lines_[i].paint(i < shown_ ? &items_[first + i] : nullptr);
    const bool pager = why == Why::NONE && paged();
    Line::show(prev_, pager && page_ > 0);
    Line::show(next_, pager && page_ < pages() - 1);
    Line::show(pager_, pager);
    if (pager) {
      const std::string text = tessera::fill(plugin_->text("page"), "page", std::to_string(page_ + 1));
      ui::set_text(pager_, tessera::fill(text.c_str(), "pages", std::to_string(pages())));
    }
  }

  int width_ = 0, height_ = 0, tile_ = -1, head_ = 0, top_ = 0, row_ = 1, rows_ = 0, shown_ = 0, page_ = 0;
  Line lines_[MAX_ROWS];
  lv_obj_t *add_{}, *add_glyph_{}, *add_words_{}, *prev_{}, *next_{}, *pager_{}, *note_{};
};

// ---- The card with the keyboard: type an item, Add sends it to the list ----

enum class Kind : uint8_t { CHAR, SHIFT, BACK, MODE, SPACE, ENTER };
struct KeyDef {
  const char *letters, *numbers;  // what the key types or shows on either layer
  uint8_t halves;                 // its width, in halves of a letter key: a row is 20
  Kind kind;
};
static constexpr int KEY_ROWS = 4;
static constexpr int KEYS_PER_ROW = 10;
// The same shape on both layers, so a layer only changes the words on the keys.
static const KeyDef KEYS[KEY_ROWS][KEYS_PER_ROW] = {
    {{"q", "1", 2, Kind::CHAR}, {"w", "2", 2, Kind::CHAR}, {"e", "3", 2, Kind::CHAR}, {"r", "4", 2, Kind::CHAR},
     {"t", "5", 2, Kind::CHAR}, {"y", "6", 2, Kind::CHAR}, {"u", "7", 2, Kind::CHAR}, {"i", "8", 2, Kind::CHAR},
     {"o", "9", 2, Kind::CHAR}, {"p", "0", 2, Kind::CHAR}},
    {{"a", "-", 2, Kind::CHAR}, {"s", "/", 2, Kind::CHAR}, {"d", ":", 2, Kind::CHAR}, {"f", ";", 2, Kind::CHAR},
     {"g", "(", 2, Kind::CHAR}, {"h", ")", 2, Kind::CHAR}, {"j", "£", 2, Kind::CHAR}, {"k", "&", 2, Kind::CHAR},
     {"l", "@", 2, Kind::CHAR}, {"'", "\"", 2, Kind::CHAR}},
    {{"", "#", 3, Kind::SHIFT}, {"z", "+", 2, Kind::CHAR}, {"x", "=", 2, Kind::CHAR}, {"c", "?", 2, Kind::CHAR},
     {"v", "!", 2, Kind::CHAR}, {"b", "*", 2, Kind::CHAR}, {"n", "%", 2, Kind::CHAR}, {"m", "_", 2, Kind::CHAR},
     {"", "", 3, Kind::BACK}, {nullptr, nullptr, 0, Kind::CHAR}},
    {{"", "", 3, Kind::MODE}, {",", ",", 2, Kind::CHAR}, {" ", " ", 10, Kind::SPACE}, {".", ".", 2, Kind::CHAR},
     {"", "", 3, Kind::ENTER}, {nullptr, nullptr, 0, Kind::CHAR}, {nullptr, nullptr, 0, Kind::CHAR},
     {nullptr, nullptr, 0, Kind::CHAR}, {nullptr, nullptr, 0, Kind::CHAR}, {nullptr, nullptr, 0, Kind::CHAR}},
};
static constexpr size_t MAX_TEXT = 120;

class AddCard : public tessera::Card {
 public:
  explicit AddCard(TodoList *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    entity_ = c.entity ? c.entity : "";
    const int gap = ui::px(ui::large() ? 8 : 4);

    // What is typed, in a field at the top, and a line under it that says what happened.
    field_h_ = std::max(ui::line_height(Font::TITLE) + ui::px(16), ui::mm(10));
    field_ = ui::block(c.parent, theme::RAISED);
    lv_obj_set_size(field_, width_, field_h_);
    typed_ = ui::label(field_, Font::TITLE, theme::INK);
    const int th = ui::line_height(Font::TITLE);
    field_pad_ = ui::px(12);
    lv_obj_set_size(typed_, width_ - 2 * field_pad_, th);
    lv_obj_set_pos(typed_, field_pad_, (field_h_ - th) / 2);
    status_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    const int sh = ui::line_height(Font::BODY);
    lv_obj_set_size(status_, width_, sh);
    lv_obj_set_pos(status_, 0, field_h_ + gap);

    // The keys, as large as the room under the field allows, never larger than a fingertip needs.
    const int kb_top = field_h_ + gap + sh + gap;
    const int key_h = std::max(ui::px(20), std::min(ui::mm(14), (height_ - kb_top - (KEY_ROWS - 1) * gap) / KEY_ROWS));
    const int kb_h = KEY_ROWS * key_h + (KEY_ROWS - 1) * gap;
    const int y0 = std::max(kb_top, height_ - kb_h);
    const float half = (width_ + gap) / 20.0f;
    back_text_ = ui::covers("⌫", Font::BODY_LARGE);
    for (int r = 0; r < KEY_ROWS; ++r) {
      int at = 0;
      for (int k = 0; k < KEYS_PER_ROW && KEYS[r][k].letters; ++k) {
        const KeyDef &def = KEYS[r][k];
        Key &key = keys_[r][k];
        key.x = static_cast<int>(at * half);
        key.w = static_cast<int>((at + def.halves) * half) - gap - key.x;
        key.y = y0 + r * (key_h + gap);
        key.h = key_h;
        at += def.halves;
        const bool accent = def.kind == Kind::ENTER;
        key.block = ui::block(c.parent, accent ? theme::ACCENT : theme::RAISED);
        lv_obj_set_size(key.block, key.w, key.h);
        lv_obj_set_pos(key.block, key.x, key.y);
        const bool icon = def.kind == Kind::SHIFT || (def.kind == Kind::BACK && !back_text_);
        key.font = icon ? Font::ICON_SMALL : Font::BODY_LARGE;
        key.label = ui::label(key.block, key.font, accent ? theme::ON_ACCENT : theme::INK);
        const int lh = ui::line_height(key.font);
        lv_obj_set_size(key.label, key.w, lh);
        lv_obj_set_pos(key.label, 0, (key.h - lh) / 2);
        lv_obj_set_style_text_align(key.label, LV_TEXT_ALIGN_CENTER, 0);
        key.used = true;
      }
    }
    shift_ = true;  // an item starts with a capital
    paint_keys();
    paint_text();
    ui::set_text(status_, entity_.empty() ? plugin_->text("choose") : plugin_->text("type_hint"));
  }

  void on_theme() override {
    for (auto &row : keys_)
      for (Key &key : row)
        if (key.used) ui::set_color(key.label, key.kind_accent ? theme::ON_ACCENT : theme::INK);
    ui::set_color(typed_, theme::INK);
    ui::set_color(status_, theme::MUTED);
    paint_keys();
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (p.y < 0) return;
    // The nearest key in the row under the finger, so a tap in a gap between two keys still counts.
    int row = -1;
    for (int r = 0; r < KEY_ROWS; ++r) {
      const Key &first = keys_[r][0];
      if (p.y >= first.y - ui::px(4) && p.y < first.y + first.h + ui::px(4)) { row = r; break; }
    }
    if (row < 0) return;
    const Key *hit = nullptr;
    int col = -1, best = INT32_MAX;
    for (int k = 0; k < KEYS_PER_ROW; ++k) {
      const Key &key = keys_[row][k];
      if (!key.used) continue;
      const int d = p.x < key.x ? key.x - p.x : p.x > key.x + key.w ? p.x - key.x - key.w : 0;
      if (d < best) { best = d; hit = &key; col = k; }
    }
    if (!hit) return;
    press(KEYS[row][col]);
  }

 private:
  struct Key {
    int x = 0, y = 0, w = 0, h = 0;
    lv_obj_t *block{}, *label{};
    Font font = Font::BODY_LARGE;
    bool used = false, kind_accent = false;
  };

  void press(const KeyDef &def) {
    switch (def.kind) {
      case Kind::CHAR:
      case Kind::SPACE: {
        if (entity_.empty() || text_len_ >= MAX_TEXT) return;
        std::string piece = numbers_ ? def.numbers : def.letters;
        if (shift_ && !numbers_ && piece.size() == 1 && piece[0] >= 'a' && piece[0] <= 'z') piece[0] -= 'a' - 'A';
        if (text_len_ + piece.size() > MAX_TEXT) return;
        memcpy(text_ + text_len_, piece.data(), piece.size());
        text_len_ += piece.size();
        text_[text_len_] = '\0';
        if (shift_ && !numbers_) shift_ = false;
        break;
      }
      case Kind::BACK:
        // Take off one character, all of its UTF-8 bytes.
        while (text_len_ > 0) {
          const unsigned char b = text_[--text_len_];
          if ((b & 0xC0) != 0x80) break;
        }
        text_[text_len_] = '\0';
        if (text_len_ == 0) shift_ = true;
        break;
      case Kind::SHIFT:
        if (numbers_) {
          press({"#", "#", 0, Kind::CHAR});
          return;
        }
        shift_ = !shift_;
        break;
      case Kind::MODE:
        numbers_ = !numbers_;
        break;
      case Kind::ENTER:
        send();
        break;
    }
    paint_keys();
    paint_text();
  }

  void send() {
    // Leading and trailing spaces are not part of the item.
    std::string item(text_, text_len_);
    const size_t a = item.find_first_not_of(' '), b = item.find_last_not_of(' ');
    if (a == std::string::npos || entity_.empty()) return;
    item = item.substr(a, b - a + 1);
    JsonDocument fields;
    fields["item"] = item;
    if (!tessera::action("todo.add_item", entity_, fields.as<JsonVariantConst>())) {
      ui::set_text(status_, plugin_->text("not_sent"));
      return;
    }
    ui::set_text(status_, tessera::fill(plugin_->text("added"), "item", item));
    text_len_ = 0;
    text_[0] = '\0';
    shift_ = true;
    numbers_ = false;
  }

  void paint_text() {
    // The end of what is typed, with a caret; the start gives way when it no longer fits.
    std::string shown = std::string(text_, text_len_) + "|";
    const int room = width_ - 2 * field_pad_;
    while (shown.size() > 1 && ui::text_width(shown, Font::TITLE) > room) {
      size_t cut = 1;
      while (cut < shown.size() && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80) ++cut;
      shown.erase(0, cut);
    }
    ui::set_text(typed_, shown);
  }

  void paint_keys() {
    for (int r = 0; r < KEY_ROWS; ++r) {
      for (int k = 0; k < KEYS_PER_ROW; ++k) {
        Key &key = keys_[r][k];
        if (!key.used) continue;
        const KeyDef &def = KEYS[r][k];
        std::string words;
        theme::Role fill = theme::RAISED, ink = theme::INK;
        switch (def.kind) {
          case Kind::CHAR:
            words = numbers_ ? def.numbers : def.letters;
            if (shift_ && !numbers_ && words.size() == 1 && words[0] >= 'a' && words[0] <= 'z') words[0] -= 'a' - 'A';
            break;
          case Kind::SPACE:
            words = plugin_->text("space");
            ink = theme::MUTED;
            break;
          case Kind::SHIFT:
            if (numbers_) {
              ui::set_font(key.label, Font::BODY_LARGE);
              words = def.numbers;
            } else {
              ui::set_font(key.label, Font::ICON_SMALL);
              words = ui::icon(ICON_SHIFT);
              if (shift_) { fill = theme::ACCENT; ink = theme::ON_ACCENT; }
            }
            lv_obj_set_height(key.label, ui::line_height(numbers_ ? Font::BODY_LARGE : Font::ICON_SMALL));
            lv_obj_set_pos(key.label, 0, (key.h - lv_obj_get_height(key.label)) / 2);
            break;
          case Kind::BACK:
            words = back_text_ ? std::string("⌫") : ui::icon(ICON_BACK);
            break;
          case Kind::MODE:
            words = plugin_->text(numbers_ ? "letters" : "numbers");
            break;
          case Kind::ENTER:
            words = plugin_->text("add");
            fill = theme::ACCENT;
            ink = theme::ON_ACCENT;
            break;
        }
        key.kind_accent = ink == theme::ON_ACCENT;
        lv_obj_set_style_bg_color(key.block, ui::color(fill), 0);
        ui::set_color(key.label, ink);
        ui::set_text(key.label, words);
      }
    }
  }

  TodoList *plugin_;
  std::string entity_;
  int width_ = 0, height_ = 0, field_h_ = 0, field_pad_ = 0;
  Key keys_[KEY_ROWS][KEYS_PER_ROW];
  lv_obj_t *field_{}, *typed_{}, *status_{};
  char text_[MAX_TEXT + 8] = "";
  size_t text_len_ = 0;
  bool shift_ = true, numbers_ = false, back_text_ = false;
};

// ---- The plugin ----

void TodoList::setup() {
  add_tile("list", [this]() { return new ListTile(this); });
  add_card("items", [this]() { return new ItemsCard(this); });
  add_card("add", [this]() { return new AddCard(this); }, true);
}

}  // namespace esphome::todo_list
