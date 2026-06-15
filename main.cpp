#define _CRT_SECURE_NO_WARNINGS
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstdio>  // Поддержка sprintf

// --- ФИЗИЧЕСКИЕ И ГЛОБАЛЬНЫЕ СТРУКТУРЫ ---

struct Particle {
    float x, y;
    float vx, vy;
    int group;
};

struct GroupConfig {
    std::string name;
    int count;
    float color[3];
    float rules[6];
    float radii[6];
};

// Глобальные конфигурационные параметры
struct SimulationConfig {
    int width = 800;
    int height = 600;
    bool paused = false;
    float simulationSpeed = 1.0f;
    float particleSize = 2.0f;
    float interactionRadius = 50.0f;
    float velocityDamping = 0.50f;
    float forceMultiplier = 1.00f;
    float backgroundColor[3] = {0.0f, 0.0f, 0.0f};
    float repel = 1.0f;
    float minRadiusRatio = 0.20f;
    int maxParticles = 2000;
} config;

// Инициализация исходных групп (полное соответствие вашему HTML-файлу)
GroupConfig groups[6] = {
    { "yellow", 200, {1.0f, 1.0f, 0.0f}, {0.15f, -0.20f, 0.0f, 0.0f, 0.0f, 0.0f}, {50.f, 50.f, 50.f, 50.f, 50.f, 50.f} },
    { "red",    200, {1.0f, 0.0f, 0.0f}, {0.0f, -0.10f, -0.34f, 0.0f, 0.0f, 0.0f}, {50.f, 50.f, 50.f, 50.f, 50.f, 50.f} },
    { "green",  200, {0.0f, 1.0f, 0.0f}, {0.34f, -0.17f, -0.32f, 0.0f, 0.0f, 0.0f}, {50.f, 50.f, 50.f, 50.f, 50.f, 50.f} },
    { "blue",   200, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {50.f, 50.f, 50.f, 50.f, 50.f, 50.f} },
    { "purple", 200, {0.5f, 0.0f, 0.5f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {50.f, 50.f, 50.f, 50.f, 50.f, 50.f} },
    { "orange", 200, {1.0f, 0.6f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {50.f, 50.f, 50.f, 50.f, 50.f, 50.f} }
};

const std::vector<std::string> groupNames = { "yellow", "red", "green", "blue", "purple", "orange" };

std::vector<Particle> particles;
std::vector<int> gridHead;
std::vector<int> gridNext;
int gridCols = 0;
int gridRows = 0;
float cellSize = 50.0f;

// Матрицы физики на CPU
float ruleMatrixFlat[36];
float radiusMatrixFlat[36];
float radiusSqMatrixFlat[36];
float minRadiusMatrixFlat[36];
float invMinRadiusMatrixFlat[36];
float midMatrixFlat[36];
float slopeMatrixFlat[36];
int groupMaxSearchCells[6];
float groupMaxRadiusSq[6];

// Настройки камеры
struct Camera {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float zoom = 1.0f;
    float targetOffsetX = 0.0f;
    float targetOffsetY = 0.0f;
    float targetZoom = 1.0f;
} camera;

// Встроенная функция линейной интерполяции для C++17
inline float lerp(float start, float end, float amt) {
    return (1.0f - amt) * start + amt * end;
}

// Переменные профайлера
float benchGridBuild = 0.0f;
float benchPhysicsForces = 0.0f;
float benchIntegration = 0.0f;
float benchRendering = 0.0f;
float benchTotal = 0.0f;
const float benchAlpha = 0.05f;

// Генератор сидов Mulberry32
uint32_t seedVal = 123456;
uint32_t currentSeed = 123456;
char seedInputBuf[64] = "123456";

uint32_t mulberry32() {
    currentSeed += 0x6D2B79F5;
    uint32_t t = currentSeed;
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

float nextRand() {
    return (float)mulberry32() / 4294967296.0f;
}

uint32_t hashString(const std::string& str) {
    uint32_t hash = 0;
    for (char c : str) {
        hash = (hash << 5) - hash + c;
    }
    return hash;
}

void centerCamera() {
    camera.targetZoom = 1.0f;
    camera.targetOffsetX = (800.0f - config.width) / 2.0f;
    camera.targetOffsetY = (600.0f - config.height) / 2.0f;
    camera.zoom = camera.targetZoom;
    camera.offsetX = camera.targetOffsetX;
    camera.offsetY = camera.targetOffsetY;
}

void updateLookupTables() {
    const float ratio = config.minRadiusRatio;
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            float rule = groups[i].rules[j];
            float maxRadius = groups[i].radii[j];
            float minRadius = maxRadius * ratio;
            float mid = (minRadius + maxRadius) * 0.5f;
            float slope = (mid - minRadius) > 0.0f ? (rule / (mid - minRadius)) : 0.0f;

            int idx = i * 6 + j;
            ruleMatrixFlat[idx] = rule;
            radiusMatrixFlat[idx] = maxRadius;
            radiusSqMatrixFlat[idx] = maxRadius * maxRadius;
            minRadiusMatrixFlat[idx] = minRadius;
            invMinRadiusMatrixFlat[idx] = minRadius > 0.0f ? (1.0f / minRadius) : 0.0f;
            midMatrixFlat[idx] = mid;
            slopeMatrixFlat[idx] = slope;
        }
    }

    for (int i = 0; i < 6; i++) {
        float maxR = 10.0f;
        for (int j = 0; j < 6; j++) {
            float r = radiusMatrixFlat[i * 6 + j];
            if (r > maxR) maxR = r;
        }
        groupMaxSearchCells[i] = (int)std::ceil(maxR / cellSize);
        groupMaxRadiusSq[i] = maxR * maxR;
    }
}

void initSpatialGrid() {
    gridCols = (int)(config.width / cellSize) + 1;
    gridRows = (int)(config.height / cellSize) + 1;
    gridHead.resize(gridCols * gridRows);
    gridNext.resize(config.maxParticles);
}

// ПРОПОРЦИОНАЛЬНОЕ РАСПРЕДЕЛЕНИЕ С ЦЕЛЬЮ СОХРАНЕНИЯ ВСЕХ ЦВЕТОВ
void populateParticles() {
    int maxLimit = config.maxParticles;
    int totalRequested = 0;
    for (int i = 0; i < 6; i++) {
        totalRequested += groups[i].count;
    }

    float scale = totalRequested > maxLimit ? ((float)maxLimit / totalRequested) : 1.0f;

    particles.clear();
    particles.reserve(maxLimit);

    for (int i = 0; i < 6; i++) {
        int countForThisGroup = (int)std::floor(groups[i].count * scale);
        for (int c = 0; c < countForThisGroup; c++) {
            if (particles.size() >= maxLimit) break;
            Particle p;
            p.x = nextRand() * (config.width - 20) + 10;
            p.y = nextRand() * (config.height - 20) + 10;
            p.vx = (nextRand() - 0.5f) * 0.5f;
            p.vy = (nextRand() - 0.5f) * 0.5f;
            p.group = i;
            particles.push_back(p);
        }
    }
    updateLookupTables();
    initSpatialGrid();
}

void randomizeParticleCountsInternal() {
    float totalFraction = 0.0f;
    float fractions[6];
    for (int i = 0; i < 6; i++) {
        fractions[i] = nextRand();
        totalFraction += fractions[i];
    }
    
    int particlesToDistribute = config.maxParticles;
    for (int i = 0; i < 6; i++) {
        float norm = totalFraction > 0.0f ? fractions[i] / totalFraction : 0.1666f;
        int count = (int)std::floor(norm * config.maxParticles);
        groups[i].count = count;
        particlesToDistribute -= count;
    }
    if (particlesToDistribute > 0) {
        groups[0].count += particlesToDistribute;
    }
}

void randomizeRulesInternal() {
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            groups[i].rules[j] = std::round((nextRand() * 2.0f - 1.0f) * 100.0f) / 100.0f;
            groups[i].radii[j] = std::floor(nextRand() * (150.0f - 10.0f + 1.0f) + 10.0f);
        }
    }
}

