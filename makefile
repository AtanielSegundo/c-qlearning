# COMPILATION MODES
release_flags := -O3 -march=native
debug_flags := -Wall -Wextra -O0 -g

# LIBRARYS
raylib := -I libs/raylib/include -L libs/raylib/lib -lraylib -lgdi32 -lwinmm

release: src/*
	# MAP EDITOR
	gcc ./src/mapEditor.c $(release_flags) -o build/mapEditor.exe $(raylib) -I ./includes/ -mwindows

debug: src/*
	# MAP EDITOR
	gcc ./src/mapEditor.c $(debug_flags) -o build/mapEditor.exe $(raylib) -I ./includes/ 