#ifndef MAZE_GENERATION_H

#define MAZE_GENERATION_H


#include "mazeIR.h"

#include <time.h>

// Estrutura auxiliar para paredes
typedef struct {
    int r, c;
} Wall;

// Lista dinâmica simples para as paredes
typedef struct {
    Wall* data;
    size_t count;
    size_t capacity;
} WallList;

static void wallListInit(WallList* wl) {
    wl->count = 0;
    wl->capacity = 32;
    wl->data = (Wall*)malloc(wl->capacity * sizeof(Wall));
}

static void wallListPush(WallList* wl, int r, int c) {
    for (size_t i = 0; i < wl->count; i++) {
        if (wl->data[i].r == r && wl->data[i].c == c) return; // já existe
    }
    if (wl->count >= wl->capacity) {
        wl->capacity *= 2;
        wl->data = (Wall*)realloc(wl->data, wl->capacity * sizeof(Wall));
    }
    wl->data[wl->count++] = (Wall){r, c};
}

static void wallListRemove(WallList* wl, size_t idx) {
    if (idx < wl->count) {
        wl->data[idx] = wl->data[wl->count - 1];
        wl->count--;
    }
}

static int surroundingCells(MazeInternalRepr* m, int r, int c) {
    int s = 0;
    if (r > 0 && getCell(m, r - 1, c) == GRID_OPEN) s++;
    if (r < m->rows - 1 && getCell(m, r + 1, c) == GRID_OPEN) s++;
    if (c > 0 && getCell(m, r, c - 1) == GRID_OPEN) s++;
    if (c < m->cols - 1 && getCell(m, r, c + 1) == GRID_OPEN) s++;
    return s;
}

MazeInternalRepr generateMaze(size_t rows, size_t cols) {
    srand((unsigned)time(NULL));
    MazeInternalRepr m = newOpenMaze(rows, cols);

    // inicializa tudo como "não visitado" (2)
    for (size_t i = 0; i < rows * cols; i++) {
        m.grid[i] = 2;
    }

    // ponto inicial
    int sr = 1 + rand() % (rows - 2);
    int sc = 1 + rand() % (cols - 2);
    setCell(&m, sr, sc, GRID_OPEN);

    WallList walls;
    wallListInit(&walls);

    // adiciona paredes em volta do start
    wallListPush(&walls, sr - 1, sc);
    wallListPush(&walls, sr + 1, sc);
    wallListPush(&walls, sr, sc - 1);
    wallListPush(&walls, sr, sc + 1);

    while (walls.count > 0) {
        size_t idx = rand() % walls.count;
        Wall w = walls.data[idx];

        if (w.r <= 0 || w.r >= (int)rows - 1 || w.c <= 0 || w.c >= (int)cols - 1) {
            wallListRemove(&walls, idx);
            continue;
        }

        if (getCell(&m, w.r, w.c) == 2) {
            if (surroundingCells(&m, w.r, w.c) < 2) {
                setCell(&m, w.r, w.c, GRID_OPEN);

                wallListPush(&walls, w.r - 1, w.c);
                wallListPush(&walls, w.r + 1, w.c);
                wallListPush(&walls, w.r, w.c - 1);
                wallListPush(&walls, w.r, w.c + 1);
            }
        }

        wallListRemove(&walls, idx);
    }

    // transforma tudo que ainda é "não visitado (2)" em parede
    for (size_t i = 0; i < rows * cols; i++) {
        if (m.grid[i] == 2) m.grid[i] = GRID_WALL;
    }

    // entrada e saída
    for (size_t j = 0; j < cols; j++) {
        if (getCell(&m, 1, j) == GRID_OPEN) {
            setCell(&m, 0, j, GRID_OPEN);
            break;
        }
    }
    for (size_t j = cols - 1; j > 0; j--) {
        if (getCell(&m, rows - 2, j) == GRID_OPEN) {
            setCell(&m, rows - 1, j, GRID_OPEN);
            break;
        }
    }

    free(walls.data);
    return m;
}

#endif