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
#define TOWER_RANGE (2.5f * cellWidth)
#define TOWER_DAMAGE 25.0f
#define TOWER_FIRE_RATE 2.0f
#define TOWER_COST 42 // Cost to build a tower

// --- Player Stats ---
// Initial stats for the player
#define PLAYER_START_HEALTH 20
#define PLAYER_START_MONEY 100

// --- Color Macros ---
#define COLOR_BLACK       (Color){0, 0, 0, 255}
#define COLOR_NEON_CYAN   (Color){0, 255, 255, 200}
#define COLOR_DARK_NEON   (Color){0, 80, 80, 255}
#define COLOR_NEON_RED    (Color){255, 0, 100, 255}
#define COLOR_NEON_ORANGE (Color){255, 165, 0, 255}
#define COLOR_NEON_WHITE  (Color){255, 255, 255, 200}
#define COLOR_HEALTH_GREEN (Color){0, 255, 0, 220}

// --- Data Structures ---

// Game State enum to manage the game loop
typedef enum {
    GAME_STATE_WAVE_TRANSITION,
    GAME_STATE_PLAYING,
    GAME_STATE_GAME_OVER
} GameState;

// Tower struct with stats
typedef struct {
    Vector2 pos;
    bool active;
    float range;
    float damage;
    float fireRate;
    float fireCooldown;
    int targetIndex;
} Tower;

// Added money reward for killing this type of enemy
typedef struct {
    float speed;
    Color color;
    float maxHealth;
    int money; // Money awarded on kill
} EnemyType;

// Enemy struct with health
typedef struct {
    Vector2 pos;
    int type;
    int pathIndex;
    float moveTimer;
    bool active;
    float health;
    float maxHealth; // Store max health for dynamic scaling
} Enemy;

#define MAX_ENEMIES_PER_WAVE 100 // Increased max enemies for later waves
typedef struct {
    Enemy enemies[MAX_ENEMIES_PER_WAVE];
    int enemyCount;
    float spawnTimer;
    int enemiesSpawned;
    bool isFinished; // Flag to check if wave is fully spawned
} EnemyWave;

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
Laser lasers[MAX_LASERS];
int laserCount = 0;

// Game state and player variables
GameState gameState;
int playerHealth;
int playerMoney;
int currentWaveNumber;


// --- Function Prototypes ---
void InitializeGame(); // NEW
void RestartGame(); // NEW
void InitializeEnemyTypes();
void CreateWave(int waveNumber);
void UpdateWave(EnemyWave *wave, float dt);
void UpdateEnemies(EnemyWave *wave, float dt);
void UpdateTowers(float dt);
void CheckWaveCompletion(); // NEW
void DrawGameUI(); // NEW
void DrawEnemies(const EnemyWave *wave);
void DrawTowers();
void DrawWall(int cellX, int cellY);
void UpdateAndDrawLasers(float dt);
bool LoadMap(const char *filename, Vector2 *startPos, Vector2 *endPos);
bool FindPathBFS(Vector2 start, Vector2 end);

// --- Game Logic ---

// Initialize all game variables
void InitializeGame() {
    playerHealth = PLAYER_START_HEALTH;
    playerMoney = PLAYER_START_MONEY;
    currentWaveNumber = 0; // Will be incremented to 1 on first wave start
    gameState = GAME_STATE_WAVE_TRANSITION;

    // Clear all towers
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            towers[x][y].active = false;
        }
    }
    
    InitializeEnemyTypes();
}

// Restart the game from a Game Over state
void RestartGame() {
    InitializeGame();
}

void InitializeEnemyTypes() {
    // Type 0: Standard enemy
    enemyTypes[0].speed = 4.0f;
    enemyTypes[0].color = COLOR_NEON_RED;
    enemyTypes[0].maxHealth = 100.0f;
    enemyTypes[0].money = 5; // NEW

    // Type 1: Fast enemy
    enemyTypes[1].speed = 8.0f;
    enemyTypes[1].color = COLOR_NEON_ORANGE;
    enemyTypes[1].maxHealth = 60.0f;
    enemyTypes[1].money = 8; // NEW
}

// Now creates waves that get progressively harder
void CreateWave(int waveNumber) {
    activeWave.enemyCount = 10 + waveNumber * 5; // More enemies each wave
    if (activeWave.enemyCount > MAX_ENEMIES_PER_WAVE) {
        activeWave.enemyCount = MAX_ENEMIES_PER_WAVE;
    }
    activeWave.enemiesSpawned = 0;
    activeWave.spawnTimer = 0.0f;
    activeWave.isFinished = false;

    float healthMultiplier = 1.0f + (waveNumber - 1) * 0.15f; // More health each wave

    for (int i = 0; i < activeWave.enemyCount; i++) {
        activeWave.enemies[i].active = false;
        // Mix enemy types based on wave number
        activeWave.enemies[i].type = (waveNumber > 2 && i % 3 == 0) ? 1 : 0;
        
        // Store max health for this specific enemy instance
        activeWave.enemies[i].maxHealth = enemyTypes[activeWave.enemies[i].type].maxHealth * healthMultiplier;
    }
}

