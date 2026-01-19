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

// Section types for the three-column layout
typedef enum {
    SECTION_DEVICE_ONLY,    // Plugins only on device
    SECTION_SYNCED,         // Plugins on both local and device
    SECTION_LOCAL_ONLY      // Plugins only on local machine
} SectionType;

// Selection state
typedef struct {
    SectionType section;
    int index;              // Index within section
} Selection;

// Drag state
typedef struct {
    bool isDragging;
    char pluginName[64];
    SectionType sourceSection;
    Vector2 startPos;
    Vector2 currentPos;
    float dragTime;
} DragState;

// Global state
static Selection g_selection = {SECTION_LOCAL_ONLY, 0};
static DragState g_drag = {0};
static float g_animTime = 0.0f;
static float g_connectionCheckTimer = 0.0f;
static bool g_needsRefresh = true;

// Section rectangles for drop detection
static Rectangle g_sectionRects[3] = {0};

// Font
static Font g_font;
static bool g_fontLoaded = false;

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
static void DrawDragGhost(void);
static void HandleInput(const PluginList *plugins);
static const PluginInfo *GetSelectedPlugin(const PluginList *plugins);
static void GetSectionCounts(const PluginList *plugins, int *deviceOnly, int *synced, int *localOnly);
static const PluginInfo *GetPluginInSection(const PluginList *plugins, SectionType section, int index);

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

    const char *paths[] = {
        "../../../fonts/ZegoeUI-U.ttf",
        "../../fonts/ZegoeUI-U.ttf",
        "../fonts/ZegoeUI-U.ttf",
        "fonts/ZegoeUI-U.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
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
// Helper Functions
// ============================================================================

static void GetSectionCounts(const PluginList *plugins, int *deviceOnly, int *synced, int *localOnly) {
    *deviceOnly = 0;
    *synced = 0;
    *localOnly = 0;

    for (int i = 0; i < plugins->count; i++) {
        const PluginInfo *p = &plugins->plugins[i];
        bool hasLocal = (p->localPath[0] != '\0');
        bool hasRemote = (p->remotePath[0] != '\0');

        if (hasLocal && hasRemote) {
            (*synced)++;
        } else if (hasRemote) {
            (*deviceOnly)++;
        } else if (hasLocal) {
            (*localOnly)++;
        }
    }
}

static const PluginInfo *GetPluginInSection(const PluginList *plugins, SectionType section, int index) {
    int count = 0;
    for (int i = 0; i < plugins->count; i++) {
        const PluginInfo *p = &plugins->plugins[i];
        bool hasLocal = (p->localPath[0] != '\0');
        bool hasRemote = (p->remotePath[0] != '\0');

        bool inSection = false;
        if (section == SECTION_SYNCED && hasLocal && hasRemote) inSection = true;
        else if (section == SECTION_DEVICE_ONLY && hasRemote && !hasLocal) inSection = true;
        else if (section == SECTION_LOCAL_ONLY && hasLocal && !hasRemote) inSection = true;

        if (inSection) {
            if (count == index) return p;
            count++;
        }
    }
    return NULL;
}

static const PluginInfo *GetSelectedPlugin(const PluginList *plugins) {
    return GetPluginInSection(plugins, g_selection.section, g_selection.index);
}

static int GetSectionCount(const PluginList *plugins, SectionType section) {
    int deviceOnly, synced, localOnly;
    GetSectionCounts(plugins, &deviceOnly, &synced, &localOnly);
    switch (section) {
        case SECTION_DEVICE_ONLY: return deviceOnly;
        case SECTION_SYNCED: return synced;
        case SECTION_LOCAL_ONLY: return localOnly;
    }
    return 0;
}

// ============================================================================
// Drawing Functions
// ============================================================================

static void DrawBackground(void) {
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
                           COLOR_CHARCOAL_DARK,
                           (Color){20, 18, 18, 255});
}

