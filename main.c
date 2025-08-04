#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h> // For abs
#include <stdbool.h>
#include <string.h>
#include <float.h> // For FLT_MAX

// --- Game Constants ---
#define SCREEN_WIDTH 1000 // Increased for UI panel
#define SCREEN_HEIGHT 800
#define GAME_AREA_WIDTH 800
#define GRID_SIZE 10
#define cellWidth (GAME_AREA_WIDTH / GRID_SIZE)
#define cellHeight (SCREEN_HEIGHT / GRID_SIZE)
#define border_buff 10
#define SPAWN_INTERVAL 0.4f
#define MAX_WAVES 30 // A win condition

// --- Player Stats ---
#define PLAYER_START_HEALTH 20
#define PLAYER_START_MONEY 150 // Slightly more to start

// --- Color Macros ---
#define COLOR_BLACK       (Color){10, 10, 20, 255}
#define COLOR_BG_GRID     (Color){0, 40, 40, 255}
#define COLOR_PATH        (Color){0, 120, 120, 150}
#define COLOR_WALL        (Color){0, 60, 60, 255}
#define COLOR_NEON_CYAN   (Color){0, 255, 255, 200}
#define COLOR_NEON_RED    (Color){255, 0, 100, 255}
#define COLOR_NEON_ORANGE (Color){255, 165, 0, 255}
#define COLOR_NEON_WHITE  (Color){255, 255, 255, 200}
#define COLOR_HEALTH_GREEN (Color){0, 255, 0, 220}
#define COLOR_UI_PANEL    (Color){20, 20, 30, 240}
#define COLOR_UI_ACCENT   (Color){0, 180, 180, 255}
#define COLOR_FROST       (Color){100, 160, 255, 200}

// --- Data Structures ---

// Game State enum to manage the game loop
typedef enum {
    GAME_STATE_WAVE_TRANSITION,
    GAME_STATE_PLAYING,
    GAME_STATE_GAME_OVER,
    GAME_STATE_VICTORY
} GameState;

// NEW: Tower types
typedef enum {
    TOWER_GUN,
    TOWER_SLOW,
    TOWER_SPLASH,
    TOWER_TYPE_COUNT
} TowerType;

// NEW: Centralized Tower Stats for balancing
#define MAX_TOWER_LEVEL 4
typedef struct {
    int cost;
    float range;
    float damage;
    float fireRate;
    float splashRadius; // Only for splash tower
} TowerLevelStats;

TowerLevelStats g_towerStats[TOWER_TYPE_COUNT][MAX_TOWER_LEVEL];
const char *g_towerNames[] = {"Gun Turret", "Frost Spire", "Cannon"};

// MODIFIED: Tower struct now includes type and level
typedef struct {
    Vector2 pos;
    bool active;
    TowerType type;
    int level;
    float fireCooldown;
    int targetIndex;
} Tower;

// MODIFIED: Enemy types
typedef enum {
    ENEMY_NORMAL,
    ENEMY_SCOUT, // Fast
    ENEMY_TANK,  // Slow and durable
    ENEMY_TYPE_COUNT
} EnemyTypeEnum;

typedef struct {
    float speed;
    Color color;
    float maxHealth;
    int money;
} EnemyType;

// MODIFIED: Enemy struct now has slow effect fields
typedef struct {
    Vector2 pos;
    int type;
    int pathIndex;
    float moveTimer;
    bool active;
    float health;
    float maxHealth;
    float speedMultiplier; // For slow effects
    float slowTimer;       // Duration of slow
} Enemy;

#define MAX_ENEMIES_PER_WAVE 150
typedef struct {
    Enemy enemies[MAX_ENEMIES_PER_WAVE];
    int enemyCount;
    float spawnTimer;
    int enemiesSpawned;
    bool isFinished;
} EnemyWave;

#define MAX_PROJECTILES 200
typedef struct {
    Vector2 startPos;
    Vector2 endPos;
    float lifeTimer;
    Color color;
    bool isSplash; // NEW: To draw explosions
} Projectile;

// --- Global Variables ---
bool walls[GRID_SIZE][GRID_SIZE] = {0};
Vector2 path[GRID_SIZE * GRID_SIZE];
int pathLength = 0;
Tower towers[GRID_SIZE][GRID_SIZE] = {0};

EnemyType enemyTypes[ENEMY_TYPE_COUNT];
EnemyWave activeWave;
Projectile projectiles[MAX_PROJECTILES];
int projectileCount = 0;

GameState gameState;
int playerHealth;
int playerMoney;
int currentWaveNumber;
float gameSpeed = 1.0f;

// NEW: UI and Selection state
int g_selectedTowerX = -1, g_selectedTowerY = -1;
TowerType g_selectedBuildType = -1; // -1 means no selection

