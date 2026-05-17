# shadPS4 macOS Games Checklist

`Acquired` here means the game is currently present in the `Games` folder as a title-ID directory.

| Acquired | Game | Title ID | Details |
|---|---|---|---|
| [x] | EA Sports UFC | CUSA00264 | Tested. Runs, then hits the known render black-screen bug. Strict watchdog shows the black frame starts upstream of final presentation; next step is VideoOut compute input tracing for shader `0xc455a5aa2c447041`. |
| [x] | UFC 2 | CUSA01968 | not tested |
| [x] | Hasbro Family Fun Pack | CUSA03312 | Tested. Initially quit when entering fullscreen; after macOS permission/restart it ran further. Needs current retest for exact playable state. |
| [x] | Joe's Diner | CUSA03774 | Tested. Starts and accepts movement input from arrow keys/left stick, but expected keyboard button bindings such as X/C/Z did not work. Needs input mapping follow-up. |
| [x] | Yooka-Laylee | CUSA05721 | Tested. Boots into scene/menu path, but has severe flicker/distortion/missing geometry and did not advance from the "press X" prompt with keyboard X. |
| [x] | Tokyo Twilight Ghost Hunters Daybreak: Special Gigs | CUSA06045 | Tested. Boots and reaches in-game visual-novel scenes. Input combo prompt `R1 + Square` maps to keyboard `U + Z`; game appeared to work normally during the initial test. |
| [x] | UFC 3 | CUSA06534 | Tested. Reaches the loading percentage screen, then strict render validation aborts on an out-of-bounds image view request: `R16G16B16A16Uint` image has 1 layer but the view requests `base_layer=2`. |
| [x] | UFC 3 Patch v1.14 | CUSA06534-patch | Patch folder; not tested independently. |
| [x] | Beast Quest | CUSA09052 | Tested. Starts, plays audio/narrator, then reaches a black screen similar to the UFC black-screen bucket. Also showed startup flicker/distortion. |
| [x] | SEGA Mega Drive Classics | CUSA09771 | Tested. Reaches the "press button to continue" screen, then enters a black-screen state while audio/game logic continues. No strict render-validation assert was captured in this run; log is dominated by repeated metadata texture-read warnings and net stub spam. |
| [x] | The Witch and the Hundred Knight 2 | CUSA10135 | Retested after NGS2 hardening and AT9 waveform decoding/mixing. Previously crashed after logo/audio loading in `PhyreEngineWorkerThread` from an uninitialized/bogus audio metadata path. Now appears to work fine during manual play; audio works and the old crash was not reproduced. |
| [x] | EA Sports UFC 4 | CUSA14204 | Tested. Reaches the first loading screen, then aborts in Metal/MoltenVK texture-view validation: source texture is `MTLPixelFormatDepth16Unorm` but the requested view format is `MTLPixelFormatR16Uint`. |
| [x] | Zero Strain | CUSA18570 | Retested after macOS fixed-mapping relocation and host sidecar filtering. Previously exited in the fixed-address mapping bucket, then hit a macOS `.DS_Store` directory-iteration crash after relocation. Now gets past both and reaches playtime updates under watchdog, exiting only via timeout. Needs manual visual/audio playtest. |
| [x] | New Super Lucky's Tale | CUSA20302 | Tested. Reaches the start screen; when pressing a button to advance, strict render validation aborts on unsupported `B4G4R4A4UnormPack16` 2D image creation with sampled/color-attachment usage. |
| [x] | Race With Ryan Road Trip Deluxe Edition | CUSA23279 | Retested after macOS fixed-mapping relocation hardening. Previously crashed when `sceKernelMapNamedDirectMemory` requested `0x4000000000` inside the Rosetta/Metal reserved hole. Now the Apple relocation path is enabled by default and the precise relocated-pointer fault handler lets the game survive watchdog runs instead of crashing; timed test reached normal playtime updates and exited only via timeout. Needs manual visual/audio playtest. |
| [x] | Katamari Damacy Reroll | CUSA24361 | Tested. Appears to work/playable during manual play. Strict black-screen watchdog stayed nonblack and no GPU wait timeout or crash was observed. Log is noisy with repeated `Unexpected metadata read by a shader (texture)` warnings, but they do not currently block gameplay. |
| [x] | Stray | CUSA24899 | not tested |
| [x] | Teenage Mutant Ninja Turtles: Shredder's Revenge | CUSA30991 | Tested. Works well enough to reach startup, main menu, and gameplay after fixing the flexible-memory/`sceKernelMunmap(0, ...)` quit path. Startup music works; user observed no in-game audio yet. |
| [x] | Gigantosaurus: Dino Sports | CUSA43402 | Tested. Appears to work/playable during manual play, with occasional flicker. No GPU wait timeout, crash, or strict black-screen abort occurred. Watchdog first saw a very dark but nonblack frame (`avg_luma` around 7, `near_black` around 96%, nonblack pixels present), then later bright nonblack frames while Unity-style assets loaded and shaders compiled. Log is noisy with repeated `Unexpected metadata read by a shader (texture)` warnings, but they do not currently block gameplay. |
| [ ] | Another Sight | CUSA15308 | Tested, then removed from folder. Blocked by fixed mapping around `0x4000000000`, which overlaps the macOS x86_64-on-Apple-Silicon reserved address hole. |
| [ ] | Minecraft Dungeons | CUSA18797 | Tested, then removed from folder. Blocked by the same fixed `0x4000000000` mapping issue. |
| [ ] | Taxi Chaos | CUSA20527 | Tested, then removed from folder. Blocked by the same fixed `0x4000000000` mapping issue. |
| [ ] | Severed Steel | CUSA30139 | Tested, then removed from folder. Blocked by the same fixed `0x4000000000` mapping issue. |