static void DrawEmberGlow(float time) {
    float pulse = (sinf(time * GLOW_SPEED) + 1.0f) * 0.5f;
    float glowAlpha = LERP(EMBER_GLOW_MIN, EMBER_GLOW_MAX, pulse);

    // Top glow
    for (int i = 0; i < 40; i++) {
        float alpha = glowAlpha * (1.0f - i / 40.0f) * 0.15f;
        Color glow = ColorWithAlpha(COLOR_FIRE_DEEP, alpha);
        DrawRectangle(0, i, WINDOW_WIDTH, 1, glow);
    }

    // Bottom glow
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
    DrawRectangle(0, 0, WINDOW_WIDTH, HEADER_HEIGHT, COLOR_WARM_GRAY);

    float pulse = (sinf(g_animTime * 2.0f) + 1.0f) * 0.5f;
    Color borderColor = ColorWithAlpha(COLOR_FIRE_DEEP, 0.6f + pulse * 0.4f);
    DrawRectangle(0, HEADER_HEIGHT - 2, WINDOW_WIDTH, 2, borderColor);

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

static void DrawSectionHeader(const char *title, int y, Color accentColor, bool isDropTarget) {
    // Highlight if drop target
    if (isDropTarget && g_drag.isDragging) {
        DrawRectangle(SIDEBAR_PADDING - 8, y - 4, SIDEBAR_WIDTH - SIDEBAR_PADDING * 2 + 16, 22,
                      ColorWithAlpha(accentColor, 0.3f));
    }
    DrawTextEx(g_font, title, (Vector2){SIDEBAR_PADDING, (float)y}, 14, 1, accentColor);
}

static void DrawPluginItem(const PluginInfo *p, int y, bool selected, bool isDragSource, Color accentColor) {
    Rectangle itemRect = {SIDEBAR_PADDING - 4, (float)y, SIDEBAR_WIDTH - SIDEBAR_PADDING * 2 + 8, SIDEBAR_ITEM_HEIGHT};

    // Dim if being dragged
    float alpha = isDragSource ? 0.3f : 1.0f;

    if (selected && !isDragSource) {
        DrawRectangleRounded(itemRect, 0.2f, 4, COLOR_CARD_SELECTED);
        DrawRectangle((int)itemRect.x, (int)itemRect.y + 6, 3, (int)itemRect.height - 12, accentColor);
    }

    Color textColor = selected ? COLOR_TEXT_BRIGHT : COLOR_TEXT_WARM;
    textColor = ColorWithAlpha(textColor, alpha);

    DrawTextEx(g_font, p->displayName, (Vector2){SIDEBAR_PADDING + 8, y + 10.0f}, 16, 1, textColor);
}

static void DrawSidebar(const PluginList *plugins, float deltaTime) {
    (void)deltaTime;

    DrawRectangle(0, HEADER_HEIGHT, SIDEBAR_WIDTH, WINDOW_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT,
                  COLOR_WARM_GRAY);
    DrawRectangle(SIDEBAR_WIDTH - 1, HEADER_HEIGHT, 1,
                  WINDOW_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT,
                  ColorWithAlpha(COLOR_FIRE_DEEP, 0.3f));

    int deviceOnly, synced, localOnly;
    GetSectionCounts(plugins, &deviceOnly, &synced, &localOnly);

    int y = HEADER_HEIGHT + SIDEBAR_PADDING;
    Vector2 mouse = GetMousePosition();

    // Check which section mouse is over for drop targeting
    SectionType hoverSection = SECTION_LOCAL_ONLY;
    bool mouseInSidebar = (mouse.x < SIDEBAR_WIDTH && mouse.y > HEADER_HEIGHT && mouse.y < WINDOW_HEIGHT - FOOTER_HEIGHT);

    // =========== SECTION: DEVICE ONLY ===========
    int sectionStartY = y;
    bool isDropTarget = g_drag.isDragging && g_drag.sourceSection == SECTION_LOCAL_ONLY && mouseInSidebar;

    // Check if mouse is in this section
    int sectionHeight = 24 + (deviceOnly > 0 ? deviceOnly * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING) : 20);
    g_sectionRects[SECTION_DEVICE_ONLY] = (Rectangle){0, (float)sectionStartY, SIDEBAR_WIDTH, (float)sectionHeight};
    if (mouseInSidebar && mouse.y >= sectionStartY && mouse.y < sectionStartY + sectionHeight) {
        hoverSection = SECTION_DEVICE_ONLY;
    }

    DrawSectionHeader("DEVICE ONLY", y, COLOR_INSTALLING, isDropTarget && hoverSection == SECTION_DEVICE_ONLY);
    y += 24;

    if (deviceOnly == 0) {
        const char *msg = (SshGetStatus() == SSH_STATUS_CONNECTED) ? "(none)" : "(connect to view)";
        DrawTextEx(g_font, msg, (Vector2){SIDEBAR_PADDING, (float)y}, 12, 1, COLOR_TEXT_DIM);
        y += 20;
    } else {
        int idx = 0;
        for (int i = 0; i < plugins->count; i++) {
            const PluginInfo *p = &plugins->plugins[i];
            if (p->remotePath[0] != '\0' && p->localPath[0] == '\0') {
                bool selected = (g_selection.section == SECTION_DEVICE_ONLY && g_selection.index == idx);
                bool isDragSource = g_drag.isDragging && strcmp(g_drag.pluginName, p->name) == 0;
                DrawPluginItem(p, y, selected, isDragSource, COLOR_INSTALLING);
                y += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                idx++;
            }
        }
    }

    y += SIDEBAR_SECTION_SPACING / 2;

    // =========== SECTION: SYNCED (BOTH) ===========
    sectionStartY = y;
    sectionHeight = 24 + (synced > 0 ? synced * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING) : 20);
    g_sectionRects[SECTION_SYNCED] = (Rectangle){0, (float)sectionStartY, SIDEBAR_WIDTH, (float)sectionHeight};
    if (mouseInSidebar && mouse.y >= sectionStartY && mouse.y < sectionStartY + sectionHeight) {
        hoverSection = SECTION_SYNCED;
    }

    isDropTarget = g_drag.isDragging && g_drag.sourceSection == SECTION_LOCAL_ONLY && mouseInSidebar;
    DrawSectionHeader("SYNCED", y, COLOR_CONNECTED, isDropTarget && hoverSection == SECTION_SYNCED);
    y += 24;

    if (synced == 0) {
        DrawTextEx(g_font, "(none)", (Vector2){SIDEBAR_PADDING, (float)y}, 12, 1, COLOR_TEXT_DIM);
        y += 20;
    } else {
        int idx = 0;
        for (int i = 0; i < plugins->count; i++) {
            const PluginInfo *p = &plugins->plugins[i];
            if (p->remotePath[0] != '\0' && p->localPath[0] != '\0') {
                bool selected = (g_selection.section == SECTION_SYNCED && g_selection.index == idx);
                bool isDragSource = g_drag.isDragging && strcmp(g_drag.pluginName, p->name) == 0;
                DrawPluginItem(p, y, selected, isDragSource, COLOR_CONNECTED);
                y += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                idx++;
            }
        }
    }

    y += SIDEBAR_SECTION_SPACING / 2;

    // =========== SECTION: LOCAL ONLY ===========
    sectionStartY = y;
    sectionHeight = 24 + (localOnly > 0 ? localOnly * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING) : 20);
    g_sectionRects[SECTION_LOCAL_ONLY] = (Rectangle){0, (float)sectionStartY, SIDEBAR_WIDTH, (float)sectionHeight};
    if (mouseInSidebar && mouse.y >= sectionStartY && mouse.y < sectionStartY + sectionHeight) {
        hoverSection = SECTION_LOCAL_ONLY;
    }

    isDropTarget = g_drag.isDragging && (g_drag.sourceSection == SECTION_SYNCED || g_drag.sourceSection == SECTION_DEVICE_ONLY) && mouseInSidebar;
    DrawSectionHeader("LOCAL ONLY", y, COLOR_EMBER, isDropTarget && hoverSection == SECTION_LOCAL_ONLY);
    y += 24;

    if (localOnly == 0) {
        DrawTextEx(g_font, "(none)", (Vector2){SIDEBAR_PADDING, (float)y}, 12, 1, COLOR_TEXT_DIM);
        y += 20;
    } else {
        int idx = 0;
        for (int i = 0; i < plugins->count; i++) {
            const PluginInfo *p = &plugins->plugins[i];
            if (p->localPath[0] != '\0' && p->remotePath[0] == '\0') {
                bool selected = (g_selection.section == SECTION_LOCAL_ONLY && g_selection.index == idx);
                bool isDragSource = g_drag.isDragging && strcmp(g_drag.pluginName, p->name) == 0;
                DrawPluginItem(p, y, selected, isDragSource, COLOR_EMBER);
                y += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                idx++;
            }
        }
    }

    // Draw drop zone highlight when dragging
    if (g_drag.isDragging && mouseInSidebar) {
        Rectangle dropRect = g_sectionRects[hoverSection];
        bool validDrop = false;

        // Validate drop: LOCAL -> DEVICE/SYNCED = install, DEVICE/SYNCED -> LOCAL = uninstall
        if (g_drag.sourceSection == SECTION_LOCAL_ONLY &&
            (hoverSection == SECTION_DEVICE_ONLY || hoverSection == SECTION_SYNCED)) {
            validDrop = true;  // Install
        } else if ((g_drag.sourceSection == SECTION_SYNCED || g_drag.sourceSection == SECTION_DEVICE_ONLY) &&
                   hoverSection == SECTION_LOCAL_ONLY) {
            validDrop = true;  // Uninstall
        }

        if (validDrop) {
            Color dropColor = ColorWithAlpha(COLOR_GOLD, 0.2f + sinf(g_animTime * 4.0f) * 0.1f);
            DrawRectangleRec(dropRect, dropColor);
            DrawRectangleLinesEx(dropRect, 2, ColorWithAlpha(COLOR_GOLD, 0.6f));
        }
    }
}