void applySeed(const std::string& seedStr) {
    uint32_t countsSeed = hashString(seedStr + "_counts");
    uint32_t rulesSeed = hashString(seedStr + "_rules");
    uint32_t positionsSeed = hashString(seedStr + "_positions");

    currentSeed = countsSeed;
    randomizeParticleCountsInternal();

    currentSeed = rulesSeed;
    randomizeRulesInternal();

    currentSeed = positionsSeed;
    populateParticles();
}

void resetSimulation() {
    uint32_t positionsSeed = hashString(std::string(seedInputBuf) + "_positions");
    currentSeed = positionsSeed;
    populateParticles();
}

// --- ФИЗИЧЕСКИЙ ШАГ НА CPU ---

void updatePhysics() {
    if (config.paused || particles.empty()) return;

    auto tStart = std::chrono::high_resolution_clock::now();

    // 1. Построение сетки (Сверхбыстрое кэширование)
    std::fill(gridHead.begin(), gridHead.end(), -1);
    int pSize = (int)particles.size();
    float wLimit = config.width - 1.0f;
    float hLimit = config.height - 1.0f;

    for (int i = 0; i < pSize; i++) {
        float x = particles[i].x;
        float y = particles[i].y;
        if (x < 0.0f) x = 0.0f; else if (x > wLimit) x = wLimit;
        if (y < 0.0f) y = 0.0f; else if (y > hLimit) y = hLimit;

        int cX = (int)(x / cellSize);
        int cY = (int)(y / cellSize);
        int cellIdx = cY * gridCols + cX;

        gridNext[i] = gridHead[cellIdx];
        gridHead[cellIdx] = i;
    }

    auto tGrid = std::chrono::high_resolution_clock::now();

    // 2. Вычисление сил взаимодействия
    const float forceMult = config.forceMultiplier;
    const float dampingVal = config.velocityDamping;
    const float repelVal = config.repel;

    for (int i = 0; i < pSize; i++) {
        float totalFx = 0.0f;
        float totalFy = 0.0f;

        const int currentGroupIdx = particles[i].group;
        const float pxi = particles[i].x;
        const float pyi = particles[i].y;

        int cellX = (int)(pxi / cellSize);
        if (cellX < 0) cellX = 0; else if (cellX >= gridCols) cellX = gridCols - 1;
        int cellY = (int)(pyi / cellSize);
        if (cellY < 0) cellY = 0; else if (cellY >= gridRows) cellY = gridRows - 1;

        const int searchRange = groupMaxSearchCells[currentGroupIdx];
        const float currentGroupMaxRadiusSq = groupMaxRadiusSq[currentGroupIdx];

        int minCellX = cellX - searchRange; if (minCellX < 0) minCellX = 0;
        int maxCellX = cellX + searchRange; if (maxCellX >= gridCols) maxCellX = gridCols - 1;
        int minCellY = cellY - searchRange; if (minCellY < 0) minCellY = 0;
        int maxCellY = cellY + searchRange; if (maxCellY >= gridRows) maxCellY = gridRows - 1;

        for (int queryCellY = minCellY; queryCellY <= maxCellY; queryCellY++) {
            int rowOffset = queryCellY * gridCols;
            for (int queryCellX = minCellX; queryCellX <= maxCellX; queryCellX++) {
                int neighborIdx = gridHead[rowOffset + queryCellX];

                while (neighborIdx != -1) {
                    if (i == neighborIdx) {
                        neighborIdx = gridNext[neighborIdx];
                        continue;
                    }

                    float dx = particles[neighborIdx].x - pxi;
                    float dy = particles[neighborIdx].y - pyi;
                    float distSq = dx * dx + dy * dy;

                    if (distSq >= currentGroupMaxRadiusSq) {
                        neighborIdx = gridNext[neighborIdx];
                        continue;
                    }

                    int neighborGroupIdx = particles[neighborIdx].group;
                    int lookupIdx = (currentGroupIdx * 6) + neighborGroupIdx;
                    float maxRadiusSq = radiusSqMatrixFlat[lookupIdx];

                    if (distSq > 0.0001f && distSq < maxRadiusSq) {
                        float dist = std::sqrt(distSq);
                        float minRadius = minRadiusMatrixFlat[lookupIdx];

                        float force = 0.0f;
                        if (dist < minRadius) {
                            force = repelVal * (dist * invMinRadiusMatrixFlat[lookupIdx] - 1.0f);
                        } else {
                            float rule = ruleMatrixFlat[lookupIdx];
                            float mid = midMatrixFlat[lookupIdx];
                            float slope = slopeMatrixFlat[lookupIdx];
                            float diff = dist - mid;
                            force = -(slope * (diff >= 0.0f ? diff : -diff)) + rule;
                        }

                        if (force != 0.0f) {
                            float F = (force * forceMult) / dist;
                            totalFx += F * dx;
                            totalFy += F * dy;
                        }
                    }
                    neighborIdx = gridNext[neighborIdx];
                }
            }
        }

        particles[i].vx = (particles[i].vx + totalFx) * dampingVal;
        particles[i].vy = (particles[i].vy + totalFy) * dampingVal;
    }

    auto tForces = std::chrono::high_resolution_clock::now();

    // 3. Физический шаг интеграции Эйлера
    const float simSpeed = config.simulationSpeed;
    const float w = (float)config.width;
    const float h = (float)config.height;

    for (int i = 0; i < pSize; i++) {
        particles[i].x += particles[i].vx * simSpeed;
        particles[i].y += particles[i].vy * simSpeed;

        if (particles[i].x <= 0.0f) { particles[i].x = 0.01f; particles[i].vx *= -0.5f; }
        else if (particles[i].x >= w) { particles[i].x = w - 0.01f; particles[i].vx *= -0.5f; }

        if (particles[i].y <= 0.0f) { particles[i].y = 0.01f; particles[i].vy *= -0.5f; }
        else if (particles[i].y >= h) { particles[i].y = h - 0.01f; particles[i].vy *= -0.5f; }
    }

    auto tEnd = std::chrono::high_resolution_clock::now();

    // Расчет показателей бенчмарка (EMA фильтр сглаживания)
    float gridMs = std::chrono::duration<float, std::milli>(tGrid - tStart).count();
    float forcesMs = std::chrono::duration<float, std::milli>(tForces - tGrid).count();
    float integrationMs = std::chrono::duration<float, std::milli>(tEnd - tForces).count();

    benchGridBuild = benchGridBuild * (1.0f - benchAlpha) + gridMs * benchAlpha;
    benchPhysicsForces = benchPhysicsForces * (1.0f - benchAlpha) + forcesMs * benchAlpha;
    benchIntegration = benchIntegration * (1.0f - benchAlpha) + integrationMs * benchAlpha;
}

