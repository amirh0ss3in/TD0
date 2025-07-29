#include "raylib.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// --- Game Constants ---
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 800
#define GRID_SIZE 10
#define cellWidth (SCREEN_WIDTH / GRID_SIZE)
#define cellHeight (SCREEN_HEIGHT / GRID_SIZE)
#define border_buff 10

// --- Color Macros ---
#define COLOR_BLACK       (Color){0, 0, 0, 255}
#define COLOR_NEON_CYAN   (Color){0, 255, 255, 200}
#define COLOR_DARK_NEON   (Color){0, 80, 80, 255}
#define COLOR_NEON_RED    (Color){255, 0, 100, 255}

// --- Data Structures ---
typedef struct {
    Vector2 pos; // Grid coordinates (x, y)
} Enemy;

// --- Global Variables ---
bool walls[GRID_SIZE][GRID_SIZE] = {0}; // 2D array representing the map's walls
Enemy enemy;                           // The enemy object

// Pathfinding & Movement
Vector2 path[GRID_SIZE * GRID_SIZE]; // Stores the calculated path for the enemy
int pathLength = 0;                  // How many steps are in the path
int currentPathIndex = 0;            // The enemy's current position in the path array
float moveTimer = 0.0f;              // Timer to control enemy movement speed
const float MOVE_INTERVAL = 0.25f;   // Time in seconds between enemy moves

/**
 * Loads the map from a file, identifies 's' (start) and 'f' (finish) points,
 * and populates the global walls array.
 * @param filename The name of the map file.
 * @param startPos A pointer to a Vector2 that will be filled with the start coordinates.
 * @param endPos A pointer to a Vector2 that will be filled with the finish coordinates.
 * @return True on success, false on failure.
 */
bool LoadMap(const char *filename, Vector2 *startPos, Vector2 *endPos) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed to open map file: %s\n", filename);
        return false;
    }

    // Initialize positions to an invalid state to check if they are found
    startPos->x = -1; startPos->y = -1;
    endPos->x = -1; endPos->y = -1;
    char line[GRID_SIZE + 2]; // +2 for newline and null terminator

    for (int y = 0; y < GRID_SIZE; y++) {
        if (fgets(line, sizeof(line), file) == NULL) {
            printf("Map file has fewer lines than expected.\n");
            fclose(file);
            return false;
        }
        if (strlen(line) < GRID_SIZE) {
            printf("Line %d too short.\n", y+1);
            fclose(file);
            return false;
        }

        for (int x = 0; x < GRID_SIZE; x++) {
            char c = line[x];
            if (c == '1') {
                walls[x][y] = true;
            } else {
                walls[x][y] = false; // 's', 'f', and '0' are all walkable spaces
                if (c == 's') {
                    if (startPos->x != -1) { // Check if 's' was already found
                        printf("Error: Multiple start points ('s') found in map.\n");
                        fclose(file);
                        return false;
                    }
                    *startPos = (Vector2){(float)x, (float)y};
                } else if (c == 'f') {
                    if (endPos->x != -1) { // Check if 'f' was already found
                        printf("Error: Multiple finish points ('f') found in map.\n");
                        fclose(file);
                        return false;
                    }
                    *endPos = (Vector2){(float)x, (float)y};
                }
            }
        }
    }
    
    fclose(file);

    // Final validation after reading the whole file
    if (startPos->x == -1) {
        printf("Error: No start point ('s') found in map.\n");
        return false;
    }
    if (endPos->x == -1) {
        printf("Error: No finish point ('f') found in map.\n");
        return false;
    }
    return true;
}

/**
 * Finds the shortest path between two points on the grid using Breadth-First Search (BFS).
 * @param start The starting grid coordinate.
 * @param end The ending grid coordinate.
 * @return True if a path is found, false otherwise.
 */