static void DrawDragGhost(void) {
    if (!g_drag.isDragging) return;

    Vector2 mouse = GetMousePosition();

    // Draw ghost following cursor
    float ghostAlpha = 0.7f + sinf(g_animTime * 6.0f) * 0.2f;
    Rectangle ghostRect = {mouse.x + 10, mouse.y - 15, 150, 30};

    DrawRectangleRounded(ghostRect, 0.3f, 4, ColorWithAlpha(COLOR_FIRE_DEEP, ghostAlpha * 0.8f));
    DrawRectangleRoundedLines(ghostRect, 0.3f, 4, ColorWithAlpha(COLOR_GOLD, ghostAlpha));

    DrawTextEx(g_font, g_drag.pluginName,
               (Vector2){ghostRect.x + 8, ghostRect.y + 7},
               14, 1, ColorWithAlpha(COLOR_TEXT_BRIGHT, ghostAlpha));

    // Draw action hint
    const char *hint = NULL;
    Vector2 mousePos = GetMousePosition();
    bool inSidebar = mousePos.x < SIDEBAR_WIDTH;

    if (inSidebar) {
        for (int i = 0; i < 3; i++) {
            if (CheckCollisionPointRec(mousePos, g_sectionRects[i])) {
                if (g_drag.sourceSection == SECTION_LOCAL_ONLY && (i == SECTION_DEVICE_ONLY || i == SECTION_SYNCED)) {
                    hint = "Drop to INSTALL";
                } else if ((g_drag.sourceSection == SECTION_SYNCED || g_drag.sourceSection == SECTION_DEVICE_ONLY) && i == SECTION_LOCAL_ONLY) {
                    hint = "Drop to UNINSTALL";
                }
                break;
            }
        }
    }

    if (hint) {
        DrawTextEx(g_font, hint,
                   (Vector2){mouse.x + 10, mouse.y + 20},
                   12, 1, COLOR_GOLD);
    }
}