## Approximate Size Reference

Sorted by approximate PS4 storage size, largest to smallest. Sizes are rough and can vary by region, patch, language packs, DLC, and disc vs. digital install.

| Acquired | Approx GB | Game |
|---|---:|---|
| [ ] | 175 | Call of Duty®: Modern Warfare® |
| [ ] | 110 | Gran Turismo® 7 |
| [ ] | 105 | Red Dead Redemption 2 |
| [ ] | 100 | Gran Turismo®SPORT |
| [ ] | 100 | Gran Turismo™Sport |
| [ ] | 100 | Call of Duty®: Black Ops III |
| [ ] | 100 | Call of Duty®: Black Ops III |
| [ ] | 100 | The Last of Us™ Part II |
| [ ] | 95 | Call of Duty®: Black Ops 4 |
| [ ] | 95 | Call of Duty®: Black Ops 4 |
| [ ] | 95 | Call of Duty®: Vanguard |
| [ ] | 90 | FINAL FANTASY VII REMAKE |
| [ ] | 90 | FINAL FANTASY VII REMAKE |
| [ ] | 90 | NBA 2K19 |
| [ ] | 90 | Battlefield™ V |
| [ ] | 86 | Grand Theft Auto V |
| [ ] | 86 | Grand Theft Auto V |
| [ ] | 80 | Call of Duty®: WWII |
| [ ] | 80 | FINAL FANTASY XV |
| [ ] | 80 | FINAL FANTASY XV |
| [ ] | 75 | Borderlands® 3 |
| [ ] | 70 | Marvel's Guardians of the Galaxy |
| [ ] | 70 | OUTRIDERS |
| [ ] | 65 | Marvel's Spider-Man |
| [ ] | 60 | Resident Evil 4 |
| [ ] | 60 | The Last of Us™ Remastered |
| [ ] | 60 | The Last of Us™ Remastered |
| [ ] | 55 | Star Wars Jedi: Fallen Order™ |
| [ ] | 55 | DEATH STRANDING |
| [ ] | 55 | Tiny Tina's Wonderlands |
| [ ] | 55 | Destiny |
| [ ] | 55 | Call of Duty®: Advanced Warfare |
| [ ] | 52 | Marvel's Spider-Man: Miles Morales |
| [ ] | 50 | Battlefield™ 1 |
| [ ] | 50 | Battlefield™ Hardline |
| [ ] | 50 | Ghost of Tsushima |
| [ ] | 50 | Uncharted™ 4: A Thief’s End |
| [ ] | 50 | Uncharted™ 4: A Thief’s End |
| [ ] | 50 | Uncharted 4: A Thief’s End™ |
| [ ] | 50 | Madden NFL 20 |
| [ ] | 50 | The Quarry |
| [ ] | 50 | The Witcher 3: Wild Hunt – Game of the Year Edition |
| [ ] | 50 | MLB® The Show™ 19 |
| [ ] | 45 | God of War |
| [ ] | 45 | God of War |
| [ ] | 45 | Horizon Zero Dawn™ |
| [ ] | 45 | Horizon Zero Dawn™ |
| [ ] | 45 | Horizon Zero Dawn™ |
| [ ] | 45 | Horizon Zero Dawn™ |
| [ ] | 45 | Uncharted: The Lost Legacy™ |
| [ ] | 45 | Uncharted: The Nathan Drake Collection™ |
| [ ] | 45 | Uncharted: The Nathan Drake Collection™ |
| [ ] | 45 | Uncharted: The Nathan Drake Collection™ |
| [ ] | 45 | Detroit: Become Human™ |
| [ ] | 45 | KINGDOM HEARTS III |
| [ ] | 45 | Madden NFL 19 |
| [ ] | 43 | NBA 2K14 |
| [ ] | 40 | Call of Duty®: Modern Warfare® Remastered |
| [ ] | 40 | Dishonored 2 |
| [ ] | 40 | Prey |
| [ ] | 40 | RESIDENT EVIL RESISTANCE |
| [ ] | 39 | KILLZONE™ SHADOW FALL |
| [ ] | 39 | KILLZONE™ SHADOW FALL |
| [ ] | 39 | KILLZONE™ SHADOW FALL |
| [ ] | 35 | Until Dawn™ |
| [ ] | 35 | Until Dawn™ |
| [ ] | 35 | South Park™: The Fractured But Whole™ |
| [ ] | 35 | HITMAN™ 2 |
| [ ] | 35 | Battlefield™ 1 |
| [ ] | 35 | Earth Defense Force 4.1: The Shadow of New Despair |
| [ ] | 32 | Crash Bandicoot™ 4: It’s About Time |
| [ ] | 30 | Madden NFL 16 |
| [ ] | 30 | Just Cause 3 |
| [ ] | 30 | Sniper Ghost Warrior 3 |
| [ ] | 30 | SCARLET NEXUS |
| [ ] | 30 | Spyro Reignited Trilogy |
| [ ] | 28 | MediEvil |
| [ ] | 28 | Concrete Genie |
| [ ] | 27 | Diablo III: Reaper of Souls – Ultimate Evil Edition |
| [ ] | 27 | Diablo III: Reaper of Souls – Ultimate Evil Edition |
| [ ] | 26 | Ratchet & Clank™ |
| [ ] | 26 | Ratchet & Clank™ |
| [ ] | 26 | RESIDENT EVIL 2 |
| [ ] | 25 | RESIDENT EVIL 7 biohazard |
| [ ] | 25 | RESIDENT EVIL 7 biohazard |
| [ ] | 25 | Borderlands®: Game of the Year Edition |
| [ ] | 25 | Stray |
| [ ] | 25 | Need for Speed™ |
| [ ] | 24 | Rise of the Tomb Raider |
| [ ] | 23 | LEFT ALIVE |
| [ ] | 22 | RESIDENT EVIL 3 |
| [ ] | 22 | Crysis®3 Remastered |
| [ ] | 22 | Redout 2 |
| [ ] | 22 | Sniper Elite 4 |
| [ ] | 21 | Tokyo 42 |
| [ ] | 21 | Battlefield™ Hardline |
| [ ] | 20 | Crysis® Remastered |
| [ ] | 20 | Crysis®2 Remastered |
| [ ] | 20 | DRIVECLUB™ |
| [ ] | 20 | DRIVECLUB™ |
| [ ] | 20 | EA SPORTS™ UFC® 2 |
| [ ] | 20 | The Outer Worlds |
| [ ] | 20 | SWORD ART ONLINE Alicization Lycoris |
| [ ] | 20 | Need for Speed™ Rivals |
| [ ] | 18 | Chicken Police |
| [ ] | 18 | SHADOW OF THE COLOSSUS™ |
| [ ] | 18 | SHADOW OF THE COLOSSUS™ |
| [ ] | 18 | Destiny |
| [ ] | 17 | Zero Strain |
| [ ] | 17 | Battlefield™ V |
| [ ] | 16 | ASTRO BOT Rescue Mission |
| [ ] | 16 | New Super Lucky's Tale |
| [X ] | 16 | The Amazing Spider-Man 2™ |
| [ X] | 16 | The Smurfs 2: The Prisoner of the Green Stone |
| [ ] | 15 | SpongeBob SquarePants: The Cosmic Shake |
| [ ] | 15 | Moss |
| [ ] | 15 | Gran Turismo®SPORT |
| [ ] | 15 | Gigantosaurus |
| [ ] | 15 | Race With Ryan |
| [ ] | 14 | Tearaway® Unfolded |
| [ ] | 14 | Crash Bandicoot N. Sane Trilogy |
| [ ] | 14 | WORLD OF FINAL FANTASY |
| [ ] | 14 | Biomutant |
| [ ] | 13 | YAKUZA 6: The Song of Life |
| [ ] | 13 | YAKUZA KIWAMI 2 |
| [ ] | 13 | Tony Hawk's™ Pro Skater™ 3 + 4 |
| [ ] | 13 | Tony Hawk's™ Pro Skater™ 1 + 2 |
| [X ] | 12 | ŌKAMI HD |
| [ ] | 12 | FINAL FANTASY XII THE ZODIAC AGE |
| [ ] | 12 | Katamari Damacy Reroll |
| [ ] | 12 | Project Highrise: Architect's Edition |
| [ ] | 12 | Resident Evil 4 |
| [X ] | 11 | The Witch and the Hundred Knight: Revival Edition |
| [ X] | 10 | SEGA Genesis Classics |
| [X ] | 10 | Hasbro Family Fun Pack |
| [X ] | 10 | Beast Quest |
| [ ] | 9 | FINAL FANTASY VI |
| [ ] | 9 | FINAL FANTASY V |
| [ ] | 9 | FINAL FANTASY IV |
| [ ] | 9 | FINAL FANTASY III |
| [ ] | 9 | FINAL FANTASY II |
| [ ] | 9 | FINAL FANTASY |
| [ X] | 8 | Yooka-Laylee |
| [X ] | 8 | Blair Witch |
| [X ] | 8 | Another Sight |
| [ X] | 7 | Severed Steel |
| [ ] | ? | Overwatch: Origins Edition |
| [ X] | 6 | Galak-Z |
| [ ] | 37.2 | KNACK 2 |
| [ ] | 36.5 | Fallout 4 |
| [ X] | 5 | Taxi Chaos |
| [ ] | ? | HORROR TALES: The Wine |
| [ X] | 4 | Minecraft Dungeons |
| [ ] | 37.2 | Call of Duty®: Modern Warfare® Remastered |
| [ X] | 3 | Teenage Mutant Ninja Turtles: Shredder's Revenge |
| [ ] | 94 | Call of Duty®: Black Ops 4 |
| [X ] | 1 | Joe's Diner |
