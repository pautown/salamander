#include "raylib.h"
#include "salamander_theme.h"
#include "ssh_manager.h"
#include "plugin_browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============================================================================
// Salamander - Plugin Manager for CarThing
// A fire-themed desktop application for managing llizardgui plugins
// ============================================================================

// State
static int g_selectedIndex = 0;
static float g_scrollOffset = 0.0f;
static float g_targetScroll = 0.0f;
static float g_animTime = 0.0f;
static float g_connectionCheckTimer = 0.0f;
static bool g_needsRefresh = true;

// Font
static Font g_font;
static bool g_fontLoaded = false;

// View state
typedef enum {
    VIEW_LOCAL,
    VIEW_DEVICE
} ViewMode;
static ViewMode g_viewMode = VIEW_LOCAL;

// Forward declarations
static void LoadAppFont(void);
static void UnloadAppFont(void);
static void DrawBackground(void);
static void DrawEmberGlow(float time);
static void DrawHeader(void);
static void DrawSidebar(const PluginList *plugins, float deltaTime);
static void DrawMainPanel(const PluginInfo *selectedPlugin);
static void DrawFooter(void);
static void DrawProgressBar(Rectangle bounds, float progress, const char *label);
static void UpdateScrolling(int itemCount, float deltaTime);
static void HandleInput(const PluginList *plugins);

// ============================================================================
// Font Loading
// ============================================================================

static int *BuildCodepoints(int *outCount) {
    static const int ranges[][2] = {
        {0x0020, 0x007E},   // ASCII
        {0x00A0, 0x00FF},   // Latin-1 Supplement
    };

    int total = 0;
    for (int i = 0; i < 2; i++) {
        total += (ranges[i][1] - ranges[i][0] + 1);
    }

    int *codepoints = malloc(total * sizeof(int));
    if (!codepoints) {
        *outCount = 0;
        return NULL;
    }

    int idx = 0;
    for (int i = 0; i < 2; i++) {
        for (int cp = ranges[i][0]; cp <= ranges[i][1]; cp++) {
            codepoints[idx++] = cp;
        }
    }

    *outCount = total;
    return codepoints;
}

static void LoadAppFont(void) {
    int count = 0;
    int *codepoints = BuildCodepoints(&count);

    // Paths relative to build directory (supporting_projects/salamander/build/)
    const char *paths[] = {
        "../../../fonts/ZegoeUI-U.ttf",           // Main project fonts
        "../../fonts/ZegoeUI-U.ttf",
        "../fonts/ZegoeUI-U.ttf",
        "fonts/ZegoeUI-U.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",  // System fallback
    };

    g_font = GetFontDefault();
    for (int i = 0; i < 5; i++) {
        Font loaded = LoadFontEx(paths[i], 32, codepoints, count);
        if (loaded.texture.id != 0) {
            g_font = loaded;
            g_fontLoaded = true;
            SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
            printf("Salamander: Loaded font from %s\n", paths[i]);
            break;
        }
    }

    if (codepoints) free(codepoints);
}

static void UnloadAppFont(void) {
    if (g_fontLoaded && g_font.texture.id != GetFontDefault().texture.id) {
        UnloadFont(g_font);
    }
}

// ============================================================================
// Drawing Functions
// ============================================================================

static void DrawBackground(void) {
    // Dark gradient background
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
                           COLOR_CHARCOAL_DARK,
                           (Color){20, 18, 18, 255});
}

