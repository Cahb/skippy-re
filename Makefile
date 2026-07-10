# Skippy/Ka'roo rewrite — C11, stdlib only, no external deps (yet).
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wshadow -O2 -Isrc
BIN     := build

.PHONY: all clean test test_jjm test_mdl test_thm game run win

all: $(BIN)/test_jjm $(BIN)/test_mdl $(BIN)/test_thm $(BIN)/test_tga $(BIN)/test_gam $(BIN)/test_level

$(BIN)/test_jjm: tests/test_jjm.c src/formats/jjm.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
$(BIN)/test_mdl: tests/test_mdl.c src/formats/mdl.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
$(BIN)/test_thm: tests/test_thm.c src/formats/thm.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
$(BIN):
	mkdir -p $(BIN)

LVL1 ?= ../game_root/SkippyAdventure/Levels/Forest/Start.jjm
MDL  ?= ../game_root/SkippyAdventure/Models/K.MDL
THM  ?= ../game_root/SkippyAdventure/Themes/Forest.thm

test_jjm: $(BIN)/test_jjm ; ./$(BIN)/test_jjm "$(LVL1)"
test_mdl: $(BIN)/test_mdl ; ./$(BIN)/test_mdl "$(MDL)"
test_thm: $(BIN)/test_thm ; ./$(BIN)/test_thm "$(THM)"
test: test_jjm test_mdl test_thm

clean: ; rm -rf $(BIN)

$(BIN)/test_tga: tests/test_tga.c src/formats/tga.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
TGA ?= ../game_root/SkippyAdventure/Textures/k_normal256.tga
test_tga: $(BIN)/test_tga ; ./$(BIN)/test_tga "$(TGA)"

$(BIN)/test_gam: tests/test_gam.c src/formats/gam.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
GAM ?= ../game_root/SkippyAdventure/JJ.GAM
test_gam: $(BIN)/test_gam ; ./$(BIN)/test_gam "$(GAM)"

$(BIN)/test_par: tests/test_par.c src/formats/par.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
PAR ?= ../game_root/SkippyAdventure/Models/forest/ausgang2.par
test_par: $(BIN)/test_par ; ./$(BIN)/test_par "$(PAR)"

$(BIN)/test_sav: tests/test_sav.c src/formats/sav.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
SAVDIR ?= ../game_root/SkippyAdventure/SavedGames
test_sav: $(BIN)/test_sav ; ./$(BIN)/test_sav "$(SAVDIR)"

$(BIN)/test_hsc: tests/test_hsc.c src/formats/hsc.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
HSCDIR ?= ../game_root/SkippyAdventure/highscores
test_hsc: $(BIN)/test_hsc ; ./$(BIN)/test_hsc "$(HSCDIR)"

FMT_ALL := src/formats/gam.c src/formats/jjm.c src/formats/thm.c src/formats/mdl.c src/formats/tga.c src/formats/asset.c
$(BIN)/test_level: tests/test_level.c $(FMT_ALL) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
test_level: $(BIN)/test_level ; ./$(BIN)/test_level

$(BIN)/test_ai: tests/test_ai.c src/sim/sim.c $(FMT_ALL) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ -lm
SABASE ?= ../game_root/SkippyAdventure
test_ai: $(BIN)/test_ai ; ./$(BIN)/test_ai "$(SABASE)"

# ---- game (M1: raylib render of Forest/Start) ----
RL_CFLAGS := $(shell pkg-config --cflags raylib)
RL_LIBS   := $(shell pkg-config --libs raylib) -lm -Wl,-rpath,/usr/local/lib
RENDER    := src/render/renderer_raylib.c
GAME_SRC  := src/main.c $(FMT_ALL) src/formats/leo.c src/formats/ani.c src/formats/par.c src/formats/jjs.c src/formats/cdt.c src/formats/sav.c src/formats/hsc.c src/sim/sim.c src/game/fx.c src/game/score.c src/game/menu.c src/game/options.c src/game/hud_text.c src/game/camera.c src/game/assets.c src/game/scene.c src/game/render_scene.c src/game/audio.c src/game/hud.c src/game/fx_emit.c src/game/mode.c $(RENDER)
$(BIN)/game: $(GAME_SRC) | $(BIN)
	$(CC) $(CFLAGS) $(RL_CFLAGS) -o $@ $^ $(RL_LIBS)
game: $(BIN)/game
BASE ?= ../game_root/EN
LEVEL ?=
run: $(BIN)/game ; ./$(BIN)/game "$(BASE)" $(LEVEL)

# ---- windows cross build (self-contained static .exe) ----
# Needs: gcc-mingw-w64-x86-64 + a MinGW-built libraylib.a, e.g.:
#   make -C ../raylib-6.0-src/src PLATFORM=PLATFORM_DESKTOP OS=Windows_NT \
#        CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar
MINGW      ?= x86_64-w64-mingw32
RAYLIB_WIN ?= ../raylib-6.0-src/src
WIN_LIBS   := -lraylib -lopengl32 -lgdi32 -lwinmm
$(BIN)/game.exe: $(GAME_SRC) | $(BIN)
	$(MINGW)-gcc $(CFLAGS) -I$(RAYLIB_WIN) -o $@ $^ -L$(RAYLIB_WIN) $(WIN_LIBS) -static -static-libgcc -lm
win: $(BIN)/game.exe