// NEW: Audio
Sound sndLaser, sndExplosion, sndPlace, sndUpgrade, sndError, sndHurt;
Music music;


// --- Function Prototypes ---
void InitializeGame();
void RestartGame();
void InitializeTowerStats();
void InitializeEnemyTypes();
void LoadGameAudio();
void UnloadGameAudio();
void CreateWave(int waveNumber);
void UpdateGame(float dt);
void UpdateWave(EnemyWave *wave, float dt);
void UpdateEnemies(EnemyWave *wave, float dt);
void UpdateTowers(float dt);
void CheckWaveCompletion();
void DrawGame();
void DrawGameUI();
void DrawBuildUI();
void DrawSelectionUI();
void DrawEnemies(const EnemyWave *wave);
void DrawTowers();
void DrawWall(int cellX, int cellY);
void UpdateAndDrawProjectiles(float dt);
void FireProjectile(Vector2 startPos, Vector2 endPos, Color color, bool isSplash);
void UpgradeSelectedTower();
void SellSelectedTower();
bool LoadMap(const char *filename, Vector2 *startPos, Vector2 *endPos);
bool FindPathBFS(Vector2 start, Vector2 end);

// --- Game Logic ---

void InitializeTowerStats() {
    // Level 0 is base
    // Gun Tower: Standard single-target damage
    g_towerStats[TOWER_GUN][0] = (TowerLevelStats){50, 2.5f * cellWidth, 40.0f, 2.0f, 0};
    g_towerStats[TOWER_GUN][1] = (TowerLevelStats){75, 2.7f * cellWidth, 65.0f, 2.2f, 0};
    g_towerStats[TOWER_GUN][2] = (TowerLevelStats){100, 3.0f * cellWidth, 90.0f, 2.5f, 0};
    g_towerStats[TOWER_GUN][3] = (TowerLevelStats){150, 3.3f * cellWidth, 130.0f, 3.0f, 0};

    // Slow Tower: No damage, but slows enemies in an area. Fire rate is how often it pulses.
    g_towerStats[TOWER_SLOW][0] = (TowerLevelStats){60, 2.0f * cellWidth, 0.5f, 1.0f, 0}; // Damage is slow %
    g_towerStats[TOWER_SLOW][1] = (TowerLevelStats){80, 2.2f * cellWidth, 0.4f, 1.0f, 0};
    g_towerStats[TOWER_SLOW][2] = (TowerLevelStats){100, 2.4f * cellWidth, 0.3f, 1.0f, 0};
    g_towerStats[TOWER_SLOW][3] = (TowerLevelStats){140, 2.6f * cellWidth, 0.2f, 1.0f, 0};

    // Splash Tower: Slower, but damages enemies in a radius
    g_towerStats[TOWER_SPLASH][0] = (TowerLevelStats){100, 2.2f * cellWidth, 50.0f, 0.8f, 0.8f * cellWidth};
    g_towerStats[TOWER_SPLASH][1] = (TowerLevelStats){120, 2.4f * cellWidth, 70.0f, 0.9f, 0.9f * cellWidth};
    g_towerStats[TOWER_SPLASH][2] = (TowerLevelStats){160, 2.6f * cellWidth, 100.0f, 1.0f, 1.0f * cellWidth};
    g_towerStats[TOWER_SPLASH][3] = (TowerLevelStats){220, 2.8f * cellWidth, 140.0f, 1.1f, 1.1f * cellWidth};
}

void InitializeEnemyTypes() {
    enemyTypes[ENEMY_NORMAL] = (EnemyType){4.0f, COLOR_NEON_RED, 100.0f, 5};
    enemyTypes[ENEMY_SCOUT] = (EnemyType){8.0f, COLOR_NEON_ORANGE, 60.0f, 8};
    enemyTypes[ENEMY_TANK] = (EnemyType){2.0f, (Color){200, 0, 200, 255}, 400.0f, 15};
}

void LoadGameAudio() {
    InitAudioDevice();
    sndLaser = LoadSound("resources/laser.wav");
    sndExplosion = LoadSound("resources/explosion.wav");
    sndPlace = LoadSound("resources/place.wav");
    sndUpgrade = LoadSound("resources/upgrade.wav");
    sndError = LoadSound("resources/error.wav");
    sndHurt = LoadSound("resources/hurt.wav");
    music = LoadMusicStream("resources/music.ogg");
    SetMusicVolume(music, 0.4f);
    PlayMusicStream(music);
}

void UnloadGameAudio() {
    UnloadSound(sndLaser);
    UnloadSound(sndExplosion);
    UnloadSound(sndPlace);
    UnloadSound(sndUpgrade);
    UnloadSound(sndError);
    UnloadSound(sndHurt);
    UnloadMusicStream(music);
    CloseAudioDevice();
}

