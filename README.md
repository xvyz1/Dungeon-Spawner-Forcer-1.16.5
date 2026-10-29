# DungeonSpawnerForcer1.16.5

Finds chunks where up to 8 dungeon spawners of one mob (notably 8 skeletons) can be forced to generate and activate from one spot, and writes the commands that build them, on Minecraft Java 1.16.5. Vibecoded.

[VIDEO](https://www.youtube.com/watch?v=XXXXXXXXXXX)

- A chunk in 1.16.5 makes 8 dungeon attempts, each one a spawner, so 8 is the most one chunk can hold. Using several chunks you can get about 14 activated with 1 player. However, using 6-9 chunks to attain 14 spanners is a lot harder than using one chunk. Also, 8 of one type is much easier to set up and fairly common.
- It is a good early to mid game farm before a higher end wither skeleton or general mob farm.
- The results are about 95% accurate.
- The commands build the terrain around the chunk (the chunk itself is regenerated), then you reset the chunk with the DungeonSpawnerForcer1.16.5-Reset fabric mod (hold a book and quill and type `.s`), which makes the game generate it again with the planned dungeons. The text file has the steps with the coordinates filled in for every result.
- The air the command clears is only for visual purposes: you only have to dig out a small area around wherever you do build the temporary blocks, to get the spawners to generate.
- Only works on 1.16.5. To avoid downgrading your world, do it all in a separate world of the same seed and copy the chunk over with MCA Selector after generating the spawners and upgrading the world to your version.
- RECOMMENDED PARAMETERS: skeleton, 8 spawners, 25000 radius. REMEMBER TO CHECK IF IT EXISTS BEFOREHAND, not guaranteed.

Any ideas/issues, DM me xvyz#4060

# Run
Run `DungeonSpawnerForcer1.16.5.exe` (Windows), or build it from the source for Linux and macOS (see Build). One file, nothing to install. `DungeonSpawnerForcer1.16.5-Reset.jar`, next to it, is the Fabric mod that resets the chunk (see How to build a result).

# Version
1.16.5 (dungeons, mineshafts, terrain, caves and lakes of that version). It only works with the seed of a 1.16.5 world. Other versions are not supported.

# Input parameters
**Numeric world seed** - the world seed in numeric form, leave empty for 0  
**Mob** - `1` SKELETON, `2` ZOMBIE, `3` SPIDER  
**Spawners of that mob in range of one spot** - 2 to 8, leave empty for 8. A spawner is in range when the standing spot is less than 16 blocks from its centre  
**Area type** - `1` SQUARE or `2` RECTANGLE, centered on the search center  
**Square radius in blocks** - 1 to 30,000,000; for a rectangle instead the width (X) and length (Z) in blocks. Every chunk whose middle is inside the area is searched  
**Search center X** and **Search center Z** - leave empty for 0  
**Number of results to show** - leave empty for 20  
**Number of threads** - leave empty to use every thread the computer has; the results are the same for any number  
**Build commands output** - `1` YES or `2` NO (leave empty for NO). With YES the commands that build every result are written to a text file named after the seed, the mob and the number of spawners, for example `48_SKELETON_8.txt` (a number is added if that file already exists), next to where the program is run  

An invalid value is simply asked for again. Inputs can also be piped in, one per line, in the order above. `DungeonSpawnerForcer1.16.5.exe selftest` re-computes fixed results and compares them with stored ones.

# Output
Every candidate of the search is checked on the built-in terrain model of 1.16.5 (noise terrain, caves, ravines, lakes and mineshafts), and the buildable ones are printed in the command window, as many as you asked for, closest to the search center first. Candidates in ocean chunks are not checked; their number is printed.

Each result gives the chunk, the standing spot (`/tp @p x.5 y.5 z.5`: your feet must be at that exact height, a plain `/tp @p x y z` puts them half a block lower and can lose a spawner), the number in range and the position of every dungeon spawner.

With build commands output on, the text file holds for every result the same lines plus the planned dungeons, the steps, and the command (one command block holds it; if it is longer than 30,000 characters it is split into several), separated by a line of `=`. The steps are the ones in How to build a result.

# How to build a result
Do this in a creative world of the same seed on 1.16.5, with Fabric and `DungeonSpawnerForcer1.16.5-Reset.jar` (next to the exe) in the `mods` folder. The jar adds the `.s` chat command: hold a book and quill and type it to reset the chunk you are standing in. With build commands output on, the text file has these steps for every result, with the coordinates filled in:
1. Teleport to the spot next to the chunk (`/tp @p x y z`).
2. Place a command block at the given position (it needs a solid block under it), paste the command into it and activate it, not from the top. If the command is split in several parts, paste them one after the other. It clears the area around the chunk and builds the floors, roofs, walls and openings of the planned dungeons outside the chunk (the chunk itself is regenerated, so nothing is built inside it). The amount of air it clears is only for visual purposes: you only have to dig out a small area at the bottom, near wherever you do it, to get the dungeons to generate.
3. Go to the top of the chunk (`/tp @p x 255 z`), hold a book and quill, and type `.s`. This makes the game save the chunk wrong so the server generates it again, now with the planned dungeons.
4. Teleport away to unload the chunk, or relog.
5. Remove the spawners of the other mobs listed in the file (they are planned on purpose to steer the others, and are outside the range of the standing spot), and remove any exposed water and lava in and around the chunk.
6. Teleport to the center (`/tp @p x.5 y.5 z.5`), the last number in the file (feet at that exact height, a plain `/tp @p x y z` puts you half a block lower and can lose a spawner). All spawners in range of that spot run.

# How it works
**Attempts.** Every chunk makes 8 dungeon attempts from the world seed alone: a position, a height and a room size each. An attempt becomes a dungeon when the floor and the roof of the room are solid and its wall ring has one to five openings.  
**Steering.** A successful attempt consumes more random numbers (one per floor block that has a solid block below it, then the chests, then the mob), which moves the later attempts and decides the mob. The floor blocks outside the chunk are free, so their number is the knob that steers the later attempts and the mob type. The search follows every combination of successes and mobs, exactly as the game does.  
**Building.** The building is done outside the chunk (the chunk is regenerated, so anything inside it is lost): floors, roofs, walls, an opening for each room and the extra floor blocks. The plan is checked by replaying the dungeon feature on the planned blocks and the chunk; the commands are checked by applying them to a block grid and replaying again. A layout is only written when both give exactly the planned dungeons.  
**Terrain model.** The chunk is generated from the seed: the noise terrain with the biome blend (biomes from Cubiomes), then the cave and ravine carvers of the neighbouring chunks, then the water and lava lakes of the chunk and the three chunks next to it, as the game does. The layout of the mineshafts around the chunk is generated too; a dungeon attempt whose room touches a mineshaft, or a lake attempt that touches an ocean chunk, or a cave that reaches the bedrock rows (y 1-4), is dropped, because those blocks are not placed. Not modelled: ocean chunks (their underwater carvers), underground springs, strongholds and villages.  
**Spot.** The standing spot is the point (on a block grid, feet half a block above) that has the most of the chosen mob within 16 blocks of the spawner centres.

# Accuracy
The dungeon search is a port of the DungeonForcer mod's search (0 differences against the mod on 625 chunks with all 8 search types, measured on an earlier build of this code; the lists of chunks and layouts of this build equal that build's). The command written for chunk 694 188 of seed 48 (8 skeletons) is the one that was built in game, and the game generated exactly the 8 planned skeletons; another chunk (-11 -11, seed 48) produced exactly the 7 planned dungeons.  
The terrain model was compared with the real chunks of a 1.16.5 server on seed 48: 99.8% of the cells agree (the differences left are mostly trees and plants above the ground), and 77% of the land chunks are identical below y 50. For 8 skeletons in a 48,000 x 48,000 block area it and the real chunks agree on all 12 candidates on land (2 of them can be built, with the same plans). For 7 skeletons in a 28,000 x 28,000 block area, 19 chunks are buildable on both with identical plans and no land chunk is accepted by the model that the real terrain does not allow; one chunk that is buildable on real terrain, next to an ocean, is rejected by the model. For 6 skeletons in a 12,000 x 12,000 block area, 6 are buildable on both with identical plans, none is missed and none wrongly accepted on land. Together: 27 of 27 reported land chunks were right (100%) and 27 of 28 buildable land chunks were found (96%). Candidates in ocean chunks are not checked.  
Where it can go wrong: springs, strongholds, villages and anything near an ocean are not modelled, and only seed 48 was compared. Test a built chunk in the game before relying on it.

