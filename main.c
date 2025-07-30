#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <float.h> // For FLT_MAX

// --- Game Constants ---
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 800
#define GRID_SIZE 10
#define cellWidth (SCREEN_WIDTH / GRID_SIZE)
#define cellHeight (SCREEN_HEIGHT / GRID_SIZE)
#define border_buff 10
#define SPAWN_INTERVAL 0.4f

// --- Tower Constants ---
// Default stats for newly placed towers
#define TOWER_RANGE (2.5f * cellWidth) // Range in pixels
#define TOWER_DAMAGE 25.0f
#define TOWER_FIRE_RATE 2.0f // Shots per second

// --- Color Macros ---
#define COLOR_BLACK       (Color){0, 0, 0, 255}
#define COLOR_NEON_CYAN   (Color){0, 255, 255, 200}
#define COLOR_DARK_NEON   (Color){0, 80, 80, 255}
#define COLOR_NEON_RED    (Color){255, 0, 100, 255}
#define COLOR_NEON_ORANGE (Color){255, 165, 0, 255}
#define COLOR_NEON_WHITE  (Color){255, 255, 255, 200}
#define COLOR_HEALTH_GREEN (Color){0, 255, 0, 220} // For health bars

// --- Data Structures ---

// Tower struct with stats
typedef struct {
    Vector2 pos; // Grid position
    bool active;
    float range;
    float damage;
    float fireRate;         // Shots per second
    float fireCooldown;     // Time until next shot
    int targetIndex;        // Index of the targeted enemy in the wave array (-1 for no target)
} Tower;

// EnemyType struct with health
typedef struct {
    float speed;
    Color color;
    float maxHealth;
} EnemyType;

// Enemy struct with health
typedef struct {
    Vector2 pos;
    int type;
    int pathIndex;
    float moveTimer;
    bool active;
    float health;
} Enemy;

#define MAX_ENEMIES_PER_WAVE 50
typedef struct {
    Enemy enemies[MAX_ENEMIES_PER_WAVE];
    int enemyCount;
    float spawnTimer;
    int enemiesSpawned;
} EnemyWave;

// Simple struct for laser visuals
#define MAX_LASERS 100
typedef struct {
    Vector2 startPos;
    Vector2 endPos;
    float lifeTimer;
    Color color;
} Laser;


// --- Global Variables ---
bool walls[GRID_SIZE][GRID_SIZE] = {0};
Vector2 path[GRID_SIZE * GRID_SIZE];
int pathLength = 0;
bool onPath[GRID_SIZE][GRID_SIZE] = {0};
Tower towers[GRID_SIZE][GRID_SIZE] = {0};

#define ENEMY_TYPE_COUNT 2
EnemyType enemyTypes[ENEMY_TYPE_COUNT];
EnemyWave activeWave;

// Array to manage active laser visuals
Laser lasers[MAX_LASERS];
int laserCount = 0;

// --- Function Prototypes ---
void InitializeEnemyTypes();
void CreateWave(int waveNumber);
void UpdateWave(EnemyWave *wave, float dt);
void UpdateEnemies(EnemyWave *wave, float dt);
void UpdateTowers(float dt); 
void DrawEnemies(const EnemyWave *wave);
void DrawTowers();
void DrawWall(int cellX, int cellY);
void UpdateAndDrawLasers(float dt); 
bool LoadMap(const char *filename, Vector2 *startPos, Vector2 *endPos);
bool FindPathBFS(Vector2 start, Vector2 end);

// --- Game Logic ---

void InitializeEnemyTypes() {
    // Type 0: Standard enemy
    enemyTypes[0].speed = 4.0f;
    enemyTypes[0].color = COLOR_NEON_RED;
    enemyTypes[0].maxHealth = 100.0f; 

    // Type 1: Fast enemy
    enemyTypes[1].speed = 8.0f;
    enemyTypes[1].color = COLOR_NEON_ORANGE;
    enemyTypes[1].maxHealth = 60.0f; 
}