void InitializeGame() {
    playerHealth = PLAYER_START_HEALTH;
    playerMoney = PLAYER_START_MONEY;
    currentWaveNumber = 0;
    gameState = GAME_STATE_WAVE_TRANSITION;
    g_selectedTowerX = -1;
    g_selectedTowerY = -1;
    g_selectedBuildType = -1;
    gameSpeed = 1.0f;

    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            towers[x][y].active = false;
        }
    }
    
    InitializeTowerStats();
    InitializeEnemyTypes();
}

void RestartGame() {
    InitializeGame();
}

void CreateWave(int waveNumber) {
    activeWave.enemyCount = 10 + waveNumber * 5;
    if (activeWave.enemyCount > MAX_ENEMIES_PER_WAVE) {
        activeWave.enemyCount = MAX_ENEMIES_PER_WAVE;
    }
    activeWave.enemiesSpawned = 0;
    activeWave.spawnTimer = 0.0f;
    activeWave.isFinished = false;

    float healthMultiplier = 1.0f + (waveNumber - 1) * 0.20f;

    for (int i = 0; i < activeWave.enemyCount; i++) {
        activeWave.enemies[i].active = false;
        
        // Introduce new enemy types in later waves
        if (waveNumber > 8 && i % 5 == 0) {
            activeWave.enemies[i].type = ENEMY_TANK;
        } else if (waveNumber > 2 && i % 3 == 0) {
            activeWave.enemies[i].type = ENEMY_SCOUT;
        } else {
            activeWave.enemies[i].type = ENEMY_NORMAL;
        }
        
        activeWave.enemies[i].maxHealth = enemyTypes[activeWave.enemies[i].type].maxHealth * healthMultiplier;
        activeWave.enemies[i].speedMultiplier = 1.0f;
        activeWave.enemies[i].slowTimer = 0.0f;
    }
}

void FireProjectile(Vector2 startPos, Vector2 endPos, Color color, bool isSplash) {
    if (projectileCount < MAX_PROJECTILES) {
        projectiles[projectileCount].startPos = startPos;
        projectiles[projectileCount].endPos = endPos;
        projectiles[projectileCount].lifeTimer = 0.15f;
        projectiles[projectileCount].color = color;
        projectiles[projectileCount].isSplash = isSplash;
        projectileCount++;
    }
}

