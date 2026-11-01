#pragma once
#include <cstring>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::todo_list {

// One open item of a list. Fixed arrays, so a tile or card (made in PSRAM) holds them there. Home Assistant's answers
// to the app cut texts at 48 bytes, so 48 and its end fit.
struct Item {
  char summary[52];
  char uid[52];
  bool done;  // ticked off on the screen, on its way to Home Assistant
};
constexpr int MAX_ITEMS = 24;

// What reads the items of a list: a tile or a card. The plugin hands every answer of the app to each of them, and each
// keeps only the answer to its own question.
class Reader {
 public:
  virtual ~Reader() = default;
  virtual void answer(JsonObjectConst message) = 0;
};

// The plugin: a tile ("list"), a card with the whole list ("items") and a card with a keyboard ("add").
class TodoList : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;

  // Ask the app for the open items of a list; the number its answer carries, 0 when it was not sent.
  uint32_t ask(const std::string &entity);
  void listen(Reader *reader) { readers_.push_back(reader); }
  void forget(Reader *reader);
  // The list's name as the tile shows it, for the card the tile opens (a card's context has no name).
  const char *opened_name() const { return opened_name_; }
  void set_opened_name(const char *name) {
    strncpy(opened_name_, name, sizeof(opened_name_) - 1);
    opened_name_[sizeof(opened_name_) - 1] = '\0';
  }

 protected:
  char opened_name_[64] = "";
  std::vector<Reader *> readers_;
};

// The open items of `entity` in an answer of todo.get_items: how many, -1 when it failed.
int read_items(JsonObjectConst message, const std::string &entity, Item *items, int max);
// Tick an item off in Home Assistant, by its uid when that came whole, else by its summary.
bool complete(const std::string &entity, const Item &item);

}  // namespace esphome::todo_list