void CreateWave(int waveNumber) {
    activeWave.enemyCount = 15;
    activeWave.enemiesSpawned = 0;
    activeWave.spawnTimer = 0.0f;

    for (int i = 0; i < MAX_ENEMIES_PER_WAVE; i++) {
        activeWave.enemies[i].active = false;
    }
    for (int i = 0; i < 10; i++) {
        activeWave.enemies[i].type = 0;
    }
    for (int i = 10; i < 15; i++) {
        activeWave.enemies[i].type = 1;
    }
}

// Function to spawn a laser visual effect
void FireLaser(Vector2 startPos, Vector2 endPos, Color color) {
    if (laserCount < MAX_LASERS) {
        lasers[laserCount].startPos = startPos;
        lasers[laserCount].endPos = endPos;
        lasers[laserCount].lifeTimer = 0.1f; // Laser is visible for 0.1 seconds
        lasers[laserCount].color = color;
        laserCount++;
    }
}

// Central logic for tower targeting and firing
void UpdateTowers(float dt) {
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            Tower *tower = &towers[x][y];
            if (!tower->active) continue;

            // Cooldown timer
            if (tower->fireCooldown > 0) {
                tower->fireCooldown -= dt;
            }

            // --- Target validation ---
            if (tower->targetIndex != -1) {
                Enemy *target = &activeWave.enemies[tower->targetIndex];
                Vector2 towerScreenPos = { (tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f };
                Vector2 enemyScreenPos = { (target->pos.x * cellWidth) + cellWidth / 2.0f, (target->pos.y * cellHeight) + cellHeight / 2.0f };
                
                // If target is no longer active or is out of range, lose the target
                if (!target->active || Vector2DistanceSqr(towerScreenPos, enemyScreenPos) > (tower->range * tower->range)) {
                    tower->targetIndex = -1;
                }
            }

            // --- Find new target ---
            if (tower->targetIndex == -1) {
                float minDistanceSqr = FLT_MAX;
                int closestEnemyIndex = -1;

                Vector2 towerScreenPos = { (tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f };

                for (int i = 0; i < activeWave.enemyCount; i++) {
                    Enemy *enemy = &activeWave.enemies[i];
                    if (!enemy->active) continue;

                    Vector2 enemyScreenPos = { (enemy->pos.x * cellWidth) + cellWidth / 2.0f, (enemy->pos.y * cellHeight) + cellHeight / 2.0f };
                    float distanceSqr = Vector2DistanceSqr(towerScreenPos, enemyScreenPos);
                    
                    // If enemy is in range and closer than the previous closest
                    if (distanceSqr <= (tower->range * tower->range) && distanceSqr < minDistanceSqr) {
                        minDistanceSqr = distanceSqr;
                        closestEnemyIndex = i;
                    }
                }
                tower->targetIndex = closestEnemyIndex;
            }
            
            // --- Firing ---
            if (tower->targetIndex != -1 && tower->fireCooldown <= 0) {
                Enemy *target = &activeWave.enemies[tower->targetIndex];
                
                // Reduce health
                target->health -= tower->damage;

                // Fire a laser visual
                Vector2 towerScreenPos = { (tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f };
                Vector2 enemyScreenPos = { (target->pos.x * cellWidth) + cellWidth / 2.0f, (target->pos.y * cellHeight) + cellHeight / 2.0f };
                FireLaser(towerScreenPos, enemyScreenPos, COLOR_NEON_WHITE);

                // Reset cooldown
                tower->fireCooldown = 1.0f / tower->fireRate;

                // Check for kill
                if (target->health <= 0) {
                    target->active = false;
                    tower->targetIndex = -1; // Allow tower to find new target immediately
                }
            }
        }
    }
}


void UpdateWave(EnemyWave *wave, float dt) {
    if (wave->enemiesSpawned >= wave->enemyCount) return;
    wave->spawnTimer += dt;
    if (wave->spawnTimer >= SPAWN_INTERVAL) {
        wave->spawnTimer = 0;
        Enemy *enemy = &wave->enemies[wave->enemiesSpawned];
        enemy->active = true;
        enemy->pos = path[0];
        enemy->pathIndex = 0;
        enemy->moveTimer = 0.0f;
        enemy->health = enemyTypes[enemy->type].maxHealth; // Initialize health
        wave->enemiesSpawned++;
    }
}

void UpdateEnemies(EnemyWave *wave, float dt) {
    for (int i = 0; i < wave->enemyCount; i++) {
        Enemy *enemy = &wave->enemies[i];
        if (!enemy->active) continue;

        if (enemy->pathIndex >= pathLength - 1) {
            enemy->active = false;
            continue;
        }

        float moveInterval = 1.0f / enemyTypes[enemy->type].speed;
        enemy->moveTimer += dt;

        Vector2 startNode = path[enemy->pathIndex];
        Vector2 targetNode = path[enemy->pathIndex + 1];

        if (enemy->moveTimer >= moveInterval) {
            enemy->pos = targetNode;
            enemy->pathIndex++;
            enemy->moveTimer -= moveInterval;

            if (enemy->pathIndex >= pathLength - 1) {
                enemy->active = false;
                continue;
            }
            startNode = path[enemy->pathIndex];
            targetNode = path[enemy->pathIndex + 1];
        }
        
        float lerpAmount = enemy->moveTimer / moveInterval;
        enemy->pos = Vector2Lerp(startNode, targetNode, lerpAmount);
    }
}

// --- Main Entry Point ---
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tron-Style Tower Defense - Firing!");
    SetTargetFPS(60);

    InitializeEnemyTypes();

    Vector2 startPos, endPos;
    if (!LoadMap("map.txt", &startPos, &endPos) || !FindPathBFS(startPos, endPos)) {
        printf("Error: Map or Path not valid. Exiting.\n");
        CloseWindow();
        return 1;
    }

    CreateWave(1);
    RenderTexture2D backgroundTexture = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);

    BeginTextureMode(backgroundTexture);
        ClearBackground(COLOR_BLACK);
        for (int y = 0; y <= GRID_SIZE; y++) {
            for (int x = 0; x < GRID_SIZE; x++) {
                bool shouldDraw = true;
                if (y > 0 && y < GRID_SIZE) { if (onPath[x][y-1] && onPath[x][y]) shouldDraw = false; }
                if (shouldDraw) DrawLineEx( (Vector2){(float)x * cellWidth, (float)y * cellHeight}, (Vector2){(float)(x+1) * cellWidth, (float)y * cellHeight}, 2, COLOR_NEON_CYAN );
            }
        }
        for (int x = 0; x <= GRID_SIZE; x++) {
            for (int y = 0; y < GRID_SIZE; y++) {
                bool shouldDraw = true;
                if (x > 0 && x < GRID_SIZE) { if (onPath[x-1][y] && onPath[x][y]) shouldDraw = false; }
                if (shouldDraw) DrawLineEx( (Vector2){(float)x * cellWidth, (float)y * cellHeight}, (Vector2){(float)x * cellWidth, (float)(y+1) * cellHeight}, 2, COLOR_NEON_CYAN );
            }
        }
        for (int x = 0; x < GRID_SIZE; x++) {
            for (int y = 0; y < GRID_SIZE; y++) {
                if (walls[x][y]) DrawWall(x, y);
            }
        }
    EndTextureMode();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // --- Input and Logic for Tower Placement ---
        Vector2 mousePos = GetMousePosition();
        int gridX = (int)(mousePos.x / cellWidth);
        int gridY = (int)(mousePos.y / cellHeight);
        bool isMouseOnGrid = (gridX >= 0 && gridX < GRID_SIZE && gridY >= 0 && gridY < GRID_SIZE);

        if (isMouseOnGrid && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (walls[gridX][gridY] && !towers[gridX][gridY].active) {
                // Initialize all tower stats on placement
                Tower *newTower = &towers[gridX][gridY];
                newTower->active = true;
                newTower->pos = (Vector2){(float)gridX, (float)gridY};
                newTower->range = TOWER_RANGE;
                newTower->damage = TOWER_DAMAGE;
                newTower->fireRate = TOWER_FIRE_RATE;
                newTower->fireCooldown = 0.0f;
                newTower->targetIndex = -1; // No target initially
            }
        }

        // --- Updates ---
        UpdateWave(&activeWave, dt);
        UpdateEnemies(&activeWave, dt);
        UpdateTowers(dt); // Update all towers

        // --- Drawing ---
        BeginDrawing();
        ClearBackground(BLACK);

        DrawTextureRec(backgroundTexture.texture,
                       (Rectangle){ 0, 0, (float)backgroundTexture.texture.width, (float)-backgroundTexture.texture.height },
                       (Vector2){ 0, 0 }, WHITE);
        
        DrawEnemies(&activeWave);
        DrawTowers();
        UpdateAndDrawLasers(dt); // Draw laser effects

        // --- Draw UI / Selection Highlight ---
        if (isMouseOnGrid) {
            if (walls[gridX][gridY]) {
                Color highlightColor = towers[gridX][gridY].active ? COLOR_NEON_RED : COLOR_NEON_WHITE; 
                DrawRectangleLinesEx( (Rectangle){(float)gridX * cellWidth, (float)gridY * cellHeight, (float)cellWidth, (float)cellHeight}, 3, Fade(highlightColor, 0.7f) );
                
                // If hovering over an existing tower, draw its range
                if (towers[gridX][gridY].active) {
                    float screenX = gridX * cellWidth + cellWidth / 2.0f;
                    float screenY = gridY * cellHeight + cellHeight / 2.0f;
                    DrawCircleLines(screenX, screenY, towers[gridX][gridY].range, Fade(COLOR_NEON_WHITE, 0.5f));
                }

            }
        }
        
        EndDrawing();
    }

    UnloadRenderTexture(backgroundTexture);
    CloseWindow();
    return 0;
}