void FireLaser(Vector2 startPos, Vector2 endPos, Color color) {
    if (laserCount < MAX_LASERS) {
        lasers[laserCount].startPos = startPos;
        lasers[laserCount].endPos = endPos;
        lasers[laserCount].lifeTimer = 0.1f;
        lasers[laserCount].color = color;
        laserCount++;
    }
}

void UpdateTowers(float dt) {
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            Tower *tower = &towers[x][y];
            if (!tower->active) continue;

            if (tower->fireCooldown > 0) {
                tower->fireCooldown -= dt;
            }

            if (tower->targetIndex != -1) {
                Enemy *target = &activeWave.enemies[tower->targetIndex];
                Vector2 towerScreenPos = { (tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f };
                if (!target->active || Vector2DistanceSqr(towerScreenPos, target->pos) > (tower->range * tower->range)) {
                    tower->targetIndex = -1;
                }
            }

            if (tower->targetIndex == -1) {
                float minDistanceSqr = FLT_MAX;
                int closestEnemyIndex = -1;
                Vector2 towerScreenPos = { (tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f };

                for (int i = 0; i < activeWave.enemyCount; i++) {
                    Enemy *enemy = &activeWave.enemies[i];
                    if (!enemy->active) continue;

                    float distanceSqr = Vector2DistanceSqr(towerScreenPos, enemy->pos);
                    if (distanceSqr <= (tower->range * tower->range) && distanceSqr < minDistanceSqr) {
                        minDistanceSqr = distanceSqr;
                        closestEnemyIndex = i;
                    }
                }
                tower->targetIndex = closestEnemyIndex;
            }
            
            if (tower->targetIndex != -1 && tower->fireCooldown <= 0) {
                Enemy *target = &activeWave.enemies[tower->targetIndex];
                target->health -= tower->damage;

                Vector2 towerScreenPos = { (tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f };
                FireLaser(towerScreenPos, target->pos, COLOR_NEON_WHITE);

                tower->fireCooldown = 1.0f / tower->fireRate;

                if (target->health <= 0) {
                    target->active = false;
                    playerMoney += enemyTypes[target->type].money; // Grant money on kill
                    tower->targetIndex = -1;
                }
            }
        }
    }
}

void UpdateWave(EnemyWave *wave, float dt) {
    if (wave->enemiesSpawned >= wave->enemyCount) {
        wave->isFinished = true; // Mark that all enemies have been spawned
        return;
    }
    wave->spawnTimer += dt;
    if (wave->spawnTimer >= SPAWN_INTERVAL) {
        wave->spawnTimer = 0;
        Enemy *enemy = &wave->enemies[wave->enemiesSpawned];
        enemy->active = true;
        // Spawn at the center of the start cell
        enemy->pos = (Vector2){ (path[0].x * cellWidth) + cellWidth / 2.0f, (path[0].y * cellHeight) + cellHeight / 2.0f };
        enemy->pathIndex = 0;
        enemy->moveTimer = 0.0f;
        enemy->health = enemy->maxHealth; // Use the pre-calculated scaled health
        wave->enemiesSpawned++;
    }
}

void UpdateEnemies(EnemyWave *wave, float dt) {
    for (int i = 0; i < wave->enemyCount; i++) {
        Enemy *enemy = &wave->enemies[i];
        if (!enemy->active) continue;

        // Check if enemy reached the end
        if (enemy->pathIndex >= pathLength - 1) {
            enemy->active = false;
            playerHealth--; // Player loses a life
            if (playerHealth <= 0) {
                playerHealth = 0;
                gameState = GAME_STATE_GAME_OVER; // Trigger game over
            }
            continue;
        }

        float moveInterval = 1.0f / enemyTypes[enemy->type].speed;
        enemy->moveTimer += dt;

        Vector2 startNode = path[enemy->pathIndex];
        Vector2 targetNode = path[enemy->pathIndex + 1];

        // Convert grid coords to screen coords for lerping
        Vector2 startScreenPos = { startNode.x * cellWidth + cellWidth / 2.0f, startNode.y * cellHeight + cellHeight / 2.0f };
        Vector2 targetScreenPos = { targetNode.x * cellWidth + cellWidth / 2.0f, targetNode.y * cellHeight + cellHeight / 2.0f };

        float lerpAmount = enemy->moveTimer / moveInterval;
        if (lerpAmount >= 1.0f) {
            lerpAmount = 1.0f;
            enemy->pathIndex++;
            enemy->moveTimer -= moveInterval;
        }
        
        enemy->pos = Vector2Lerp(startScreenPos, targetScreenPos, lerpAmount);
    }
}

// Checks if all enemies in the current wave are gone
void CheckWaveCompletion() {
    if (!activeWave.isFinished) return; // Not all enemies have spawned yet

    for (int i = 0; i < activeWave.enemyCount; i++) {
        if (activeWave.enemies[i].active) {
            return; // At least one enemy is still active, so wave is not over
        }
    }

    // If we get here, all spawned enemies are inactive
    gameState = GAME_STATE_WAVE_TRANSITION;
}


// --- Main Entry Point ---
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tron-Style Tower Defense - Full Game");
    SetTargetFPS(60);

    Vector2 startPos, endPos;
    if (!LoadMap("map.txt", &startPos, &endPos) || !FindPathBFS(startPos, endPos)) {
        TraceLog(LOG_ERROR, "Map or Path not valid. Exiting.");
        CloseWindow();
        return 1;
    }

    InitializeGame(); // Set up initial game state

    RenderTexture2D backgroundTexture = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    BeginTextureMode(backgroundTexture);
        ClearBackground(COLOR_BLACK);
        // Draw grid lines (optimized)
        for (int y = 0; y <= GRID_SIZE; y++) DrawLine(0, y * cellHeight, SCREEN_WIDTH, y * cellHeight, COLOR_DARK_NEON);
        for (int x = 0; x <= GRID_SIZE; x++) DrawLine(x * cellWidth, 0, x * cellWidth, SCREEN_HEIGHT, COLOR_DARK_NEON);
        // Draw path highlight
        for (int i = 0; i < pathLength - 1; i++) {
            Vector2 p1 = {path[i].x * cellWidth + cellWidth/2, path[i].y * cellHeight + cellHeight/2};
            Vector2 p2 = {path[i+1].x * cellWidth + cellWidth/2, path[i+1].y * cellHeight + cellHeight/2};
            DrawLineEx(p1, p2, 5, COLOR_NEON_CYAN);
        }
        // Draw wall platforms
        for (int x = 0; x < GRID_SIZE; x++) {
            for (int y = 0; y < GRID_SIZE; y++) {
                if (walls[x][y]) DrawWall(x, y);
            }
        }
    EndTextureMode();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // --- Game State Machine ---
        switch (gameState) {
            case GAME_STATE_PLAYING: {
                UpdateWave(&activeWave, dt);
                UpdateEnemies(&activeWave, dt);
                UpdateTowers(dt);
                CheckWaveCompletion(); // Check if the wave is over
            } break;

            case GAME_STATE_WAVE_TRANSITION: {
                // Wait for player to start the next wave
                if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    currentWaveNumber++;
                    CreateWave(currentWaveNumber);
                    gameState = GAME_STATE_PLAYING;
                }
            } break;

            case GAME_STATE_GAME_OVER: {
                // Wait for player to restart
                if (IsKeyPressed(KEY_R)) {
                    RestartGame();
                }
            } break;
        }

        // --- Tower Placement (can happen anytime except game over) ---
        if (gameState != GAME_STATE_GAME_OVER) {
            Vector2 mousePos = GetMousePosition();
            int gridX = (int)(mousePos.x / cellWidth);
            int gridY = (int)(mousePos.y / cellHeight);
            bool isMouseOnGrid = (gridX >= 0 && gridX < GRID_SIZE && gridY >= 0 && gridY < GRID_SIZE);

            if (isMouseOnGrid && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                // Check for cost and if it's a valid wall placement
                if (walls[gridX][gridY] && !towers[gridX][gridY].active && playerMoney >= TOWER_COST) {
                    playerMoney -= TOWER_COST; // Spend money
                    Tower *newTower = &towers[gridX][gridY];
                    newTower->active = true;
                    newTower->pos = (Vector2){(float)gridX, (float)gridY};
                    newTower->range = TOWER_RANGE;
                    newTower->damage = TOWER_DAMAGE;
                    newTower->fireRate = TOWER_FIRE_RATE;
                    newTower->fireCooldown = 0.0f;
                    newTower->targetIndex = -1;
                }
            }
        }

        // --- Drawing ---
        BeginDrawing();
        ClearBackground(BLACK);

        DrawTextureRec(backgroundTexture.texture,
                       (Rectangle){ 0, 0, (float)backgroundTexture.texture.width, (float)-backgroundTexture.texture.height },
                       (Vector2){ 0, 0 }, WHITE);
        
        DrawEnemies(&activeWave);
        DrawTowers();
        UpdateAndDrawLasers(dt);

        // --- Draw UI / Selection Highlight ---
        Vector2 mousePos = GetMousePosition();
        int gridX = (int)(mousePos.x / cellWidth);
        int gridY = (int)(mousePos.y / cellHeight);
        bool isMouseOnGrid = (gridX >= 0 && gridX < GRID_SIZE && gridY >= 0 && gridY < GRID_SIZE);
        if (isMouseOnGrid && gameState != GAME_STATE_GAME_OVER) {
            if (walls[gridX][gridY]) {
                Color highlightColor = COLOR_NEON_WHITE;
                if (towers[gridX][gridY].active) highlightColor = COLOR_NEON_RED;
                else if (playerMoney < TOWER_COST) highlightColor = Fade(COLOR_NEON_RED, 0.5f); // Can't afford
                
                DrawRectangleLinesEx( (Rectangle){(float)gridX * cellWidth, (float)gridY * cellHeight, (float)cellWidth, (float)cellHeight}, 3, Fade(highlightColor, 0.7f) );
                
                if (towers[gridX][gridY].active) {
                    float screenX = gridX * cellWidth + cellWidth / 2.0f;
                    float screenY = gridY * cellHeight + cellHeight / 2.0f;
                    DrawCircleLines(screenX, screenY, towers[gridX][gridY].range, Fade(COLOR_NEON_WHITE, 0.5f));
                }
            }
        }
        
        DrawGameUI(); // Draw all UI text

        EndDrawing();
    }

    UnloadRenderTexture(backgroundTexture);
    CloseWindow();
    return 0;
}