static void DrawMainPanel(const PluginInfo *plugin) {
    int panelX = PANEL_START_X;
    int panelY = HEADER_HEIGHT;
    int panelW = WINDOW_WIDTH - PANEL_START_X;
    int panelH = WINDOW_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;

    DrawRectangle(panelX, panelY, panelW, panelH, COLOR_CHARCOAL_DARK);

    if (!plugin) {
        DrawTextEx(g_font, "Select a plugin",
                   (Vector2){panelX + PANEL_PADDING, panelY + PANEL_PADDING + 50},
                   20, 1, COLOR_TEXT_DIM);
        DrawTextEx(g_font, "Drag plugins between sections to install/uninstall",
                   (Vector2){panelX + PANEL_PADDING, panelY + PANEL_PADDING + 80},
                   14, 1, COLOR_TEXT_DIM);
        return;
    }

    int y = panelY + PANEL_PADDING;

    // Plugin name
    DrawTextEx(g_font, plugin->displayName, (Vector2){panelX + PANEL_PADDING, (float)y}, 32, 2, COLOR_TEXT_BRIGHT);
    y += 48;

    // Underline
    float pulse = (sinf(g_animTime * 2.0f) + 1.0f) * 0.5f;
    Color lineColor = ColorWithAlpha(COLOR_FIRE_DEEP, 0.4f + pulse * 0.3f);
    DrawRectangle(panelX + PANEL_PADDING, y, 200, 2, lineColor);
    y += 20;

    DrawTextEx(g_font, "llizardgui plugin", (Vector2){panelX + PANEL_PADDING, (float)y}, 16, 1, COLOR_TEXT_DIM);
    y += 30;

    // File sizes
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
            statusText = "Synced (on both)";
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
    bool canInstall = (plugin->localPath[0] != '\0' && plugin->remotePath[0] == '\0' && SshGetStatus() == SSH_STATUS_CONNECTED);
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

    // Progress bar
    const PluginOpState *opState = PluginBrowserGetOpState();
    if (opState->operation != OP_NONE || (opState->complete && g_animTime < 3.0f)) {
        Rectangle progressBounds = {panelX + PANEL_PADDING, (float)y, panelW - PANEL_PADDING * 2, PROGRESS_HEIGHT};
        DrawProgressBar(progressBounds, opState->progress, opState->message);
    }
}