static void DrawEmberGlow(float time) {
    // Animated ember glow at screen edges
    float pulse = (sinf(time * GLOW_SPEED) + 1.0f) * 0.5f;
    float glowAlpha = LERP(EMBER_GLOW_MIN, EMBER_GLOW_MAX, pulse);

    // Top glow
    for (int i = 0; i < 40; i++) {
        float alpha = glowAlpha * (1.0f - i / 40.0f) * 0.15f;
        Color glow = ColorWithAlpha(COLOR_FIRE_DEEP, alpha);
        DrawRectangle(0, i, WINDOW_WIDTH, 1, glow);
    }

    // Bottom glow (stronger)
    for (int i = 0; i < 60; i++) {
        float alpha = glowAlpha * (1.0f - i / 60.0f) * 0.2f;
        Color glow = ColorWithAlpha(COLOR_EMBER, alpha);
        DrawRectangle(0, WINDOW_HEIGHT - 60 + i, WINDOW_WIDTH, 1, glow);
    }

    // Corner accents
    float cornerPulse = (sinf(time * PULSE_SPEED + 1.5f) + 1.0f) * 0.5f;
    Color cornerColor = ColorWithAlpha(COLOR_FIRE_DEEP, 0.1f + cornerPulse * 0.1f);
    DrawCircleGradient(0, 0, 150, cornerColor, BLANK);
    DrawCircleGradient(WINDOW_WIDTH, WINDOW_HEIGHT, 180, cornerColor, BLANK);
}

static void DrawHeader(void) {
    // Header background
    DrawRectangle(0, 0, WINDOW_WIDTH, HEADER_HEIGHT, COLOR_WARM_GRAY);

    // Bottom border with fire accent
    float pulse = (sinf(g_animTime * 2.0f) + 1.0f) * 0.5f;
    Color borderColor = ColorWithAlpha(COLOR_FIRE_DEEP, 0.6f + pulse * 0.4f);
    DrawRectangle(0, HEADER_HEIGHT - 2, WINDOW_WIDTH, 2, borderColor);

    // Title
    DrawTextEx(g_font, "SALAMANDER",
               (Vector2){HEADER_PADDING, (HEADER_HEIGHT - 24) / 2},
               24, 1, COLOR_FIRE_DEEP);

    // Connection status
    SshConnectionStatus status = SshGetStatus();
    const char *statusText;
    Color statusColor;
    const char *statusIcon;

    switch (status) {
        case SSH_STATUS_CONNECTED:
            statusText = SshGetHost();
            statusColor = COLOR_CONNECTED;
            statusIcon = "[*]";
            break;
        case SSH_STATUS_CHECKING:
            statusText = "Checking...";
            statusColor = COLOR_EMBER;
            statusIcon = "[~]";
            break;
        default:
            statusText = "Disconnected";
            statusColor = COLOR_DISCONNECTED;
            statusIcon = "[ ]";
            break;
    }

    char statusStr[128];
    snprintf(statusStr, sizeof(statusStr), "%s %s", statusIcon, statusText);
    Vector2 textSize = MeasureTextEx(g_font, statusStr, 18, 1);
    DrawTextEx(g_font, statusStr,
               (Vector2){WINDOW_WIDTH - textSize.x - HEADER_PADDING, (HEADER_HEIGHT - 18) / 2},
               18, 1, statusColor);
}