// --- Function Implementations ---

// Updates laser lifetimes and draws them
void UpdateAndDrawLasers(float dt) {
    for (int i = 0; i < laserCount; i++) {
        lasers[i].lifeTimer -= dt;
        if (lasers[i].lifeTimer > 0) {
            DrawLineEx(lasers[i].startPos, lasers[i].endPos, 3, Fade(lasers[i].color, lasers[i].lifeTimer * 10));
        } else {
            // Remove laser by swapping with the last one
            lasers[i] = lasers[laserCount - 1];
            laserCount--;
            i--; // Re-check the current index as it's now a new laser
        }
    }
}

void DrawTowers() {
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            if (towers[x][y].active) {
                float screenX = x * cellWidth;
                float screenY = y * cellHeight;
                Vector2 v1 = { screenX + cellWidth / 2.0f, screenY + border_buff };
                Vector2 v2 = { screenX + border_buff, screenY + cellHeight - border_buff };
                Vector2 v3 = { screenX + cellWidth - border_buff, screenY + cellHeight - border_buff };
                DrawTriangle(v1, v2, v3, COLOR_NEON_ORANGE);
                DrawTriangleLines(v1, v2, v3, COLOR_NEON_CYAN);
            }
        }
    }
}

void DrawWall(int cellX, int cellY) {
    float x = cellX * cellWidth;
    float y = cellY * cellHeight;
    DrawRectangleV((Vector2){x + border_buff, y + border_buff},
                   (Vector2){cellWidth - border_buff * 2, cellHeight - border_buff * 2},
                   COLOR_DARK_NEON);
    for (int i = 0; i < 3; i++) {
        DrawRectangleLinesEx( (Rectangle){x - i, y - i, cellWidth + 2*i, cellHeight + 2*i}, 1, Fade(COLOR_NEON_CYAN, 0.4f - i * 0.1f));
    }
}

