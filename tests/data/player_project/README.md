# Player test project

Used by the `player.*` tests (tests/CMakeLists.txt): a camera looking at a red box, a physics ball
that falls onto the floor, and a Lua script that lifts a green box at 1 m/s. `noor_player --frames
120 --report` must show the HUD (a green square at the top left, "Score: 3"), the ball on the floor, the green box 2 m higher, and red in the middle.
