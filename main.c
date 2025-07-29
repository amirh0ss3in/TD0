#include "raylib.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 800
#define GRID_SIZE 10
#define cellWidth (SCREEN_WIDTH / GRID_SIZE)
#define cellHeight (SCREEN_HEIGHT / GRID_SIZE)
#define border_buff 10

// Color macros
#define COLOR_BLACK       (Color){0, 0, 0, 255}
#define COLOR_NEON_CYAN   (Color){0, 255, 255, 200}
#define COLOR_DARK_NEON   (Color){0, 80, 80, 255}

bool walls[GRID_SIZE][GRID_SIZE] = {0};

bool LoadMap(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed to open map file: %s\n", filename);
        return false;
    }

    // Check file size roughly: expect at least GRID_SIZE lines of GRID_SIZE chars + newlines
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (fileSize < GRID_SIZE * GRID_SIZE) {
        printf("Map file too small or malformed.\n");
        fclose(file);
        return false;
    }

    char line[GRID_SIZE + 2]; // +2 for newline and null terminator

    for (int y = 0; y < GRID_SIZE; y++) {
        if (fgets(line, sizeof(line), file) == NULL) {
            printf("Map file has fewer lines than expected.\n");
            fclose(file);
            return false;
        }

        if ((int)strlen(line) < GRID_SIZE) {
            printf("Line %d too short.\n", y+1);
            fclose(file);
            return false;
        }

        for (int x = 0; x < GRID_SIZE; x++) {
            if (line[x] == '1') {
                walls[x][y] = true;
            } else if (line[x] == '0') {
                walls[x][y] = false;
            } else {
                printf("Invalid character '%c' at line %d, col %d\n", line[x], y+1, x+1);
                fclose(file);
                return false;
            }
        }
    }

    fclose(file);
    return true;
}

void DrawWall(int cellX, int cellY) {
    float x = cellX * cellWidth;
    float y = cellY * cellHeight;

    // Draw inner fill
    DrawRectangleV((Vector2){x + border_buff, y + border_buff},
                   (Vector2){cellWidth - border_buff * 2, cellHeight - border_buff * 2},
                   COLOR_DARK_NEON);

    // Optional glowing border effect (simple example)
    for (int i = 0; i < 3; i++) {
        DrawRectangleLinesEx(
            (Rectangle){x - i, y - i, cellWidth + 2*i, cellHeight + 2*i},
            1,
            Fade(COLOR_NEON_CYAN, 0.4f - i * 0.1f)
        );
    }
}

void DrawWalls() {
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            if (walls[x][y]) {
                DrawWall(x, y);
            }
        }
    }
}

int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tron-Style Grid from File");
    SetTargetFPS(60);

    if (!LoadMap("map.txt")) {
        // Fill empty map on failure
        memset(walls, 0, sizeof(walls));
        printf("Using empty map.\n");
    }

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(COLOR_BLACK);

        // Draw grid lines
        for (int i = 0; i <= GRID_SIZE; i++) {
            float x = i * cellWidth;
            float y = i * cellHeight;

            DrawLineEx((Vector2){x, 0}, (Vector2){x, SCREEN_HEIGHT}, 2, COLOR_NEON_CYAN);
            DrawLineEx((Vector2){0, y}, (Vector2){SCREEN_WIDTH, y}, 2, COLOR_NEON_CYAN);
        }

        DrawWalls();

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