// --- ОТРИСОВКА СЦЕНЫ OpenGL ---

void drawScene() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 800, 600, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Очистка экрана
    glClearColor(config.backgroundColor[0], config.backgroundColor[1], config.backgroundColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glPushMatrix();
    glTranslatef(camera.offsetX, camera.offsetY, 0.0f);
    glScalef(camera.zoom, camera.zoom, 1.0f);

    // Рисование динамических границ поля симуляции
    glColor3f(0.35f, 0.35f, 0.35f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(0.0f, 0.0f);
    glVertex2f((float)config.width, 0.0f);
    glVertex2f((float)config.width, (float)config.height);
    glVertex2f(0.0f, (float)config.height);
    glEnd();

    // Пакетный рендеринг частиц через Vertex Arrays
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    int pSize = (int)particles.size();
    std::vector<float> verts(pSize * 2);
    std::vector<float> colors(pSize * 3);

    for (int i = 0; i < pSize; i++) {
        verts[i * 2 + 0] = particles[i].x;
        verts[i * 2 + 1] = particles[i].y;

        int g = particles[i].group;
        colors[i * 3 + 0] = groups[g].color[0];
        colors[i * 3 + 1] = groups[g].color[1];
        colors[i * 3 + 2] = groups[g].color[2];
    }

    glVertexPointer(2, GL_FLOAT, 0, verts.data());
    glColorPointer(3, GL_FLOAT, 0, colors.data());
    glPointSize(config.particleSize);
    glDrawArrays(GL_POINTS, 0, pSize);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glPopMatrix();
}

// --- УПРАВЛЕНИЕ МЫШЬЮ И КЛАВИАТУРОЙ ---

bool isDragging = false;
double lastMouseX, lastMouseY;

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    // Игнорируем зажатие мыши над интерфейсом ImGui
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            isDragging = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        } else {
            isDragging = false;
        }
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (isDragging) {
        double dx = xpos - lastMouseX;
        double dy = ypos - lastMouseY;
        camera.targetOffsetX += dx / camera.zoom;
        camera.targetOffsetY += dy / camera.zoom;
        lastMouseX = xpos;
        lastMouseY = ypos;
    }
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (yoffset > 0.0) camera.targetZoom *= 1.15f;
    else if (yoffset < 0.0) camera.targetZoom *= 0.85f;
}