void UpdateTowers(float dt) {
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            Tower *tower = &towers[x][y];
            if (!tower->active) continue;

            TowerLevelStats stats = g_towerStats[tower->type][tower->level];
            if (tower->fireCooldown > 0) {
                tower->fireCooldown -= dt;
            }

            // SLOW TOWER LOGIC (Area of Effect, no target)
            if (tower->type == TOWER_SLOW) {
                if (tower->fireCooldown <= 0) {
                    Vector2 towerScreenPos = {(tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f};
                    for (int i = 0; i < activeWave.enemyCount; i++) {
                        Enemy *enemy = &activeWave.enemies[i];
                        if (!enemy->active) continue;
                        if (CheckCollisionPointCircle(enemy->pos, towerScreenPos, stats.range)) {
                            enemy->speedMultiplier = stats.damage; // Using damage field for slow %
                            enemy->slowTimer = 1.0f / stats.fireRate + 0.1f; // Resets every pulse
                        }
                    }
                    tower->fireCooldown = 1.0f / stats.fireRate;
                }
                continue; // Skip targeting logic for slow tower
            }

            // TARGETING LOGIC (for Gun and Splash)
            if (tower->targetIndex != -1) {
                Enemy *target = &activeWave.enemies[tower->targetIndex];
                Vector2 towerScreenPos = {(tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f};
                if (!target->active || Vector2DistanceSqr(towerScreenPos, target->pos) > (stats.range * stats.range)) {
                    tower->targetIndex = -1;
                }
            }

            if (tower->targetIndex == -1) {
                float minDistanceSqr = FLT_MAX;
                int closestEnemyIndex = -1;
                Vector2 towerScreenPos = {(tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f};
                for (int i = 0; i < activeWave.enemyCount; i++) {
                    Enemy *enemy = &activeWave.enemies[i];
                    if (!enemy->active) continue;
                    float distanceSqr = Vector2DistanceSqr(towerScreenPos, enemy->pos);
                    if (distanceSqr <= (stats.range * stats.range) && distanceSqr < minDistanceSqr) {
                        minDistanceSqr = distanceSqr;
                        closestEnemyIndex = i;
                    }
                }
                tower->targetIndex = closestEnemyIndex;
            }
            
            // FIRING LOGIC
            if (tower->targetIndex != -1 && tower->fireCooldown <= 0) {
                Enemy *target = &activeWave.enemies[tower->targetIndex];
                Vector2 towerScreenPos = {(tower->pos.x * cellWidth) + cellWidth / 2.0f, (tower->pos.y * cellHeight) + cellHeight / 2.0f};
                
                if (tower->type == TOWER_GUN) {
                    target->health -= stats.damage;
                    FireProjectile(towerScreenPos, target->pos, COLOR_NEON_WHITE, false);
                    PlaySound(sndLaser);
                } else if (tower->type == TOWER_SPLASH) {
                    // Damage all enemies in splash radius of the target
                    for (int i = 0; i < activeWave.enemyCount; i++) {
                        Enemy *splashTarget = &activeWave.enemies[i];
                        if (splashTarget->active && Vector2DistanceSqr(target->pos, splashTarget->pos) < (stats.splashRadius * stats.splashRadius)) {
                            splashTarget->health -= stats.damage;
                        }
                    }
                    FireProjectile(towerScreenPos, target->pos, COLOR_NEON_ORANGE, true);
                    PlaySound(sndExplosion);
                }

                tower->fireCooldown = 1.0f / stats.fireRate;

                // Check all enemies for death after splash damage
                for (int i = 0; i < activeWave.enemyCount; i++) {
                    Enemy *enemy = &activeWave.enemies[i];
                    if (enemy->active && enemy->health <= 0) {
                        enemy->active = false;
                        playerMoney += enemyTypes[enemy->type].money;
                        if (tower->targetIndex == i) {
                            tower->targetIndex = -1;
                        }
                    }
                }
            }
        }
    }
}

void UpdateWave(EnemyWave *wave, float dt) {
    if (wave->enemiesSpawned >= wave->enemyCount) {
        wave->isFinished = true;
        return;
    }
    wave->spawnTimer += dt;
    if (wave->spawnTimer >= SPAWN_INTERVAL) {
        wave->spawnTimer = 0;
        Enemy *enemy = &wave->enemies[wave->enemiesSpawned];
        enemy->active = true;
        enemy->pos = (Vector2){(path[0].x * cellWidth) + cellWidth / 2.0f, (path[0].y * cellHeight) + cellHeight / 2.0f};
        enemy->pathIndex = 0;
        enemy->moveTimer = 0.0f;
        enemy->health = enemy->maxHealth;
        wave->enemiesSpawned++;
    }
}

void UpdateEnemies(EnemyWave *wave, float dt) {
    for (int i = 0; i < wave->enemyCount; i++) {
        Enemy *enemy = &wave->enemies[i];
        if (!enemy->active) continue;

        // Update slow effect
        if (enemy->slowTimer > 0) {
            enemy->slowTimer -= dt;
        } else {
            enemy->speedMultiplier = 1.0f;
        }

        if (enemy->pathIndex >= pathLength - 1) {
            enemy->active = false;
            playerHealth--;
            PlaySound(sndHurt);
            if (playerHealth <= 0) {
                playerHealth = 0;
                gameState = GAME_STATE_GAME_OVER;
            }
            continue;
        }

        float effectiveSpeed = enemyTypes[enemy->type].speed * enemy->speedMultiplier;
        float moveInterval = 1.0f / effectiveSpeed;
        enemy->moveTimer += dt;

        Vector2 startNode = path[enemy->pathIndex];
        Vector2 targetNode = path[enemy->pathIndex + 1];
        Vector2 startScreenPos = {startNode.x * cellWidth + cellWidth / 2.0f, startNode.y * cellHeight + cellHeight / 2.0f};
        Vector2 targetScreenPos = {targetNode.x * cellWidth + cellWidth / 2.0f, targetNode.y * cellHeight + cellHeight / 2.0f};

        float lerpAmount = enemy->moveTimer / moveInterval;
        if (lerpAmount >= 1.0f) {
            lerpAmount = 1.0f;
            enemy->pathIndex++;
            enemy->moveTimer -= moveInterval;
        }
        
        enemy->pos = Vector2Lerp(startScreenPos, targetScreenPos, lerpAmount);
    }
}

void CheckWaveCompletion() {
    if (!activeWave.isFinished) return;
    if (currentWaveNumber > MAX_WAVES) {
        gameState = GAME_STATE_VICTORY;
        return;
    }

    for (int i = 0; i < activeWave.enemyCount; i++) {
        if (activeWave.enemies[i].active) return;
    }

    gameState = GAME_STATE_WAVE_TRANSITION;
}

void HandleInput() {
    // Fast Forward Toggle
    if (IsKeyPressed(KEY_F)) {
        gameSpeed = (gameSpeed == 1.0f) ? 2.0f : 1.0f;
    }
    
    // Deselection
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        g_selectedBuildType = -1;
        g_selectedTowerX = -1;
        g_selectedTowerY = -1;
    }

    Vector2 mousePos = GetMousePosition();
    int gridX = (int)(mousePos.x / cellWidth);
    int gridY = (int)(mousePos.y / cellHeight);
    bool isMouseOnGameArea = (mousePos.x < GAME_AREA_WIDTH && mousePos.x > 0);

    // Tower Placement / Selection
    if (isMouseOnGameArea && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (g_selectedBuildType != -1) { // Trying to build
            if (walls[gridX][gridY] && !towers[gridX][gridY].active) {
                int cost = g_towerStats[g_selectedBuildType][0].cost;
                if (playerMoney >= cost) {
                    playerMoney -= cost;
                    Tower *newTower = &towers[gridX][gridY];
                    newTower->active = true;
                    newTower->pos = (Vector2){(float)gridX, (float)gridY};
                    newTower->type = g_selectedBuildType;
                    newTower->level = 0;
                    newTower->fireCooldown = 0.0f;
                    newTower->targetIndex = -1;
                    g_selectedBuildType = -1; // Deselect after building
                    PlaySound(sndPlace);
                } else {
                    PlaySound(sndError);
                }
            } else {
                PlaySound(sndError);
            }
        } else { // Trying to select an existing tower
            if (towers[gridX][gridY].active) {
                g_selectedTowerX = gridX;
                g_selectedTowerY = gridY;
            } else {
                g_selectedTowerX = -1;
                g_selectedTowerY = -1;
            }
        }
    }
}

