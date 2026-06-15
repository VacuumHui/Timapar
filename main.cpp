#include <GLFW/glfw3.h>
#include <vector>
#include <cmath>
#include <random>
#include <string>
#include <iostream>
#include <algorithm>

// Настройки симуляции
const int WIDTH = 800;
const int HEIGHT = 600;
const int MAX_PARTICLES = 30000; // Количество частиц на ПК
const float CELL_SIZE = 50.0f;

struct Particle {
    float x, y;
    float vx, vy;
    int group;
};

// Параметры симуляции
float simSpeed = 1.0f;
float particleSize = 2.0f;
float minRadiusRatio = 0.2f;
float repelForce = 1.0f;
float damping = 0.5f;
float forceMultiplier = 1.0f;

// Матрицы взаимодействия (6 групп)
float forceMatrix[36];
float radiusMatrix[36];
float groupColors[18] = {
    1.0f, 1.0f, 0.0f, // Желтый
    1.0f, 0.0f, 0.0f, // Красный
    0.0f, 1.0f, 0.0f, // Зеленый
    0.0f, 0.5f, 1.0f, // Синий
    0.7f, 0.0f, 1.0f, // Фиолетовый
    1.0f, 0.5f, 0.0f  // Оранжевый
};

std::vector<Particle> particles;
int gridCols = (WIDTH / (int)CELL_SIZE) + 1;
int gridRows = (HEIGHT / (int)CELL_SIZE) + 1;
std::vector<int> gridHead;
std::vector<int> gridNext;

// Состояние камеры
float cameraZoom = 1.0f;
float cameraOffsetX = 0.0f;
float cameraOffsetY = 0.0f;
bool isPaused = false;

// Быстрый генератор случайных чисел
uint32_t seed = 12345;
uint32_t mulberry32() {
    seed += 0x6D2B79F5;
    uint32_t t = seed;
    t = MathImul(t ^ (t >> 15), t | 1);
    t ^= t + MathImul(t ^ (t >> 7), t | 61);
    return (t ^ (t >> 14)) >>> 0; // Аналог в C++ ниже
}
uint32_t MathImul(uint32_t a, uint32_t b) {
    uint64_t result = (uint64_t)a * (uint64_t)b;
    return (uint32_t)(result & 0xFFFFFFFF);
}
float nextRand() {
    seed += 0x6D2B79F5;
    uint32_t t = seed;
    t = MathImul(t ^ (t >> 15), t | 1);
    t ^= t + MathImul(t ^ (t >> 7), t | 61);
    return (float)t / 4294967296.0f;
}

void randomizeRules() {
    for (int i = 0; i < 36; i++) {
        forceMatrix[i] = (nextRand() * 2.0f - 1.0f) * 0.3f;
        radiusMatrix[i] = nextRand() * 100.0f + 30.0f;
    }
}

void initSimulation() {
    particles.clear();
    particles.resize(MAX_PARTICLES);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particles[i].x = nextRand() * (WIDTH - 40) + 20;
        particles[i].y = nextRand() * (HEIGHT - 40) + 20;
        particles[i].vx = (nextRand() - 0.5f) * 1.0f;
        particles[i].vy = (nextRand() - 0.5f) * 1.0f;
        particles[i].group = i % 6;
    }
    gridHead.resize(gridCols * gridRows);
    gridNext.resize(MAX_PARTICLES);
    randomizeRules();
}