// --- ПОСТРОЕНИЕ ИНТЕРФЕЙСА IMGUI ---

void drawImGuiInterface() {
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 1. Панель глобальных настроек
    ImGui::Begin("Глобальные настройки", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("Сид (Seed):");
    ImGui::InputText("##seed_val", seedInputBuf, IM_ARRAYSIZE(seedInputBuf));
    ImGui::SameLine();
    if (ImGui::Button("Применить")) {
        applySeed(std::string(seedInputBuf));
    }
    ImGui::SameLine();
    if (ImGui::Button("Новый сид")) {
        seedVal = (seedVal + 37) * 1234567;
        sprintf(seedInputBuf, "%u", seedVal);
        applySeed(std::string(seedInputBuf));
    }

    ImGui::Separator();
    
    // Динамический размер поля
    ImGui::Text("Границы сцены (Размер поля):");
    if (ImGui::SliderInt("Ширина поля", &config.width, 200, 3000)) {
        initSpatialGrid();
    }
    if (ImGui::SliderInt("Высота поля", &config.height, 200, 3000)) {
        initSpatialGrid();
    }

    ImGui::SliderFloat("Скорость симуляции", &config.simulationSpeed, 0.1f, 2.0f, "%.1f");
    ImGui::SliderFloat("Размер частиц", &config.particleSize, 1.0f, 10.0f, "%.0f");
    ImGui::SliderFloat("Радиус взаимодействия", &config.interactionRadius, 10.0f, 150.0f, "%.0f");
    ImGui::SliderFloat("Зона отталкивания (%)", &config.minRadiusRatio, 0.05f, 0.50f, "%.2f");
    ImGui::SliderFloat("Сила близкого отталкивания", &config.repel, 0.1f, 5.0f, "%.1f");
    ImGui::SliderFloat("Затухание скорости", &config.velocityDamping, 0.10f, 0.99f, "%.2f");
    ImGui::SliderFloat("Сила взаимодействия", &config.forceMultiplier, 0.01f, 5.00f, "%.2f");
    ImGui::ColorEdit3("Фон", config.backgroundColor);

    int maxP = config.maxParticles;
    if (ImGui::InputInt("Макс. количество частиц", &maxP)) {
        if (maxP < 10) maxP = 10;
        if (maxP > ABSOLUTE_MAX_PARTICLES) maxP = ABSOLUTE_MAX_PARTICLES;
        config.maxParticles = maxP;
        populateParticles();
    }

    if (ImGui::Button("Сбросить симуляцию")) {
        resetSimulation();
    }
    ImGui::SameLine();
    if (ImGui::Button(config.paused ? "Продолжить" : "Пауза")) {
        config.paused = !config.paused;
    }
    ImGui::SameLine();
    if (ImGui::Button("Случайные свойства")) {
        seedVal = (seedVal + 11) * 8887;
        sprintf(seedInputBuf, "%u", seedVal);
        applySeed(std::string(seedInputBuf));
    }
    ImGui::SameLine();
    if (ImGui::Button("Случайное количество")) {
        seedVal = (seedVal + 99) * 1313;
        sprintf(seedInputBuf, "%u", seedVal);
        applySeed(std::string(seedInputBuf));
    }

    ImGui::End();

    // 2. Индивидуальные настройки для всех 6 групп (Цвета и 72 слайдера взаимосвязей)
    ImGui::Begin("Настройки групп частиц", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    for (int i = 0; i < 6; i++) {
        if (ImGui::CollapsingHeader((groups[i].name + " частицы").c_str())) {
            ImGui::PushID(i);
            
            if (ImGui::InputInt("Количество", &groups[i].count)) {
                if (groups[i].count < 0) groups[i].count = 0;
                populateParticles();
            }
            ImGui::ColorEdit3("Цвет", groups[i].color);

            ImGui::Separator();
            ImGui::Text("Силы притяжения (rules):");
            for (int j = 0; j < 6; j++) {
                ImGui::SliderFloat(("К " + groups[j].name + "##f").c_str(), &groups[i].rules[j], -1.0f, 1.0f, "%.2f");
            }

            ImGui::Separator();
            ImGui::Text("Радиусы взаимодействия (radii):");
            for (int j = 0; j < 6; j++) {
                ImGui::SliderFloat(("Радиус к " + groups[j].name + "##r").c_str(), &groups[i].radii[j], 10.0f, 150.0f, "%.0f");
            }

            ImGui::PopID();
        }
    }
    ImGui::End();

    // 3. Окно бенчмарка и статистики производительности
    ImGui::Begin("Анализ производительности", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Всего частиц: %d", (int)particles.size());
    ImGui::Separator();

    ImGui::Text("1. Сборка сетки (CPU): %.2f ms", benchGridBuild);
    ImGui::Text("2. Расчет физических сил: %.2f ms", benchPhysicsForces);
    ImGui::Text("3. Физическая интеграция: %.2f ms", benchIntegration);
    ImGui::Text("4. Отрисовка OpenGL: %.2f ms", benchRendering);
    
    ImGui::Separator();
    float totalSum = benchGridBuild + benchPhysicsForces + benchIntegration + benchRendering;
    ImGui::Text("Общее время кадра: %.2f ms", totalSum);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

// --- MAIN FUNCTION ---

int main(int argc, char** argv) {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Particle Life - Desktop GPU/CPU Edition", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Включение V-Sync для плавной анимации без разрывов кадров

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);

    // Инициализация ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    // Запуск первой симуляции
    applySeed(std::string(seedInputBuf));
    centerCamera();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Сглаживание камеры
        camera.zoom = lerp(camera.zoom, camera.targetZoom, 0.15f);
        camera.offsetX = lerp(camera.offsetX, camera.targetOffsetX, 0.15f);
        camera.offsetY = lerp(camera.offsetY, camera.targetOffsetY, 0.15f);

        // Физический тик
        updatePhysics();

        // Отрисовка OpenGL
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        auto tRenderStart = std::chrono::high_resolution_clock::now();
        drawScene();
        auto tRenderEnd = std::chrono::high_resolution_clock::now();

        float renderMs = std::chrono::duration<float, std::milli>(tRenderEnd - tRenderStart).count();
        benchRendering = benchRendering * (1.0f - benchAlpha) + renderMs * benchAlpha;

        // Построение и наложение UI интерфейса поверх OpenGL сцены
        drawImGuiInterface();

        glfwSwapBuffers(window);
    }

    // Очистка ресурсов перед выходом
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