void UpdateGame(float dt) {
    UpdateMusicStream(music);
    dt *= gameSpeed; // Apply game speed multiplier

    switch (gameState) {
        case GAME_STATE_PLAYING:
            UpdateWave(&activeWave, dt);
            UpdateEnemies(&activeWave, dt);
            UpdateTowers(dt);
            CheckWaveCompletion();
            break;
        case GAME_STATE_WAVE_TRANSITION:
            if (IsKeyPressed(KEY_SPACE)) {
                currentWaveNumber++;
                CreateWave(currentWaveNumber);
                gameState = GAME_STATE_PLAYING;
            }
            break;
        case GAME_STATE_GAME_OVER:
            if (IsKeyPressed(KEY_R)) RestartGame();
            break;
        case GAME_STATE_VICTORY:
            if (IsKeyPressed(KEY_R)) RestartGame();
            break;
    }
    
    if (gameState != GAME_STATE_GAME_OVER && gameState != GAME_STATE_VICTORY) {
        HandleInput();
    }
}

// --- Main Entry Point ---
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tower Defense: Evolved");
    SetTargetFPS(60);

    Vector2 startPos, endPos;
    if (!LoadMap("map.txt", &startPos, &endPos) || !FindPathBFS(startPos, endPos)) {
        TraceLog(LOG_ERROR, "Map or Path not valid. Exiting.");
        CloseWindow();
        return 1;
    }

    InitializeGame();
    LoadGameAudio();

    RenderTexture2D backgroundTexture = LoadRenderTexture(GAME_AREA_WIDTH, SCREEN_HEIGHT);
    BeginTextureMode(backgroundTexture);
        ClearBackground(COLOR_BLACK);
        for (int y = 0; y <= GRID_SIZE; y++) DrawLine(0, y * cellHeight, GAME_AREA_WIDTH, y * cellHeight, COLOR_BG_GRID);
        for (int x = 0; x <= GRID_SIZE; x++) DrawLine(x * cellWidth, 0, x * cellWidth, SCREEN_HEIGHT, COLOR_BG_GRID);
        for (int i = 0; i < pathLength - 1; i++) {
            Vector2 p1 = {path[i].x * cellWidth + cellWidth/2, path[i].y * cellHeight + cellHeight/2};
            Vector2 p2 = {path[i+1].x * cellWidth + cellWidth/2, path[i+1].y * cellHeight + cellHeight/2};
            DrawLineEx(p1, p2, 10, COLOR_PATH);
        }
        for (int x = 0; x < GRID_SIZE; x++) {
            for (int y = 0; y < GRID_SIZE; y++) {
                if (walls[x][y]) DrawWall(x, y);
            }
        }
    EndTextureMode();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        UpdateGame(dt);
        
        BeginDrawing();
        ClearBackground(COLOR_BLACK);
        DrawTextureRec(backgroundTexture.texture, (Rectangle){0, 0, (float)backgroundTexture.texture.width, (float)-backgroundTexture.texture.height}, (Vector2){0, 0}, WHITE);
        
        DrawEnemies(&activeWave);
        DrawTowers();
        UpdateAndDrawProjectiles(dt * gameSpeed);

        // Draw placement/selection highlights
        Vector2 mousePos = GetMousePosition();
        int gridX = (int)(mousePos.x / cellWidth);
        int gridY = (int)(mousePos.y / cellHeight);
        bool isMouseOnGameArea = (mousePos.x < GAME_AREA_WIDTH && mousePos.x > 0 && gridX < GRID_SIZE && gridY < GRID_SIZE);

        if (isMouseOnGameArea && gameState != GAME_STATE_GAME_OVER && gameState != GAME_STATE_VICTORY) {
            if (g_selectedBuildType != -1 && walls[gridX][gridY]) {
                Color highlightColor = (playerMoney >= g_towerStats[g_selectedBuildType][0].cost) ? COLOR_NEON_CYAN : COLOR_NEON_RED;
                DrawRectangleLinesEx((Rectangle){(float)gridX * cellWidth, (float)gridY * cellHeight, (float)cellWidth, (float)cellHeight}, 3, Fade(highlightColor, 0.7f));
                DrawCircleLines(gridX * cellWidth + cellWidth / 2, gridY * cellHeight + cellHeight / 2, g_towerStats[g_selectedBuildType][0].range, Fade(highlightColor, 0.5f));
            }
        }
        if (g_selectedTowerX != -1) {
            Tower* tower = &towers[g_selectedTowerX][g_selectedTowerY];
            TowerLevelStats stats = g_towerStats[tower->type][tower->level];
            DrawCircleLines(g_selectedTowerX * cellWidth + cellWidth / 2, g_selectedTowerY * cellHeight + cellHeight / 2, stats.range, Fade(COLOR_NEON_WHITE, 0.8f));
        }
        
        DrawGameUI();

        EndDrawing();
    }

    UnloadRenderTexture(backgroundTexture);
    UnloadGameAudio();
    CloseWindow();
    return 0;
}

