# NoorPong

The smallest real game in NoorEngine's samples, and one of Milestone 9's **yardsticks**: a 2D game that must stay
small, fast and simple however big the engine's worlds get ([M9 plan](../../docs/milestones/M9_PLAN.md), principle 0;
the numbers are in [M9_YARDSTICKS.md](../../docs/milestones/M9_YARDSTICKS.md)).

- **Play:** left bat **W / S**, right bat **Up / Down**. The computer plays a side until someone presses its keys.
- **How it's made:** an orthographic camera (`"projection": "orthographic"`, 20 m high) looking at boxes on a table, a
  pixel-art sprite for the ball, and one Lua script, `Scripts/Pong.luau`, that moves everything at the fixed 60 Hz
  step, so a game plays the same every time from the same seed.
- **Generated** by `tools/samples/make_noorpong.py` (the ball is drawn there pixel by pixel).

No terrain, no world grid, no streaming, nothing procedural: none of that is needed to make a game.
