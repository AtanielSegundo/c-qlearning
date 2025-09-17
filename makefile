# Caminho padrão para includes
include_path := -I ./includes/

# Flags de compilação
release_flags := -O3 -march=native
debug_flags   := -Wall -Wextra -O0 -g

# Bibliotecas externas
raylib  := -I libs/raylib/include -L libs/raylib/lib -lraylib -lgdi32 -lwinmm
raygui  := -L libs/ -l:raygui.a

# Alvo padrão e backend
target  ?= _release
backend ?=

ifeq ($(target),_release)
	build_flags = $(release_flags)
	backend     = -mwindows
else
	build_flags = $(debug_flags)
endif

# --------------------------------------------------------------------
# Alvo principal: constrói tudo
# --------------------------------------------------------------------
build: build/agentCLI.exe build/mazeEditor.exe

# --------------------------------------------------------------------
# Binário agentCLI (linha de comando)
# --------------------------------------------------------------------
build/agentCLI.exe: src/agentCLI.c
	@echo ">>> Building agentCLI (linha de comando)"
	gcc $< $(include_path) $(build_flags) -o $@ -lcomdlg32

# --------------------------------------------------------------------
# Binário mazeEditor (interface gráfica)
# depende da biblioteca raygui
# --------------------------------------------------------------------
build/mazeEditor.exe: src/mazeEditor.c libs/raygui.a
	@echo ">>> Building mazeEditor (interface gráfica)"
	gcc $< $(include_path) $(build_flags) -o $@ $(raylib) $(raygui) $(backend)

# --------------------------------------------------------------------
# Biblioteca estática raygui
# --------------------------------------------------------------------
libs/raygui.a: src/raygui.c
	@echo ">>> Building raygui static library"
	gcc -c $< -DRAYGUI_IMPLEMENTATION -o raygui.o $(raylib) $(include_path)
	ar rcs $@ raygui.o

# --------------------------------------------------------------------
# Limpeza
# --------------------------------------------------------------------
clean:
	@echo ">>> Cleaning build files"
	rm -f build/*.exe raygui.o libs/raygui.a