static void DrawProgressBar(Rectangle bounds, float progress, const char *label) {
    DrawRectangleRounded(bounds, PROGRESS_RADIUS, 4, COLOR_ASH);

    Rectangle fill = {bounds.x + 2, bounds.y + 2, (bounds.width - 4) * progress, bounds.height - 4};
    if (fill.width > 0) {
        Color fillColor = (progress >= 1.0f) ? COLOR_CONNECTED : COLOR_FIRE_DEEP;
        DrawRectangleRounded(fill, PROGRESS_RADIUS, 4, fillColor);
    }

    if (label && label[0]) {
        float labelY = bounds.y + bounds.height + 8;
        DrawTextEx(g_font, label, (Vector2){bounds.x, labelY}, 14, 1, COLOR_TEXT_DIM);
    }

    char pctStr[16];
    snprintf(pctStr, sizeof(pctStr), "%.0f%%", progress * 100);
    Vector2 pctSize = MeasureTextEx(g_font, pctStr, 12, 1);
    DrawTextEx(g_font, pctStr,
               (Vector2){bounds.x + bounds.width - pctSize.x, bounds.y + (bounds.height - 12) / 2},
               12, 1, COLOR_TEXT_BRIGHT);
}

static void DrawFooter(void) {
    int footerY = WINDOW_HEIGHT - FOOTER_HEIGHT;

    DrawRectangle(0, footerY, WINDOW_WIDTH, FOOTER_HEIGHT, COLOR_WARM_GRAY);
    DrawRectangle(0, footerY, WINDOW_WIDTH, 1, ColorWithAlpha(COLOR_FIRE_DEEP, 0.3f));

    const char *instructions = "Drag to install/uninstall  |  Arrow keys: Navigate  |  Tab: Switch section  |  R: Refresh";
    DrawTextEx(g_font, instructions,
               (Vector2){HEADER_PADDING, footerY + (FOOTER_HEIGHT - 14) / 2.0f},
               14, 1, COLOR_TEXT_DIM);
}