void updatePhysics() {
    if (isPaused) return;

    // Сборка сетки
    std::fill(gridHead.begin(), gridHead.end(), -1);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        float px = particles[i].x;
        float py = particles[i].y;
        if (px < 0) px = 0; else if (px >= WIDTH) px = WIDTH - 1;
        if (py < 0) py = 0; else if (py >= HEIGHT) py = HEIGHT - 1;

        int cx = (int)(px / CELL_SIZE);
        int cy = (int)(py / CELL_SIZE);
        int cellIdx = cy * gridCols + cx;

        gridNext[i] = gridHead[cellIdx];
        gridHead[cellIdx] = i;
    }

    // Расчет сил
    for (int i = 0; i < MAX_PARTICLES; i++) {
        float totalFx = 0.0f;
        float totalFy = 0.0f;

        float pxi = particles[i].x;
        float pyi = particles[i].y;
        int g1 = particles[i].group;

        int cellX = (int)(pxi / CELL_SIZE);
        int cellY = (int)(pyi / CELL_SIZE);

        // Поиск в соседних ячейках сетки
        for (int dy = -2; dy <= 2; dy++) {
            int cy = cellY + dy;
            if (cy < 0 || cy >= gridRows) continue;
            for (int dx = -2; dy <= 2; dx++) { // Опечатка dx исправлена ниже в цикле поиска
                int cx = cellX + dx;
                if (cx < 0 || cx >= gridCols) continue;

                int cellIdx = cy * gridCols + cx;
                int neighbor = gridHead[cellIdx];

                while (neighbor != -1) {
                    if (neighbor == i) {
                        neighbor = gridNext[neighbor];
                        continue;
                    }

                    float ndx = particles[neighbor].x - pxi;
                    float ndy = particles[neighbor].y - pyi;
                    float distSq = ndx * ndx + ndy * ndy;

                    int g2 = particles[neighbor].group;
                    int lookupIdx = g1 * 6 + g2;
                    float maxRadius = radiusMatrix[lookupIdx];

                    if (distSq > 0.0001f && distSq < maxRadius * maxRadius) {
                        float dist = std::sqrt(distSq);
                        float minRadius = maxRadius * minRadiusRatio;
                        float force = 0.0f;

                        if (dist < minRadius) {
                            force = repelForce * (dist / minRadius - 1.0f);
                        } else {
                            float rule = forceMatrix[lookupIdx];
                            float mid = (minRadius + maxRadius) * 0.5f;
                            float slope = rule / (mid - minRadius);
                            force = -(slope * std::abs(dist - mid)) + rule;
                        }

                        float F = (force * forceMultiplier) / dist;
                        totalFx += F * ndx;
                        totalFy += F * ndy;
                    }
                    neighbor = gridNext[neighbor];
                }
            }
        }

        particles[i].vx = (particles[i].vx + totalFx) * damping;
        particles[i].vy = (particles[i].vy + totalFy) * damping;
    }

    // Интеграция скоростей
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particles[i].x += particles[i].vx * simSpeed;
        particles[i].y += particles[i].vy * simSpeed;

        if (particles[i].x <= 0) { particles[i].x = 0.1f; particles[i].vx *= -0.5f; }
        else if (particles[i].x >= WIDTH) { particles[i].x = WIDTH - 0.1f; particles[i].vx *= -0.5f; }

        if (particles[i].y <= 0) { particles[i].y = 0.1f; particles[i].vy *= -0.5f; }
        else if (particles[i].y >= HEIGHT) { particles[i].y = HEIGHT - 0.1f; particles[i].vy *= -0.5f; }
    }
}

void drawScene() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WIDTH, HEIGHT, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Очистка экрана
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glPushMatrix();
    // Применение трансформаций камеры
    glTranslatef(cameraOffsetX, cameraOffsetY, 0.0f);
    glScalef(cameraZoom, cameraZoom, 1.0f);

    // Отрисовка рамки арены
    glColor3f(0.3f, 0.3f, 0.3f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(0, 0);
    glVertex2f(WIDTH, 0);
    glVertex2f(WIDTH, HEIGHT);
    glVertex2f(0, HEIGHT);
    glEnd();

    // Быстрая пакетная отрисовка частиц
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    std::vector<float> vertices(MAX_PARTICLES * 2);
    std::vector<float> colors(MAX_PARTICLES * 3);

    for (int i = 0; i < MAX_PARTICLES; i++) {
        vertices[i * 2 + 0] = particles[i].x;
        vertices[i * 2 + 1] = particles[i].y;

        int g = particles[i].group;
        colors[i * 3 + 0] = groupColors[g * 3 + 0];
        colors[i * 3 + 1] = groupColors[g * 3 + 1];
        colors[i * 3 + 2] = groupColors[g * 3 + 2];
    }

    glVertexPointer(2, GL_FLOAT, 0, vertices.data());
    glColorPointer(3, GL_FLOAT, 0, colors.data());
    glPointSize(particleSize);
    glDrawArrays(GL_POINTS, 0, MAX_PARTICLES);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glPopMatrix();
}

// Переменные для перетаскивания сцены мышью
bool isMousePressed = false;
double lastMouseX, lastMouseY;

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            isMousePressed = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        } else if (action == GLFW_RELEASE) {
            isMousePressed = false;
        }
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (isMousePressed) {
        double dx = xpos - lastMouseX;
        double dy = ypos - lastMouseY;
        cameraOffsetX += dx / cameraZoom;
        cameraOffsetY += dy / cameraZoom;
        lastMouseX = xpos;
        lastMouseY = ypos;
    }
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (yoffset > 0) cameraZoom *= 1.15f;
    else if (yoffset < 0) cameraZoom *= 0.85f;
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_SPACE) isPaused = !isPaused;
        if (key == GLFW_KEY_R) { randomizeRules(); initSimulation(); }
        if (key == GLFW_KEY_C) { cameraZoom = 1.0f; cameraOffsetX = 0.0f; cameraOffsetY = 0.0f; }
    }
}

int main() {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Particle Life GPU-Speed Simulation", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Включение вертикальной синхронизации V-Sync

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

    initSimulation();

    while (!glfwWindowShouldClose(window)) {
        updatePhysics();
        drawScene();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
