# Caminho padrão para includes
include_path := -I ./includes/

# Flags de compilação
release_flags := -O3 -march=native
debug_flags   := -Wall -pedantic -Wextra -O0 -g

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
build: build/agentCLI.exe build/mazeEditor.exe build/agentViewer.exe

# --------------------------------------------------------------------
# Binário agentCLI (linha de comando)
# --------------------------------------------------------------------
build/agentCLI.exe: src/agentCLI.c includes/agent.h
	@echo ">>> Building agentCLI (linha de comando)"
	gcc $< $(include_path) $(build_flags) -o $@ -lcomdlg32

# --------------------------------------------------------------------
# Binário agentViwer (GUI)
# depende da biblioteca raygui
# --------------------------------------------------------------------
build/agentViewer.exe: src/agentViewer.c libs/raygui.a includes/agent.h
	@echo ">>> Building Agent Viewer (GUI)"
	gcc $< $(include_path) $(build_flags) -o $@ $(raylib) $(raygui) $(backend)

# --------------------------------------------------------------------
# Binário mazeEditor (GUI)
# depende da biblioteca raygui
# --------------------------------------------------------------------
build/mazeEditor.exe: src/mazeEditor.c libs/raygui.a includes/mazeRender.h
	@echo ">>> Building mazeEditor (GUI)"
	gcc $< $(include_path) $(build_flags) -o $@ $(raylib) $(raygui) $(backend)

# --------------------------------------------------------------------
# Biblioteca estática raygui
# --------------------------------------------------------------------
libs/raygui.a: src/raygui.c
	@echo ">>> Building raygui static library"
	gcc -c $< -DRAYGUI_IMPLEMENTATION -o build/raygui.o $(raylib) $(include_path)
	ar rcs $@ build/raygui.o
	rm build/raygui.o


# --------------------------------------------------------------------
# Limpeza
# --------------------------------------------------------------------
clean:
	@echo ">>> Cleaning build files"
	rm -f build/*.exe libs/raygui.a