// DrawEnemies and draws health bars
void DrawEnemies(const EnemyWave *wave) {
    for (int i = 0; i < wave->enemyCount; i++) {
        const Enemy *enemy = &wave->enemies[i];
        if (enemy->active) {
            float screenX = enemy->pos.x * cellWidth + cellWidth / 2;
            float screenY = enemy->pos.y * cellHeight + cellHeight / 2;
            DrawCircleV((Vector2){screenX, screenY}, cellWidth / 3.5f, enemyTypes[enemy->type].color);

            // Draw health bar
            float healthPercentage = enemy->health / enemyTypes[enemy->type].maxHealth;
            float barWidth = cellWidth * 0.8f;
            float barHeight = 8.0f;
            Vector2 barPos = { screenX - barWidth / 2, screenY - cellHeight / 2.0f - barHeight };
            
            // Background of the health bar
            DrawRectangleV(barPos, (Vector2){barWidth, barHeight}, Fade(BLACK, 0.7f));
            // Current health
            DrawRectangleV(barPos, (Vector2){barWidth * healthPercentage, barHeight}, COLOR_HEALTH_GREEN);
            // Border
            DrawRectangleLinesEx((Rectangle){barPos.x, barPos.y, barWidth, barHeight}, 1, Fade(COLOR_NEON_CYAN, 0.8f));
        }
    }
}