static void DrawSidebar(const PluginList *plugins, float deltaTime) {
    (void)deltaTime;

    // Sidebar background
    DrawRectangle(0, HEADER_HEIGHT, SIDEBAR_WIDTH, WINDOW_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT,
                  COLOR_WARM_GRAY);

    // Right border
    DrawRectangle(SIDEBAR_WIDTH - 1, HEADER_HEIGHT, 1,
                  WINDOW_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT,
                  ColorWithAlpha(COLOR_FIRE_DEEP, 0.3f));

    int y = HEADER_HEIGHT + SIDEBAR_PADDING;

    // Section: Local Plugins
    DrawTextEx(g_font, "LOCAL PLUGINS", (Vector2){SIDEBAR_PADDING, (float)y}, 14, 1, COLOR_TEXT_DIM);
    y += 24;

    // Count local plugins
    int localCount = 0;
    for (int i = 0; i < plugins->count; i++) {
        if (plugins->plugins[i].localPath[0] != '\0') localCount++;
    }

    if (localCount == 0) {
        DrawTextEx(g_font, "No local plugins", (Vector2){SIDEBAR_PADDING, (float)y}, 14, 1, COLOR_TEXT_DIM);
        y += SIDEBAR_ITEM_HEIGHT;
    }

    // Draw local plugins
    int listIndex = 0;
    for (int i = 0; i < plugins->count; i++) {
        const PluginInfo *p = &plugins->plugins[i];
        if (p->localPath[0] == '\0') continue;

        bool selected = (listIndex == g_selectedIndex && g_viewMode == VIEW_LOCAL);
        Rectangle itemRect = {SIDEBAR_PADDING - 4, (float)y, SIDEBAR_WIDTH - SIDEBAR_PADDING * 2 + 8, SIDEBAR_ITEM_HEIGHT};

        if (selected) {
            // Selected background
            DrawRectangleRounded(itemRect, 0.2f, 4, COLOR_CARD_SELECTED);
            // Fire accent bar
            DrawRectangle((int)itemRect.x, (int)itemRect.y + 6, 3, (int)itemRect.height - 12, COLOR_FIRE_DEEP);
        }

        // Status indicator
        const char *indicator = (p->status == PLUGIN_INSTALLED) ? "*" : "o";
        Color indicatorColor = (p->status == PLUGIN_INSTALLED) ? COLOR_CONNECTED : COLOR_TEXT_DIM;
        DrawTextEx(g_font, indicator, (Vector2){SIDEBAR_PADDING + 4, y + 10.0f}, 16, 1, indicatorColor);

        // Plugin name
        Color textColor = selected ? COLOR_TEXT_BRIGHT : COLOR_TEXT_WARM;
        DrawTextEx(g_font, p->displayName, (Vector2){SIDEBAR_PADDING + 20, y + 10.0f}, 16, 1, textColor);

        y += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
        listIndex++;
    }

    y += SIDEBAR_SECTION_SPACING;

    // Section: Device Plugins
    DrawTextEx(g_font, "DEVICE PLUGINS", (Vector2){SIDEBAR_PADDING, (float)y}, 14, 1, COLOR_TEXT_DIM);
    y += 24;

    // Count device plugins
    int deviceCount = 0;
    for (int i = 0; i < plugins->count; i++) {
        if (plugins->plugins[i].remotePath[0] != '\0') deviceCount++;
    }

    if (deviceCount == 0) {
        const char *msg = (SshGetStatus() == SSH_STATUS_CONNECTED) ? "No plugins installed" : "Connect to view";
        DrawTextEx(g_font, msg, (Vector2){SIDEBAR_PADDING, (float)y}, 14, 1, COLOR_TEXT_DIM);
    }

    // Draw device plugins
    listIndex = 0;
    for (int i = 0; i < plugins->count; i++) {
        const PluginInfo *p = &plugins->plugins[i];
        if (p->remotePath[0] == '\0') continue;

        bool selected = (listIndex == g_selectedIndex && g_viewMode == VIEW_DEVICE);
        Rectangle itemRect = {SIDEBAR_PADDING - 4, (float)y, SIDEBAR_WIDTH - SIDEBAR_PADDING * 2 + 8, SIDEBAR_ITEM_HEIGHT};

        if (selected) {
            DrawRectangleRounded(itemRect, 0.2f, 4, COLOR_CARD_SELECTED);
            DrawRectangle((int)itemRect.x, (int)itemRect.y + 6, 3, (int)itemRect.height - 12, COLOR_EMBER);
        }

        // Status indicator (all device plugins are installed by definition)
        DrawTextEx(g_font, "*", (Vector2){SIDEBAR_PADDING + 4, y + 10.0f}, 16, 1, COLOR_CONNECTED);

        Color textColor = selected ? COLOR_TEXT_BRIGHT : COLOR_TEXT_WARM;
        DrawTextEx(g_font, p->displayName, (Vector2){SIDEBAR_PADDING + 20, y + 10.0f}, 16, 1, textColor);

        y += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
        listIndex++;
    }
}