// ============================================================================
// Input Handling
// ============================================================================

static void HandleInput(const PluginList *plugins) {
    Vector2 mouse = GetMousePosition();
    bool isBusy = PluginBrowserIsBusy();

    int deviceOnly, synced, localOnly;
    GetSectionCounts(plugins, &deviceOnly, &synced, &localOnly);

    // Keyboard navigation
    if (IsKeyPressed(KEY_TAB)) {
        // Cycle through sections
        int tries = 3;
        do {
            g_selection.section = (g_selection.section + 1) % 3;
            g_selection.index = 0;
            tries--;
        } while (GetSectionCount(plugins, g_selection.section) == 0 && tries > 0);
    }

    int currentCount = GetSectionCount(plugins, g_selection.section);
    if (IsKeyPressed(KEY_DOWN) && currentCount > 0) {
        g_selection.index = (g_selection.index + 1) % currentCount;
    }
    if (IsKeyPressed(KEY_UP) && currentCount > 0) {
        g_selection.index = (g_selection.index - 1 + currentCount) % currentCount;
    }

    // Refresh
    if (IsKeyPressed(KEY_R)) {
        g_needsRefresh = true;
    }

    // Keyboard install/uninstall
    const PluginInfo *selectedPlugin = GetSelectedPlugin(plugins);
    if (IsKeyPressed(KEY_ENTER) && selectedPlugin && !isBusy) {
        if (selectedPlugin->localPath[0] != '\0' && selectedPlugin->remotePath[0] == '\0' &&
            SshGetStatus() == SSH_STATUS_CONNECTED) {
            PluginBrowserInstall(selectedPlugin->name);
            g_needsRefresh = true;
        }
    }
    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && selectedPlugin && !isBusy) {
        if (selectedPlugin->remotePath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED) {
            PluginBrowserUninstall(selectedPlugin->name);
            g_needsRefresh = true;
        }
    }

    // Mouse click selection
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && mouse.x < SIDEBAR_WIDTH && !isBusy) {
        // Check which section and item was clicked
        for (int section = 0; section < 3; section++) {
            if (CheckCollisionPointRec(mouse, g_sectionRects[section])) {
                int itemY = (int)g_sectionRects[section].y + 24;  // After header
                int count = GetSectionCount(plugins, section);

                for (int i = 0; i < count; i++) {
                    Rectangle itemRect = {SIDEBAR_PADDING - 4, (float)itemY,
                                          SIDEBAR_WIDTH - SIDEBAR_PADDING * 2 + 8, SIDEBAR_ITEM_HEIGHT};
                    if (CheckCollisionPointRec(mouse, itemRect)) {
                        g_selection.section = section;
                        g_selection.index = i;

                        // Start drag
                        const PluginInfo *p = GetPluginInSection(plugins, section, i);
                        if (p) {
                            g_drag.isDragging = true;
                            strncpy(g_drag.pluginName, p->name, sizeof(g_drag.pluginName) - 1);
                            g_drag.sourceSection = section;
                            g_drag.startPos = mouse;
                            g_drag.dragTime = 0;
                        }
                        break;
                    }
                    itemY += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                }
                break;
            }
        }
    }

    // Mouse button click on main panel buttons
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && selectedPlugin && !isBusy && mouse.x >= PANEL_START_X) {
        int panelX = PANEL_START_X;
        int buttonY = HEADER_HEIGHT + PANEL_PADDING + 48 + 20 + 30 + 24 + 24 + 10 + 50;

        Rectangle installBtn = {panelX + PANEL_PADDING, (float)buttonY, BUTTON_WIDTH, BUTTON_HEIGHT};
        Rectangle uninstallBtn = {panelX + PANEL_PADDING + BUTTON_WIDTH + BUTTON_SPACING, (float)buttonY, BUTTON_WIDTH, BUTTON_HEIGHT};

        if (CheckCollisionPointRec(mouse, installBtn)) {
            if (selectedPlugin->localPath[0] != '\0' && selectedPlugin->remotePath[0] == '\0' &&
                SshGetStatus() == SSH_STATUS_CONNECTED) {
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

    // Update drag
    if (g_drag.isDragging) {
        g_drag.currentPos = mouse;
        g_drag.dragTime += GetFrameTime();

        // Cancel if moved too little (was just a click)
        float dragDist = sqrtf(powf(mouse.x - g_drag.startPos.x, 2) + powf(mouse.y - g_drag.startPos.y, 2));
        if (dragDist < 5 && g_drag.dragTime < 0.15f && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            g_drag.isDragging = false;
        }

        // Handle drop
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            if (mouse.x < SIDEBAR_WIDTH && !isBusy && SshGetStatus() == SSH_STATUS_CONNECTED) {
                for (int section = 0; section < 3; section++) {
                    if (CheckCollisionPointRec(mouse, g_sectionRects[section])) {
                        // Validate and perform action
                        if (g_drag.sourceSection == SECTION_LOCAL_ONLY &&
                            (section == SECTION_DEVICE_ONLY || section == SECTION_SYNCED)) {
                            // Install
                            printf("Salamander: Installing %s (dragged to device)\n", g_drag.pluginName);
                            PluginBrowserInstall(g_drag.pluginName);
                            g_needsRefresh = true;
                        } else if ((g_drag.sourceSection == SECTION_SYNCED || g_drag.sourceSection == SECTION_DEVICE_ONLY) &&
                                   section == SECTION_LOCAL_ONLY) {
                            // Uninstall
                            printf("Salamander: Uninstalling %s (dragged to local)\n", g_drag.pluginName);
                            PluginBrowserUninstall(g_drag.pluginName);
                            g_needsRefresh = true;
                        }
                        break;
                    }
                }
            }

            g_drag.isDragging = false;
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
    const char *localPath = "../../../build-armv7-drm";
    if (argc > 1) {
        localPath = argv[1];
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Salamander - Plugin Manager");
    SetTargetFPS(TARGET_FPS);
    LoadAppFont();

    SshInit(NULL, NULL, NULL);
    PluginBrowserInit(localPath);

    char absPath[512] = {0};
    if (realpath(localPath, absPath)) {
        printf("Salamander: Looking for plugins in %s\n", absPath);
    } else {
        printf("Salamander: Looking for plugins in %s (path may not exist)\n", localPath);
    }
    printf("Salamander: Will connect to CarThing at %s\n", SshGetHost());

    SshCheckConnection();

    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        g_animTime += deltaTime;

        // Periodic connection check
        g_connectionCheckTimer += deltaTime;
        if (g_connectionCheckTimer >= 5.0f) {
            SshCheckConnection();
            g_connectionCheckTimer = 0.0f;
        }

        // Refresh plugins
        if (g_needsRefresh && !PluginBrowserIsBusy()) {
            PluginBrowserRefresh();
            g_needsRefresh = false;

            // Reset selection if invalid
            int count = GetSectionCount(PluginBrowserGetList(), g_selection.section);
            if (g_selection.index >= count) {
                g_selection.index = count > 0 ? count - 1 : 0;
            }
        }

        const PluginList *plugins = PluginBrowserGetList();
        HandleInput(plugins);

        const PluginInfo *selectedPlugin = GetSelectedPlugin(plugins);

        BeginDrawing();
        ClearBackground(COLOR_CHARCOAL_DARK);

        DrawBackground();
        DrawEmberGlow(g_animTime);
        DrawHeader();
        DrawSidebar(plugins, deltaTime);
        DrawMainPanel(selectedPlugin);
        DrawFooter();
        DrawDragGhost();

        EndDrawing();
    }

    PluginBrowserShutdown();
    SshShutdown();
    UnloadAppFont();
    CloseWindow();

    return 0;
}