bool LoadMap(const char *filename, Vector2 *startPos, Vector2 *endPos) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed to open map file: %s\n", filename);
        return false;
    }
    startPos->x = -1; startPos->y = -1;
    endPos->x = -1; endPos->y = -1;
    char line[GRID_SIZE + 2];
    for (int y = 0; y < GRID_SIZE; y++) {
        if (fgets(line, sizeof(line), file) == NULL) {
            fclose(file); return false;
        }
        if (strlen(line) < GRID_SIZE) {
            fclose(file); return false;
        }
        for (int x = 0; x < GRID_SIZE; x++) {
            char c = line[x];
            if (c == '1') {
                walls[x][y] = true;
            } else {
                walls[x][y] = false;
                if (c == 's') {
                    if (startPos->x != -1) { fclose(file); return false; }
                    *startPos = (Vector2){(float)x, (float)y};
                } else if (c == 'f') {
                    if (endPos->x != -1) { fclose(file); return false; }
                    *endPos = (Vector2){(float)x, (float)y};
                }
            }
        }
    }
    fclose(file);
    if (startPos->x == -1 || endPos->x == -1) return false;
    return true;
}
bool FindPathBFS(Vector2 start, Vector2 end) {
    Vector2 queue[GRID_SIZE * GRID_SIZE];
    int head = 0, tail = 0;
    Vector2 parent[GRID_SIZE][GRID_SIZE];
    bool visited[GRID_SIZE][GRID_SIZE] = {0};
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    if (walls[(int)start.x][(int)start.y] || walls[(int)end.x][(int)end.y]) return false;
    queue[tail++] = start;
    visited[(int)start.x][(int)start.y] = true;
    parent[(int)start.x][(int)start.y] = (Vector2){-1, -1};
    bool pathFound = false;
    while (head < tail) {
        Vector2 current = queue[head++];
        if (current.x == end.x && current.y == end.y) {
            pathFound = true;
            break;
        }
        for (int i = 0; i < 4; i++) {
            int nextX = current.x + dx[i];
            int nextY = current.y + dy[i];
            if (nextX >= 0 && nextX < GRID_SIZE && nextY >= 0 && nextY < GRID_SIZE) {
                if (!visited[nextX][nextY] && !walls[nextX][nextY]) {
                    visited[nextX][nextY] = true;
                    parent[nextX][nextY] = current;
                    queue[tail++] = (Vector2){(float)nextX, (float)nextY};
                }
            }
        }
    }
    if (!pathFound) return false;
    memset(onPath, 0, sizeof(onPath));
    pathLength = 0;
    Vector2 current = end;
    while (current.x != -1) {
        path[pathLength++] = current;
        onPath[(int)current.x][(int)current.y] = true;
        current = parent[(int)current.x][(int)current.y];
    }
    for (int i = 0; i < pathLength / 2; i++) {
        Vector2 temp = path[i];
        path[i] = path[pathLength - 1 - i];
        path[pathLength - 1 - i] = temp;
    }
    return true;
}