// --- Drawing & UI Functions ---

void UpdateAndDrawProjectiles(float dt) {
    for (int i = 0; i < projectileCount; i++) {
        projectiles[i].lifeTimer -= dt;
        if (projectiles[i].lifeTimer > 0) {
            if (projectiles[i].isSplash) {
                DrawCircleV(projectiles[i].endPos, g_towerStats[TOWER_SPLASH][0].splashRadius * (1.0 - (projectiles[i].lifeTimer / 0.15f)), Fade(projectiles[i].color, projectiles[i].lifeTimer * 8.0f));
            } else {
                DrawLineEx(projectiles[i].startPos, projectiles[i].endPos, 3, Fade(projectiles[i].color, projectiles[i].lifeTimer * 10));
            }
        } else {
            projectiles[i] = projectiles[projectileCount - 1];
            projectileCount--;
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
                Vector2 center = {screenX + cellWidth/2.0f, screenY + cellHeight/2.0f};
                
                // Base platform
                DrawRectangle(screenX + 15, screenY + 15, cellWidth - 30, cellHeight - 30, DARKGRAY);

                switch(towers[x][y].type) {
                    case TOWER_GUN:
                        DrawPoly(center, 3, cellWidth/2.5f, 0, COLOR_NEON_ORANGE);
                        DrawPolyLinesEx(center, 3, cellWidth/2.5f, 0, 2, COLOR_NEON_CYAN);
                        break;
                    case TOWER_SLOW:
                        DrawCircleV(center, cellWidth/3.0f, COLOR_FROST);
                        DrawCircleV(center, cellWidth/4.5f, COLOR_NEON_CYAN);
                        break;
                    case TOWER_SPLASH:
                        DrawRectangleV((Vector2){center.x - 15, center.y - 15}, (Vector2){30,30}, ORANGE);
                        DrawCircleV(center, 10, DARKGRAY);
                        break;
                }
                // Draw level indicator
                for (int i = 0; i <= towers[x][y].level; i++) {
                    DrawCircle(screenX + 10 + i*6, screenY + cellHeight - 10, 2, GOLD);
                }
            }
        }
    }
}

void DrawWall(int cellX, int cellY) {
    float x = cellX * cellWidth;
    float y = cellY * cellHeight;
    DrawRectangleV((Vector2){x + border_buff, y + border_buff}, (Vector2){cellWidth - border_buff * 2, cellHeight - border_buff * 2}, COLOR_WALL);
}