# Speed
Eight skeletons: about 1.5 million chunks per second on a 16-thread processor (a 48,000 x 48,000 block area, 9 million chunks, takes about 4-6 seconds). Fewer spawners are slower because many more combinations are possible: 7 about 40,000 chunks per second, 6 about 10,000, 5 and fewer a few thousand or less. Checking one candidate on the terrain model takes about 10 ms.

# Build
Windows: run `build.bat` from a Visual Studio Developer Command Prompt (or download `DungeonSpawnerForcer1.16.5.exe` from Releases).

Linux and macOS (gcc or clang, 64-bit, including Apple Silicon):
```
gcc -o DungeonSpawnerForcer1.16.5 -std=gnu11 -O2 -ffp-contract=off ./src/*.c ./src/*/*.c -lm -lpthread
```
`compile.txt` has this and a MinGW command. `-ffp-contract=off` keeps the floating point math the same as the game's, which the terrain model needs. Run `DungeonSpawnerForcer1.16.5 selftest` once on a new system; it prints `selftest: PASSED` (the terrain model line compares the generated blocks of four chunks with stored ones). Windows (MSVC) is what has been run so far; the Linux and macOS builds have not been run on those systems. Sources are in `src`.

# Credits
Biome generation: [Cubiomes](https://github.com/Cubitect/cubiomes) by Cubitect (MIT license, see `src/cubiomes/LICENSE`); only the parts needed for 1.16 overworld biomes are included.  
Gaider10 and andrew_10_: the chunk reset dungeon forcing this is based on (the DungeonForcer and BookDupe mods are by andrew_10_).