static void DrawMainPanel(const PluginInfo *plugin) {
    int panelX = PANEL_START_X;
    int panelY = HEADER_HEIGHT;
    int panelW = WINDOW_WIDTH - PANEL_START_X;
    int panelH = WINDOW_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;

    // Panel background
    DrawRectangle(panelX, panelY, panelW, panelH, COLOR_CHARCOAL_DARK);

    if (!plugin) {
        DrawTextEx(g_font, "Select a plugin",
                   (Vector2){panelX + PANEL_PADDING, panelY + PANEL_PADDING + 50},
                   20, 1, COLOR_TEXT_DIM);
        return;
    }

    int y = panelY + PANEL_PADDING;

    // Plugin name (large)
    DrawTextEx(g_font, plugin->displayName, (Vector2){panelX + PANEL_PADDING, (float)y}, 32, 2, COLOR_TEXT_BRIGHT);
    y += 48;

    // Underline
    float pulse = (sinf(g_animTime * 2.0f) + 1.0f) * 0.5f;
    Color lineColor = ColorWithAlpha(COLOR_FIRE_DEEP, 0.4f + pulse * 0.3f);
    DrawRectangle(panelX + PANEL_PADDING, y, 200, 2, lineColor);
    y += 20;

    // Description placeholder
    DrawTextEx(g_font, "llizardgui plugin", (Vector2){panelX + PANEL_PADDING, (float)y}, 16, 1, COLOR_TEXT_DIM);
    y += 30;

    // File size info
    char sizeStr[64];
    if (plugin->localSize > 0) {
        FormatFileSize(plugin->localSize, sizeStr, sizeof(sizeStr));
        char info[128];
        snprintf(info, sizeof(info), "Local size: %s", sizeStr);
        DrawTextEx(g_font, info, (Vector2){panelX + PANEL_PADDING, (float)y}, 16, 1, COLOR_TEXT_WARM);
        y += 24;
    }

    if (plugin->remoteSize > 0) {
        FormatFileSize(plugin->remoteSize, sizeStr, sizeof(sizeStr));
        char info[128];
        snprintf(info, sizeof(info), "Device size: %s", sizeStr);
        DrawTextEx(g_font, info, (Vector2){panelX + PANEL_PADDING, (float)y}, 16, 1, COLOR_TEXT_WARM);
        y += 24;
    }

    y += 10;

    // Status
    const char *statusText;
    Color statusColor;
    switch (plugin->status) {
        case PLUGIN_INSTALLED:
            statusText = "Installed";
            statusColor = COLOR_CONNECTED;
            break;
        case PLUGIN_LOCAL_ONLY:
            statusText = "Local Only";
            statusColor = COLOR_EMBER;
            break;
        case PLUGIN_DEVICE_ONLY:
            statusText = "Device Only";
            statusColor = COLOR_INSTALLING;
            break;
    }

    DrawTextEx(g_font, "Status:", (Vector2){panelX + PANEL_PADDING, (float)y}, 16, 1, COLOR_TEXT_DIM);
    DrawTextEx(g_font, statusText, (Vector2){panelX + PANEL_PADDING + 70, (float)y}, 16, 1, statusColor);
    y += 50;

    // Action buttons
    bool canInstall = (plugin->localPath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED);
    bool canUninstall = (plugin->remotePath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED);
    bool isBusy = PluginBrowserIsBusy();

    Rectangle installBtn = {panelX + PANEL_PADDING, (float)y, BUTTON_WIDTH, BUTTON_HEIGHT};
    Rectangle uninstallBtn = {panelX + PANEL_PADDING + BUTTON_WIDTH + BUTTON_SPACING, (float)y, BUTTON_WIDTH, BUTTON_HEIGHT};

    // Install button
    Color installBg = (canInstall && !isBusy) ? COLOR_FIRE_DEEP : ColorWithAlpha(COLOR_FIRE_DEEP, 0.3f);
    Color installText = (canInstall && !isBusy) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_DIM;
    DrawRectangleRounded(installBtn, BUTTON_RADIUS, 4, installBg);
    Vector2 installSize = MeasureTextEx(g_font, "INSTALL", 16, 1);
    DrawTextEx(g_font, "INSTALL",
               (Vector2){installBtn.x + (installBtn.width - installSize.x) / 2,
                         installBtn.y + (installBtn.height - 16) / 2},
               16, 1, installText);

    // Uninstall button
    Color uninstallBg = (canUninstall && !isBusy) ? COLOR_ASH : ColorWithAlpha(COLOR_ASH, 0.3f);
    Color uninstallText = (canUninstall && !isBusy) ? COLOR_TEXT_WARM : COLOR_TEXT_DIM;
    DrawRectangleRounded(uninstallBtn, BUTTON_RADIUS, 4, uninstallBg);
    Vector2 uninstallSize = MeasureTextEx(g_font, "UNINSTALL", 16, 1);
    DrawTextEx(g_font, "UNINSTALL",
               (Vector2){uninstallBtn.x + (uninstallBtn.width - uninstallSize.x) / 2,
                         uninstallBtn.y + (uninstallBtn.height - 16) / 2},
               16, 1, uninstallText);

    y += BUTTON_HEIGHT + 30;

    // Progress bar (if operation in progress)
    const PluginOpState *opState = PluginBrowserGetOpState();
    if (opState->operation != OP_NONE || (opState->complete && g_animTime < 3.0f)) {
        Rectangle progressBounds = {panelX + PANEL_PADDING, (float)y, panelW - PANEL_PADDING * 2, PROGRESS_HEIGHT};
        DrawProgressBar(progressBounds, opState->progress, opState->message);
    }
}