void DrawEnemies(const EnemyWave *wave) {
    for (int i = 0; i < wave->enemyCount; i++) {
        const Enemy *enemy = &wave->enemies[i];
        if (enemy->active) {
            Color color = enemyTypes[enemy->type].color;
            if (enemy->slowTimer > 0) color = ColorBrightness(color, -0.4f);
            DrawCircleV(enemy->pos, cellWidth / 3.5f, color);
            if (enemy->slowTimer > 0) DrawCircleLines(enemy->pos.x, enemy->pos.y, cellWidth / 3.0f, COLOR_FROST);

            float healthPercentage = enemy->health / enemy->maxHealth;
            float barWidth = cellWidth * 0.8f;
            float barHeight = 8.0f;
            Vector2 barPos = {enemy->pos.x - barWidth / 2, enemy->pos.y - cellHeight / 2.0f - barHeight};
            DrawRectangleV(barPos, (Vector2){barWidth, barHeight}, Fade(BLACK, 0.7f));
            DrawRectangleV(barPos, (Vector2){barWidth * healthPercentage, barHeight}, COLOR_HEALTH_GREEN);
            DrawRectangleLinesEx((Rectangle){barPos.x, barPos.y, barWidth, barHeight}, 1, Fade(COLOR_NEON_CYAN, 0.8f));
        }
    }
}

void DrawGameUI() {
    // Main UI Panel
    DrawRectangle(GAME_AREA_WIDTH, 0, SCREEN_WIDTH - GAME_AREA_WIDTH, SCREEN_HEIGHT, COLOR_UI_PANEL);
    DrawLine(GAME_AREA_WIDTH, 0, GAME_AREA_WIDTH, SCREEN_HEIGHT, COLOR_UI_ACCENT);

    // Stats Display
    int uiX = GAME_AREA_WIDTH + 15;
    DrawText(TextFormat("WAVE: %d / %d", currentWaveNumber > 0 ? currentWaveNumber : 1, MAX_WAVES), uiX, 20, 20, COLOR_NEON_CYAN);
    DrawText(TextFormat("HEALTH: %d", playerHealth), uiX, 50, 20, COLOR_HEALTH_GREEN);
    DrawText(TextFormat("MONEY: $%d", playerMoney), uiX, 80, 20, COLOR_NEON_ORANGE);
    DrawText(TextFormat("SPEED: %.0fx", gameSpeed), uiX, 110, 20, COLOR_NEON_WHITE);
    DrawText("F: Toggle Speed", uiX, 135, 10, GRAY);
    
    // UI Separator
    DrawLine(GAME_AREA_WIDTH, 160, SCREEN_WIDTH, 160, COLOR_UI_ACCENT);

    if (g_selectedTowerX != -1) {
        DrawSelectionUI();
    } else {
        DrawBuildUI();
    }
    
    // Game State Information
    if (gameState == GAME_STATE_WAVE_TRANSITION) {
        const char* text = "PRESS SPACE to START WAVE";
        int textWidth = MeasureText(text, 30);
        DrawText(text, GAME_AREA_WIDTH / 2 - textWidth / 2, SCREEN_HEIGHT / 2 - 15, 30, COLOR_NEON_WHITE);
    } else if (gameState == GAME_STATE_GAME_OVER) {
        DrawRectangle(0, 0, GAME_AREA_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.7f));
        DrawText("GAME OVER", GAME_AREA_WIDTH / 2 - MeasureText("GAME OVER", 60) / 2, SCREEN_HEIGHT / 2 - 40, 60, COLOR_NEON_RED);
        DrawText("Press 'R' to Restart", GAME_AREA_WIDTH / 2 - MeasureText("Press 'R' to Restart", 30) / 2, SCREEN_HEIGHT / 2 + 30, 30, COLOR_NEON_WHITE);
    } else if (gameState == GAME_STATE_VICTORY) {
        DrawRectangle(0, 0, GAME_AREA_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.7f));
        DrawText("VICTORY!", GAME_AREA_WIDTH / 2 - MeasureText("VICTORY!", 60) / 2, SCREEN_HEIGHT / 2 - 40, 60, (Color){0, 255, 120, 255});
        DrawText("Press 'R' to Play Again", GAME_AREA_WIDTH / 2 - MeasureText("Press 'R' to Play Again", 30) / 2, SCREEN_HEIGHT / 2 + 30, 30, COLOR_NEON_WHITE);
    }
}

void DrawBuildUI() {
    int uiX = GAME_AREA_WIDTH + 15;
    int yPos = 180;
    DrawText("BUILD TOWERS", uiX, yPos, 20, COLOR_UI_ACCENT);
    yPos += 30;

    for (int i = 0; i < TOWER_TYPE_COUNT; i++) {
        Rectangle buildBox = {uiX - 5, yPos, 180, 60};
        bool canAfford = playerMoney >= g_towerStats[i][0].cost;
        Color boxColor = (g_selectedBuildType == i) ? COLOR_UI_ACCENT : (canAfford ? COLOR_NEON_CYAN : COLOR_NEON_RED);

        DrawRectangleLinesEx(buildBox, 2, boxColor);
        DrawText(g_towerNames[i], buildBox.x + 10, buildBox.y + 10, 20, canAfford ? WHITE : GRAY);
        DrawText(TextFormat("$%d", g_towerStats[i][0].cost), buildBox.x + 10, buildBox.y + 35, 20, canAfford ? COLOR_NEON_ORANGE : GRAY);
        
        if (CheckCollisionPointRec(GetMousePosition(), buildBox) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (canAfford) {
                g_selectedBuildType = i;
                g_selectedTowerX = -1; g_selectedTowerY = -1; // Deselect any tower
            } else {
                PlaySound(sndError);
            }
        }
        yPos += 70;
    }
}

