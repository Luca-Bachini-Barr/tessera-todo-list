# To-do list

A to-do list from Home Assistant on your Tessera screen: the open items on a tile, ticked off with a tap, and new items
added with a keyboard on the screen. It works with any to-do list Home Assistant has: Microsoft To Do, the Shopping
list, Todoist, Google Tasks, CalDAV, Local To-do.

## What it shows

- **The tile**: the list's name, how many items are open, and as many items as fit, each with a ring. Tap the ring to
  tick the item off. Tap the **+** to add an item. Tap anywhere else for the whole list.
- **The whole list**: every open item, a page at a time. Tap an item to tick it off, or **Add an item** at the top.
- **Add an item**: a keyboard across the screen. Type, then press **Add**. The keyboard stays open for the next item,
  and the line under the field says what was added. **123** switches to digits and signs.

The list is read again when its number of open items changes, and every minute while it is on the screen, so items
added on your phone show up within a minute.

## Set up

1. Make sure the screen may perform Home Assistant actions: in Home Assistant, Settings, Devices & services, ESPHome,
   the screen's **Configure**, turn on **Allow the device to perform Home Assistant actions**.
2. Add the plugin to a screen in Tessera. The screen builds once.
3. In Layout, place the **To-do list** tile from the library's Plugins group. A size of 2x2 or larger shows several
   items.
4. In the inspector, choose the to-do list (a `todo.` entity) for the tile. Give the tile a name if you want another
   one than the list's.

## Good to know

- Typing the same letter twice needs a short pause between the taps: the screen counts two taps on the same spot within
  0.6 seconds as one.
- Home Assistant sends the screen at most 48 bytes of each item's words, so a very long item is cut on the screen.
  Ticking off such an item may not work; tick it off in the app instead.
- Adding by voice works without this plugin: with a voice assistant on the screen, say "add milk to my shopping list".