// --- Function Implementations ---

void UpdateAndDrawLasers(float dt) {
    for (int i = 0; i < laserCount; i++) {
        lasers[i].lifeTimer -= dt;
        if (lasers[i].lifeTimer > 0) {
            DrawLineEx(lasers[i].startPos, lasers[i].endPos, 3, Fade(lasers[i].color, lasers[i].lifeTimer * 10));
        } else {
            lasers[i] = lasers[laserCount - 1];
            laserCount--;
            i--;
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
}

void DrawEnemies(const EnemyWave *wave) {
    for (int i = 0; i < wave->enemyCount; i++) {
        const Enemy *enemy = &wave->enemies[i];
        if (enemy->active) {
            DrawCircleV(enemy->pos, cellWidth / 3.5f, enemyTypes[enemy->type].color);

            float healthPercentage = enemy->health / enemy->maxHealth; // Use enemy's specific max health
            float barWidth = cellWidth * 0.8f;
            float barHeight = 8.0f;
            Vector2 barPos = { enemy->pos.x - barWidth / 2, enemy->pos.y - cellHeight / 2.0f - barHeight };
            
            DrawRectangleV(barPos, (Vector2){barWidth, barHeight}, Fade(BLACK, 0.7f));
            DrawRectangleV(barPos, (Vector2){barWidth * healthPercentage, barHeight}, COLOR_HEALTH_GREEN);
            DrawRectangleLinesEx((Rectangle){barPos.x, barPos.y, barWidth, barHeight}, 1, Fade(COLOR_NEON_CYAN, 0.8f));
        }
    }
}

// Function to draw all the UI text
void DrawGameUI() {
    // Draw Player Stats
    DrawText(TextFormat("WAVE: %d", currentWaveNumber), 10, 10, 20, COLOR_NEON_CYAN);
    DrawText(TextFormat("HEALTH: %d", playerHealth), 10, 35, 20, COLOR_HEALTH_GREEN);
    DrawText(TextFormat("MONEY: $%d", playerMoney), 10, 60, 20, COLOR_NEON_ORANGE);

    // Draw Game State Information
    if (gameState == GAME_STATE_WAVE_TRANSITION) {
        const char* text = "CLICK or PRESS SPACE to START WAVE";
        int textWidth = MeasureText(text, 30);
        DrawText(text, SCREEN_WIDTH / 2 - textWidth / 2, SCREEN_HEIGHT / 2 - 15, 30, COLOR_NEON_WHITE);
    } else if (gameState == GAME_STATE_GAME_OVER) {
        const char* text1 = "GAME OVER";
        const char* text2 = "Press 'R' to Restart";
        int text1Width = MeasureText(text1, 60);
        int text2Width = MeasureText(text2, 30);
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.7f)); // Darken screen
        DrawText(text1, SCREEN_WIDTH / 2 - text1Width / 2, SCREEN_HEIGHT / 2 - 40, 60, COLOR_NEON_RED);
        DrawText(text2, SCREEN_WIDTH / 2 - text2Width / 2, SCREEN_HEIGHT / 2 + 30, 30, COLOR_NEON_WHITE);
    }
}


// --- Utility Functions (Unchanged) ---

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