bool FindPathBFS(Vector2 start, Vector2 end) {
    // Data structures for BFS algorithm
    Vector2 queue[GRID_SIZE * GRID_SIZE];
    int head = 0, tail = 0;
    Vector2 parent[GRID_SIZE][GRID_SIZE];
    bool visited[GRID_SIZE][GRID_SIZE] = {0};

    // Movement directions: Up, Down, Left, Right
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    if (walls[(int)start.x][(int)start.y] || walls[(int)end.x][(int)end.y]) return false;
    
    // Initialize the search with the start node
    queue[tail++] = start;
    visited[(int)start.x][(int)start.y] = true;
    parent[(int)start.x][(int)start.y] = (Vector2){-1, -1}; // Start node has no parent

    bool pathFound = false;
    while (head < tail) {
        Vector2 current = queue[head++]; // Dequeue the next node to visit

        // If we've reached the end, stop searching
        if (current.x == end.x && current.y == end.y) {
            pathFound = true;
            break;
        }

        // Explore neighbors
        for (int i = 0; i < 4; i++) {
            int nextX = current.x + dx[i];
            int nextY = current.y + dy[i];

            // Check if the neighbor is valid (within bounds, not a wall, and not visited)
            if (nextX >= 0 && nextX < GRID_SIZE && nextY >= 0 && nextY < GRID_SIZE) {
                if (!visited[nextX][nextY] && !walls[nextX][nextY]) {
                    visited[nextX][nextY] = true;
                    parent[nextX][nextY] = current;
                    queue[tail++] = (Vector2){(float)nextX, (float)nextY}; // Enqueue the neighbor
                }
            }
        }
    }
    
    if (!pathFound) return false;

    // Reconstruct the path by backtracking from the end node using the parent array
    pathLength = 0;
    Vector2 current = end;
    while (current.x != -1) {
        path[pathLength++] = current;
        current = parent[(int)current.x][(int)current.y];
    }
    
    // The path is currently from end-to-start, so we must reverse it
    for (int i = 0; i < pathLength / 2; i++) {
        Vector2 temp = path[i];
        path[i] = path[pathLength - 1 - i];
        path[pathLength - 1 - i] = temp;
    }
    return true;
}

/**
 * Updates the enemy's position based on the calculated path and a timer.
 * @param dt Delta time (time since last frame) for frame-rate independent movement.
 */
void UpdateEnemy(float dt) {
    // Do nothing if there's no path or the enemy has reached the end
    if (pathLength == 0 || currentPathIndex >= pathLength - 1) return; 

    moveTimer += dt;
    if (moveTimer >= MOVE_INTERVAL) {
        moveTimer = 0; // Reset timer
        currentPathIndex++;
        enemy.pos = path[currentPathIndex];
    }
}

// --- Drawing Functions ---

void DrawWall(int cellX, int cellY) {
    float x = cellX * cellWidth;
    float y = cellY * cellHeight;
    DrawRectangleV((Vector2){x + border_buff, y + border_buff},
                   (Vector2){cellWidth - border_buff * 2, cellHeight - border_buff * 2},
                   COLOR_DARK_NEON);
    // Draw a simple glowing effect
    for (int i = 0; i < 3; i++) {
        DrawRectangleLinesEx(
            (Rectangle){x - i, y - i, cellWidth + 2*i, cellHeight + 2*i},
            1, Fade(COLOR_NEON_CYAN, 0.4f - i * 0.1f));
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

void DrawEnemy() {
    if (pathLength == 0) return;
    float screenX = enemy.pos.x * cellWidth + cellWidth / 2;
    float screenY = enemy.pos.y * cellHeight + cellHeight / 2;
    DrawCircleV((Vector2){screenX, screenY}, cellWidth / 3, COLOR_NEON_RED);
}


// --- Main Entry Point ---
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tron-Style Enemy Pathfinding");
    SetTargetFPS(60);

    // --- Initialization ---
    Vector2 startPos, endPos;
    if (!LoadMap("map.txt", &startPos, &endPos)) {
        CloseWindow();
        return 1; // Exit if map loading fails
    }

    if (FindPathBFS(startPos, endPos)) {
        enemy.pos = path[0];
        currentPathIndex = 0;
        printf("Path found! Length: %d steps.\n", pathLength);
    } else {
        enemy.pos = (Vector2){-1, -1}; // Place enemy off-screen if no path found
        printf("Pathfinding failed between the start and finish points.\n");
    }

    // --- Main Game Loop ---
    while (!WindowShouldClose()) {
        // --- Update Step ---
        UpdateEnemy(GetFrameTime());

        // --- Draw Step ---
        BeginDrawing();
        ClearBackground(COLOR_BLACK);

        // Draw grid lines
        for (int i = 0; i <= GRID_SIZE; i++) {
            DrawLineEx((Vector2){i * cellWidth, 0}, (Vector2){i * cellWidth, SCREEN_HEIGHT}, 2, COLOR_NEON_CYAN);
            DrawLineEx((Vector2){0, i * cellHeight}, (Vector2){SCREEN_WIDTH, i * cellHeight}, 2, COLOR_NEON_CYAN);
        }

        DrawWalls();
        DrawEnemy();
        
        EndDrawing();
    }

    CloseWindow();
    return 0;
}