static void DrawProgressBar(Rectangle bounds, float progress, const char *label) {
    // Background
    DrawRectangleRounded(bounds, PROGRESS_RADIUS, 4, COLOR_ASH);

    // Fill
    Rectangle fill = {bounds.x + 2, bounds.y + 2, (bounds.width - 4) * progress, bounds.height - 4};
    if (fill.width > 0) {
        // Gradient fill from fire deep to ember
        Color fillColor = (progress >= 1.0f) ? COLOR_CONNECTED : COLOR_FIRE_DEEP;
        DrawRectangleRounded(fill, PROGRESS_RADIUS, 4, fillColor);
    }

    // Label
    if (label && label[0]) {
        float labelY = bounds.y + bounds.height + 8;
        DrawTextEx(g_font, label, (Vector2){bounds.x, labelY}, 14, 1, COLOR_TEXT_DIM);
    }

    // Percentage
    char pctStr[16];
    snprintf(pctStr, sizeof(pctStr), "%.0f%%", progress * 100);
    Vector2 pctSize = MeasureTextEx(g_font, pctStr, 12, 1);
    DrawTextEx(g_font, pctStr,
               (Vector2){bounds.x + bounds.width - pctSize.x, bounds.y + (bounds.height - 12) / 2},
               12, 1, COLOR_TEXT_BRIGHT);
}

static void DrawFooter(void) {
    int footerY = WINDOW_HEIGHT - FOOTER_HEIGHT;

    // Footer background
    DrawRectangle(0, footerY, WINDOW_WIDTH, FOOTER_HEIGHT, COLOR_WARM_GRAY);

    // Top border
    DrawRectangle(0, footerY, WINDOW_WIDTH, 1, ColorWithAlpha(COLOR_FIRE_DEEP, 0.3f));

    // Instructions
    const char *instructions = "Up/Down: Navigate  |  Tab: Switch view  |  Enter: Install  |  Delete: Uninstall  |  R: Refresh";
    DrawTextEx(g_font, instructions,
               (Vector2){HEADER_PADDING, footerY + (FOOTER_HEIGHT - 14) / 2.0f},
               14, 1, COLOR_TEXT_DIM);
}