void DrawSelectionUI() {
    int uiX = GAME_AREA_WIDTH + 15;
    int yPos = 180;
    Tower* tower = &towers[g_selectedTowerX][g_selectedTowerY];
    TowerLevelStats currentStats = g_towerStats[tower->type][tower->level];

    DrawText("TOWER STATS", uiX, yPos, 20, COLOR_UI_ACCENT);
    yPos += 30;
    DrawText(TextFormat("%s Lvl %d", g_towerNames[tower->type], tower->level + 1), uiX, yPos, 20, WHITE);
    yPos += 30;
    DrawText(TextFormat("Range: %.0f", currentStats.range), uiX, yPos, 15, GRAY);
    yPos += 20;
    if (tower->type == TOWER_SLOW) {
        DrawText(TextFormat("Slow: %d%%", 100 - (int)(currentStats.damage * 100)), uiX, yPos, 15, GRAY);
    } else {
        DrawText(TextFormat("Damage: %.0f", currentStats.damage), uiX, yPos, 15, GRAY);
    }
    yPos += 20;
    DrawText(TextFormat("Fire Rate: %.1f/s", currentStats.fireRate), uiX, yPos, 15, GRAY);
    yPos += 40;

    // Upgrade Button
    if (tower->level < MAX_TOWER_LEVEL - 1) {
        TowerLevelStats nextStats = g_towerStats[tower->type][tower->level + 1];
        bool canAfford = playerMoney >= nextStats.cost;
        Rectangle upgradeBox = {uiX, yPos, 170, 40};
        DrawRectangleLinesEx(upgradeBox, 2, canAfford ? COLOR_NEON_CYAN : GRAY);
        DrawText(TextFormat("UPGRADE ($%d)", nextStats.cost), upgradeBox.x + 10, upgradeBox.y + 12, 20, canAfford ? WHITE : GRAY);
        if (CheckCollisionPointRec(GetMousePosition(), upgradeBox) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            UpgradeSelectedTower();
        }
    } else {
        DrawText("Max Level Reached", uiX, yPos, 20, GOLD);
    }
    yPos += 50;

    // Sell Button
    int sellValue = 0;
    for(int i = 0; i <= tower->level; i++) sellValue += g_towerStats[tower->type][i].cost;
    sellValue *= 0.7f;
    Rectangle sellBox = {uiX, yPos, 170, 40};
    DrawRectangleLinesEx(sellBox, 2, COLOR_NEON_RED);
    DrawText(TextFormat("SELL ($%d)", sellValue), sellBox.x + 10, sellBox.y + 12, 20, WHITE);
    if (CheckCollisionPointRec(GetMousePosition(), sellBox) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        SellSelectedTower();
    }
}

void UpgradeSelectedTower() {
    if (g_selectedTowerX == -1) return;
    Tower* tower = &towers[g_selectedTowerX][g_selectedTowerY];
    if (tower->level < MAX_TOWER_LEVEL - 1) {
        int cost = g_towerStats[tower->type][tower->level + 1].cost;
        if (playerMoney >= cost) {
            playerMoney -= cost;
            tower->level++;
            PlaySound(sndUpgrade);
        } else {
            PlaySound(sndError);
        }
    }
}

void SellSelectedTower() {
    if (g_selectedTowerX == -1) return;
    Tower* tower = &towers[g_selectedTowerX][g_selectedTowerY];
    int sellValue = 0;
    for(int i = 0; i <= tower->level; i++) sellValue += g_towerStats[tower->type][i].cost;
    sellValue *= 0.7f;
    
    playerMoney += sellValue;
    tower->active = false;
    g_selectedTowerX = -1;
    g_selectedTowerY = -1;
    PlaySound(sndPlace);
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
    char line[GRID_SIZE + 3]; // +3 for \r, \n, \0
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
    pathLength = 0;
    Vector2 current = end;
    while (current.x != -1) {
        path[pathLength++] = current;
        current = parent[(int)current.x][(int)current.y];
    }
    for (int i = 0; i < pathLength / 2; i++) {
        Vector2 temp = path[i];
        path[i] = path[pathLength - 1 - i];
        path[pathLength - 1 - i] = temp;
    }
    return true;
}