// ============================================================================
// Input and Logic
// ============================================================================

static void UpdateScrolling(int itemCount, float deltaTime) {
    if (itemCount == 0) return;

    // Calculate target scroll to keep selection visible
    float itemHeight = SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
    float selectedY = g_selectedIndex * itemHeight;
    float visibleHeight = WINDOW_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 100;

    float targetTop = g_targetScroll;
    float targetBottom = g_targetScroll + visibleHeight;

    if (selectedY < targetTop + 20) {
        g_targetScroll = selectedY - 20;
    } else if (selectedY + itemHeight > targetBottom - 20) {
        g_targetScroll = selectedY + itemHeight - visibleHeight + 20;
    }

    // Clamp
    float maxScroll = itemCount * itemHeight - visibleHeight;
    if (maxScroll < 0) maxScroll = 0;
    if (g_targetScroll < 0) g_targetScroll = 0;
    if (g_targetScroll > maxScroll) g_targetScroll = maxScroll;

    // Smooth interpolation
    float diff = g_targetScroll - g_scrollOffset;
    g_scrollOffset += diff * SCROLL_SPEED * deltaTime;
    if (fabsf(diff) < 0.5f) g_scrollOffset = g_targetScroll;
}

static void HandleInput(const PluginList *plugins) {
    // Get counts for current view
    int localCount = 0, deviceCount = 0;
    for (int i = 0; i < plugins->count; i++) {
        if (plugins->plugins[i].localPath[0] != '\0') localCount++;
        if (plugins->plugins[i].remotePath[0] != '\0') deviceCount++;
    }

    int currentCount = (g_viewMode == VIEW_LOCAL) ? localCount : deviceCount;

    // Navigation
    if (IsKeyPressed(KEY_DOWN) && currentCount > 0) {
        g_selectedIndex = (g_selectedIndex + 1) % currentCount;
    }
    if (IsKeyPressed(KEY_UP) && currentCount > 0) {
        g_selectedIndex = (g_selectedIndex - 1 + currentCount) % currentCount;
    }

    // Switch view with Tab
    if (IsKeyPressed(KEY_TAB)) {
        g_viewMode = (g_viewMode == VIEW_LOCAL) ? VIEW_DEVICE : VIEW_LOCAL;
        g_selectedIndex = 0;
    }

    // Refresh with R
    if (IsKeyPressed(KEY_R)) {
        g_needsRefresh = true;
    }

    // Get selected plugin
    const PluginInfo *selectedPlugin = NULL;
    int idx = 0;
    if (g_viewMode == VIEW_LOCAL) {
        for (int i = 0; i < plugins->count && !selectedPlugin; i++) {
            if (plugins->plugins[i].localPath[0] != '\0') {
                if (idx == g_selectedIndex) selectedPlugin = &plugins->plugins[i];
                idx++;
            }
        }
    } else {
        for (int i = 0; i < plugins->count && !selectedPlugin; i++) {
            if (plugins->plugins[i].remotePath[0] != '\0') {
                if (idx == g_selectedIndex) selectedPlugin = &plugins->plugins[i];
                idx++;
            }
        }
    }

    // Install with Enter
    if (IsKeyPressed(KEY_ENTER) && selectedPlugin && !PluginBrowserIsBusy()) {
        if (selectedPlugin->localPath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED) {
            PluginBrowserInstall(selectedPlugin->name);
            g_needsRefresh = true;
        }
    }

    // Uninstall with Delete or Backspace
    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && selectedPlugin && !PluginBrowserIsBusy()) {
        if (selectedPlugin->remotePath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED) {
            PluginBrowserUninstall(selectedPlugin->name);
            g_needsRefresh = true;
        }
    }

    // Mouse click on buttons
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && selectedPlugin && !PluginBrowserIsBusy()) {
        Vector2 mouse = GetMousePosition();
        int panelX = PANEL_START_X;
        int buttonY = HEADER_HEIGHT + PANEL_PADDING + 48 + 20 + 30 + 24 + 24 + 10 + 50;

        Rectangle installBtn = {panelX + PANEL_PADDING, (float)buttonY, BUTTON_WIDTH, BUTTON_HEIGHT};
        Rectangle uninstallBtn = {panelX + PANEL_PADDING + BUTTON_WIDTH + BUTTON_SPACING, (float)buttonY, BUTTON_WIDTH, BUTTON_HEIGHT};

        if (CheckCollisionPointRec(mouse, installBtn)) {
            if (selectedPlugin->localPath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED) {
                PluginBrowserInstall(selectedPlugin->name);
                g_needsRefresh = true;
            }
        } else if (CheckCollisionPointRec(mouse, uninstallBtn)) {
            if (selectedPlugin->remotePath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED) {
                PluginBrowserUninstall(selectedPlugin->name);
                g_needsRefresh = true;
            }
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
    // Parse command line for local plugin path
    // Default path relative to build directory (supporting_projects/salamander/build/)
    const char *localPath = "../../../build-armv7-drm";
    if (argc > 1) {
        localPath = argv[1];
    }

    // Initialize
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Salamander - Plugin Manager");
    SetTargetFPS(TARGET_FPS);
    LoadAppFont();

    // Initialize managers
    SshInit(NULL, NULL, NULL);  // Use defaults
    PluginBrowserInit(localPath);

    // Resolve and print absolute path for clarity
    char absPath[512] = {0};
    if (realpath(localPath, absPath)) {
        printf("Salamander: Looking for plugins in %s\n", absPath);
    } else {
        printf("Salamander: Looking for plugins in %s (path may not exist)\n", localPath);
    }
    printf("Salamander: Will connect to CarThing at %s\n", SshGetHost());

    // Initial connection check
    SshCheckConnection();

    // Main loop
    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        g_animTime += deltaTime;

        // Periodic connection check
        g_connectionCheckTimer += deltaTime;
        if (g_connectionCheckTimer >= 5.0f) {
            SshCheckConnection();
            g_connectionCheckTimer = 0.0f;
        }

        // Refresh plugins if needed
        if (g_needsRefresh && !PluginBrowserIsBusy()) {
            PluginBrowserRefresh();
            g_needsRefresh = false;
        }

        // Get plugin list
        const PluginList *plugins = PluginBrowserGetList();

        // Handle input
        HandleInput(plugins);

        // Update scrolling
        int localCount = 0;
        for (int i = 0; i < plugins->count; i++) {
            if (plugins->plugins[i].localPath[0] != '\0') localCount++;
        }
        UpdateScrolling(localCount, deltaTime);

        // Get selected plugin for main panel
        const PluginInfo *selectedPlugin = NULL;
        int idx = 0;
        if (g_viewMode == VIEW_LOCAL) {
            for (int i = 0; i < plugins->count && !selectedPlugin; i++) {
                if (plugins->plugins[i].localPath[0] != '\0') {
                    if (idx == g_selectedIndex) selectedPlugin = &plugins->plugins[i];
                    idx++;
                }
            }
        } else {
            for (int i = 0; i < plugins->count && !selectedPlugin; i++) {
                if (plugins->plugins[i].remotePath[0] != '\0') {
                    if (idx == g_selectedIndex) selectedPlugin = &plugins->plugins[i];
                    idx++;
                }
            }
        }

        // Draw
        BeginDrawing();
        ClearBackground(COLOR_CHARCOAL_DARK);

        DrawBackground();
        DrawEmberGlow(g_animTime);
        DrawHeader();
        DrawSidebar(plugins, deltaTime);
        DrawMainPanel(selectedPlugin);
        DrawFooter();

        EndDrawing();
    }

    // Cleanup
    PluginBrowserShutdown();
    SshShutdown();
    UnloadAppFont();
    CloseWindow();

    return 0;
}
