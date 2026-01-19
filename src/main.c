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

// Smooth scrolling state
static float g_scrollOffset = 0.0f;
static float g_targetScroll = 0.0f;
static float g_totalContentHeight = 0.0f;

// Sidebar content area
#define SIDEBAR_CONTENT_TOP (HEADER_HEIGHT)
#define SIDEBAR_CONTENT_HEIGHT (WINDOW_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT)

// Section rectangles for drop detection (in content space, before scroll)
static Rectangle g_sectionRects[3] = {0};

// Hover state for sidebar
static SectionType g_hoverSection = SECTION_LOCAL_ONLY;
static int g_hoverIndex = -1;  // -1 means no hover

// Button rectangles (set by DrawMainPanel, used by HandleInput)
static Rectangle g_installBtn = {0};
static Rectangle g_uninstallBtn = {0};

// Button press animation state
static float g_installBtnPress = 0.0f;   // 0 = normal, 1 = fully pressed
static float g_uninstallBtnPress = 0.0f;

// Toast notification system
typedef struct {
    char message[128];
    bool isSuccess;
    float timer;        // Counts down from TOAST_DURATION
    float slideIn;      // Animation 0->1
    bool active;
} Toast;

#define TOAST_DURATION 3.0f
#define TOAST_SLIDE_SPEED 8.0f
static Toast g_toast = {0};

// Salamander success messages
static const char *g_successMessages[] = {
    "Toasty! %s installed",
    "Fired up! %s is ready",
    "Blazing! %s deployed",
    "Sizzling! %s complete",
    "Hot stuff! %s installed",
};
static const int g_successMessageCount = 5;

// Salamander failure messages
static const char *g_failMessages[] = {
    "Fizzled! %s failed",
    "Extinguished! %s error",
    "Cooled off! %s failed",
    "Smoke out! %s error",
};
static const int g_failMessageCount = 4;

// Last operation tracking for toast display
static bool g_lastOpComplete = true;
static bool g_lastOpSuccess = false;
static char g_lastOpPlugin[64] = {0};
static PluginOperation g_lastOpType = OP_NONE;

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
static void DrawToast(void);
static void ShowToast(const char *pluginName, bool isSuccess, bool isInstall);
static void UpdateToast(float deltaTime);
static void UpdateButtonAnimations(float deltaTime);
static void HandleInput(const PluginList *plugins);
static void UpdateScroll(float deltaTime);
static void ScrollToSelection(const PluginList *plugins);
static const PluginInfo *GetSelectedPlugin(const PluginList *plugins);
static void GetSectionCounts(const PluginList *plugins, int *deviceOnly, int *synced, int *localOnly);
static const PluginInfo *GetPluginInSection(const PluginList *plugins, SectionType section, int index);
static float GetSelectionY(const PluginList *plugins);

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

// Calculate Y position of selected item in content space (before scroll)
static float GetSelectionY(const PluginList *plugins) {
    int deviceOnly, synced, localOnly;
    GetSectionCounts(plugins, &deviceOnly, &synced, &localOnly);

    float y = SIDEBAR_PADDING;

    // DEVICE ONLY section
    y += 24;  // Header
    if (deviceOnly == 0) {
        y += 20;
    } else {
        if (g_selection.section == SECTION_DEVICE_ONLY) {
            return y + g_selection.index * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING);
        }
        y += deviceOnly * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING);
    }
    y += SIDEBAR_SECTION_SPACING / 2;

    // SYNCED section
    y += 24;  // Header
    if (synced == 0) {
        y += 20;
    } else {
        if (g_selection.section == SECTION_SYNCED) {
            return y + g_selection.index * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING);
        }
        y += synced * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING);
    }
    y += SIDEBAR_SECTION_SPACING / 2;

    // LOCAL ONLY section
    y += 24;  // Header
    if (g_selection.section == SECTION_LOCAL_ONLY) {
        return y + g_selection.index * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING);
    }

    return y;
}

// ============================================================================
// Scrolling
// ============================================================================

static void UpdateScroll(float deltaTime) {
    // Smooth interpolation (glass on glass effect)
    float diff = g_targetScroll - g_scrollOffset;
    float speed = 12.0f;

    g_scrollOffset += diff * speed * deltaTime;

    // Snap when very close
    if (fabsf(diff) < 0.5f) {
        g_scrollOffset = g_targetScroll;
    }
}

static void ScrollToSelection(const PluginList *plugins) {
    float selY = GetSelectionY(plugins);
    float itemHeight = SIDEBAR_ITEM_HEIGHT;

    // Calculate visible area
    float visibleTop = g_targetScroll;
    float visibleBottom = g_targetScroll + SIDEBAR_CONTENT_HEIGHT;

    // Margins
    float topMargin = 40;
    float bottomMargin = 60;

    // Calculate max scroll
    float maxScroll = g_totalContentHeight - SIDEBAR_CONTENT_HEIGHT;
    if (maxScroll < 0) maxScroll = 0;

    // Scroll to keep selection visible
    if (selY < visibleTop + topMargin) {
        g_targetScroll = selY - topMargin;
    } else if (selY + itemHeight > visibleBottom - bottomMargin) {
        g_targetScroll = selY + itemHeight - SIDEBAR_CONTENT_HEIGHT + bottomMargin;
    }

    // Clamp
    if (g_targetScroll < 0) g_targetScroll = 0;
    if (g_targetScroll > maxScroll) g_targetScroll = maxScroll;
}

// ============================================================================
// Toast Notifications & Button Animations
// ============================================================================

static void ShowToast(const char *pluginName, bool isSuccess, bool isInstall) {
    g_toast.active = true;
    g_toast.isSuccess = isSuccess;
    g_toast.timer = TOAST_DURATION;
    g_toast.slideIn = 0.0f;

    // Pick a random fun message
    if (isSuccess) {
        int idx = GetRandomValue(0, g_successMessageCount - 1);
        if (isInstall) {
            snprintf(g_toast.message, sizeof(g_toast.message), g_successMessages[idx], pluginName);
        } else {
            // Uninstall success messages
            const char *uninstallMsgs[] = {
                "Cooled down! %s removed",
                "Ashes cleared! %s gone",
                "Extinguished! %s removed",
            };
            snprintf(g_toast.message, sizeof(g_toast.message), uninstallMsgs[idx % 3], pluginName);
        }
    } else {
        int idx = GetRandomValue(0, g_failMessageCount - 1);
        snprintf(g_toast.message, sizeof(g_toast.message), g_failMessages[idx], pluginName);
    }
}

static void UpdateToast(float deltaTime) {
    if (!g_toast.active) return;

    // Slide in animation
    if (g_toast.slideIn < 1.0f) {
        g_toast.slideIn += deltaTime * TOAST_SLIDE_SPEED;
        if (g_toast.slideIn > 1.0f) g_toast.slideIn = 1.0f;
    }

    // Count down timer
    g_toast.timer -= deltaTime;

    // Slide out when timer runs low
    if (g_toast.timer <= 0.5f) {
        g_toast.slideIn = g_toast.timer / 0.5f;
    }

    if (g_toast.timer <= 0.0f) {
        g_toast.active = false;
    }
}

static void UpdateButtonAnimations(float deltaTime) {
    // Decay button press animations
    float decaySpeed = 8.0f;
    if (g_installBtnPress > 0.0f) {
        g_installBtnPress -= deltaTime * decaySpeed;
        if (g_installBtnPress < 0.0f) g_installBtnPress = 0.0f;
    }
    if (g_uninstallBtnPress > 0.0f) {
        g_uninstallBtnPress -= deltaTime * decaySpeed;
        if (g_uninstallBtnPress < 0.0f) g_uninstallBtnPress = 0.0f;
    }
}

static void DrawToast(void) {
    if (!g_toast.active) return;

    // Calculate toast position (slides in from right)
    float toastWidth = 320;
    float toastHeight = 60;
    float padding = 20;

    // Ease out cubic for smooth slide
    float ease = 1.0f - powf(1.0f - g_toast.slideIn, 3.0f);
    float xOffset = (1.0f - ease) * (toastWidth + padding);

    float x = WINDOW_WIDTH - toastWidth - padding + xOffset;
    float y = HEADER_HEIGHT + padding;

    Rectangle toastRect = {x, y, toastWidth, toastHeight};

    // Background with glow
    Color bgColor = g_toast.isSuccess ? (Color){30, 60, 30, 240} : (Color){60, 30, 30, 240};
    Color glowColor = g_toast.isSuccess ? COLOR_CONNECTED : COLOR_DISCONNECTED;
    Color borderColor = g_toast.isSuccess ? COLOR_CONNECTED : COLOR_DISCONNECTED;

    // Glow effect
    float glowPulse = (sinf(g_animTime * 4.0f) + 1.0f) * 0.5f;
    for (int i = 8; i > 0; i--) {
        float alpha = (8 - i) / 8.0f * 0.15f * (0.7f + glowPulse * 0.3f);
        Rectangle glowRect = {toastRect.x - i, toastRect.y - i, toastRect.width + i*2, toastRect.height + i*2};
        DrawRectangleRounded(glowRect, 0.3f, 4, ColorWithAlpha(glowColor, alpha));
    }

    // Main toast body
    DrawRectangleRounded(toastRect, 0.3f, 4, bgColor);
    DrawRectangleRoundedLines(toastRect, 0.3f, 4, borderColor);

    // Icon
    float iconX = x + 16;
    float iconY = y + toastHeight / 2;
    if (g_toast.isSuccess) {
        // Checkmark / flame icon
        DrawCircle((int)iconX, (int)iconY, 12, ColorWithAlpha(COLOR_CONNECTED, 0.3f));
        DrawTextEx(g_font, "*", (Vector2){iconX - 6, iconY - 10}, 24, 1, COLOR_GOLD);
    } else {
        // X / error icon
        DrawCircle((int)iconX, (int)iconY, 12, ColorWithAlpha(COLOR_DISCONNECTED, 0.3f));
        DrawTextEx(g_font, "X", (Vector2){iconX - 6, iconY - 10}, 20, 1, COLOR_DISCONNECTED);
    }

    // Message text
    Color textColor = g_toast.isSuccess ? COLOR_CONNECTED : (Color){255, 180, 180, 255};
    DrawTextEx(g_font, g_toast.message, (Vector2){x + 40, y + 12}, 16, 1, textColor);

    // Subtitle
    const char *subtitle = g_toast.isSuccess ? "Operation complete" : "Please try again";
    DrawTextEx(g_font, subtitle, (Vector2){x + 40, y + 34}, 12, 1, COLOR_TEXT_DIM);
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

    for (int i = 0; i < 40; i++) {
        float alpha = glowAlpha * (1.0f - i / 40.0f) * 0.15f;
        Color glow = ColorWithAlpha(COLOR_FIRE_DEEP, alpha);
        DrawRectangle(0, i, WINDOW_WIDTH, 1, glow);
    }

    for (int i = 0; i < 60; i++) {
        float alpha = glowAlpha * (1.0f - i / 60.0f) * 0.2f;
        Color glow = ColorWithAlpha(COLOR_EMBER, alpha);
        DrawRectangle(0, WINDOW_HEIGHT - 60 + i, WINDOW_WIDTH, 1, glow);
    }

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

static void DrawSectionHeader(const char *title, float y, Color accentColor, bool isDropTarget) {
    if (isDropTarget && g_drag.isDragging) {
        DrawRectangle(SIDEBAR_PADDING - 8, (int)y - 4, SIDEBAR_WIDTH - SIDEBAR_PADDING * 2 + 16, 22,
                      ColorWithAlpha(accentColor, 0.3f));
    }
    DrawTextEx(g_font, title, (Vector2){SIDEBAR_PADDING, y}, 14, 1, accentColor);
}

static void DrawPluginItem(const PluginInfo *p, float y, bool selected, bool hovered, bool isDragSource, Color accentColor) {
    Rectangle itemRect = {SIDEBAR_PADDING - 4, y, SIDEBAR_WIDTH - SIDEBAR_PADDING * 2 + 8, SIDEBAR_ITEM_HEIGHT};

    float alpha = isDragSource ? 0.3f : 1.0f;

    if (selected && !isDragSource) {
        DrawRectangleRounded(itemRect, 0.2f, 4, COLOR_CARD_SELECTED);
        DrawRectangle((int)itemRect.x, (int)itemRect.y + 6, 3, (int)itemRect.height - 12, accentColor);
    } else if (hovered && !isDragSource) {
        // Hover state - subtle highlight
        DrawRectangleRounded(itemRect, 0.2f, 4, COLOR_CARD_HOVER);
    }

    Color textColor = selected ? COLOR_TEXT_BRIGHT : (hovered ? COLOR_TEXT_BRIGHT : COLOR_TEXT_WARM);
    textColor = ColorWithAlpha(textColor, alpha);

    DrawTextEx(g_font, p->displayName, (Vector2){SIDEBAR_PADDING + 8, y + 10.0f}, 16, 1, textColor);
}

static void DrawSidebar(const PluginList *plugins, float deltaTime) {
    (void)deltaTime;

    // Sidebar background
    DrawRectangle(0, HEADER_HEIGHT, SIDEBAR_WIDTH, SIDEBAR_CONTENT_HEIGHT, COLOR_WARM_GRAY);
    DrawRectangle(SIDEBAR_WIDTH - 1, HEADER_HEIGHT, 1, SIDEBAR_CONTENT_HEIGHT,
                  ColorWithAlpha(COLOR_FIRE_DEEP, 0.3f));

    int deviceOnly, synced, localOnly;
    GetSectionCounts(plugins, &deviceOnly, &synced, &localOnly);

    Vector2 mouse = GetMousePosition();
    bool mouseInSidebar = (mouse.x < SIDEBAR_WIDTH && mouse.y > HEADER_HEIGHT && mouse.y < WINDOW_HEIGHT - FOOTER_HEIGHT);

    // Reset hover state
    g_hoverIndex = -1;

    // Begin scissor mode for scrolling content
    BeginScissorMode(0, SIDEBAR_CONTENT_TOP, SIDEBAR_WIDTH, SIDEBAR_CONTENT_HEIGHT);

    // Content Y starts at padding, offset by scroll
    float y = SIDEBAR_CONTENT_TOP + SIDEBAR_PADDING - g_scrollOffset;
    float contentY = SIDEBAR_PADDING;  // For section rect calculation (without screen offset)

    // Adjusted mouse Y for scroll
    float adjustedMouseY = mouse.y - SIDEBAR_CONTENT_TOP + g_scrollOffset;

    SectionType hoverSection = SECTION_LOCAL_ONLY;

    // =========== SECTION: DEVICE ONLY ===========
    float sectionStartY = contentY;
    int sectionHeight = 24 + (deviceOnly > 0 ? deviceOnly * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING) : 20);
    g_sectionRects[SECTION_DEVICE_ONLY] = (Rectangle){0, sectionStartY, SIDEBAR_WIDTH, (float)sectionHeight};

    if (mouseInSidebar && adjustedMouseY >= sectionStartY && adjustedMouseY < sectionStartY + sectionHeight) {
        hoverSection = SECTION_DEVICE_ONLY;
    }

    bool isDropTarget = g_drag.isDragging && g_drag.sourceSection == SECTION_LOCAL_ONLY && mouseInSidebar;
    DrawSectionHeader("DEVICE ONLY", y, COLOR_INSTALLING, isDropTarget && hoverSection == SECTION_DEVICE_ONLY);
    y += 24;
    contentY += 24;

    if (deviceOnly == 0) {
        const char *msg = (SshGetStatus() == SSH_STATUS_CONNECTED) ? "(none)" : "(connect to view)";
        DrawTextEx(g_font, msg, (Vector2){SIDEBAR_PADDING, y}, 12, 1, COLOR_TEXT_DIM);
        y += 20;
        contentY += 20;
    } else {
        int idx = 0;
        for (int i = 0; i < plugins->count; i++) {
            const PluginInfo *p = &plugins->plugins[i];
            if (p->remotePath[0] != '\0' && p->localPath[0] == '\0') {
                bool selected = (g_selection.section == SECTION_DEVICE_ONLY && g_selection.index == idx);
                bool isDragSource = g_drag.isDragging && strcmp(g_drag.pluginName, p->name) == 0;

                // Check hover
                bool hovered = false;
                if (mouseInSidebar && !g_drag.isDragging) {
                    float itemTop = contentY;
                    float itemBottom = contentY + SIDEBAR_ITEM_HEIGHT;
                    if (adjustedMouseY >= itemTop && adjustedMouseY < itemBottom) {
                        g_hoverSection = SECTION_DEVICE_ONLY;
                        g_hoverIndex = idx;
                        hovered = true;
                    }
                }

                DrawPluginItem(p, y, selected, hovered, isDragSource, COLOR_INSTALLING);
                y += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                contentY += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                idx++;
            }
        }
    }

    y += SIDEBAR_SECTION_SPACING / 2;
    contentY += SIDEBAR_SECTION_SPACING / 2;

    // =========== SECTION: SYNCED ===========
    sectionStartY = contentY;
    sectionHeight = 24 + (synced > 0 ? synced * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING) : 20);
    g_sectionRects[SECTION_SYNCED] = (Rectangle){0, sectionStartY, SIDEBAR_WIDTH, (float)sectionHeight};

    if (mouseInSidebar && adjustedMouseY >= sectionStartY && adjustedMouseY < sectionStartY + sectionHeight) {
        hoverSection = SECTION_SYNCED;
    }

    isDropTarget = g_drag.isDragging && g_drag.sourceSection == SECTION_LOCAL_ONLY && mouseInSidebar;
    DrawSectionHeader("SYNCED", y, COLOR_CONNECTED, isDropTarget && hoverSection == SECTION_SYNCED);
    y += 24;
    contentY += 24;

    if (synced == 0) {
        DrawTextEx(g_font, "(none)", (Vector2){SIDEBAR_PADDING, y}, 12, 1, COLOR_TEXT_DIM);
        y += 20;
        contentY += 20;
    } else {
        int idx = 0;
        for (int i = 0; i < plugins->count; i++) {
            const PluginInfo *p = &plugins->plugins[i];
            if (p->remotePath[0] != '\0' && p->localPath[0] != '\0') {
                bool selected = (g_selection.section == SECTION_SYNCED && g_selection.index == idx);
                bool isDragSource = g_drag.isDragging && strcmp(g_drag.pluginName, p->name) == 0;

                // Check hover
                bool hovered = false;
                if (mouseInSidebar && !g_drag.isDragging) {
                    float itemTop = contentY;
                    float itemBottom = contentY + SIDEBAR_ITEM_HEIGHT;
                    if (adjustedMouseY >= itemTop && adjustedMouseY < itemBottom) {
                        g_hoverSection = SECTION_SYNCED;
                        g_hoverIndex = idx;
                        hovered = true;
                    }
                }

                DrawPluginItem(p, y, selected, hovered, isDragSource, COLOR_CONNECTED);
                y += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                contentY += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                idx++;
            }
        }
    }

    y += SIDEBAR_SECTION_SPACING / 2;
    contentY += SIDEBAR_SECTION_SPACING / 2;

    // =========== SECTION: LOCAL ONLY ===========
    sectionStartY = contentY;
    sectionHeight = 24 + (localOnly > 0 ? localOnly * (SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING) : 20);
    g_sectionRects[SECTION_LOCAL_ONLY] = (Rectangle){0, sectionStartY, SIDEBAR_WIDTH, (float)sectionHeight};

    if (mouseInSidebar && adjustedMouseY >= sectionStartY && adjustedMouseY < sectionStartY + sectionHeight) {
        hoverSection = SECTION_LOCAL_ONLY;
    }

    isDropTarget = g_drag.isDragging && (g_drag.sourceSection == SECTION_SYNCED || g_drag.sourceSection == SECTION_DEVICE_ONLY) && mouseInSidebar;
    DrawSectionHeader("LOCAL ONLY", y, COLOR_EMBER, isDropTarget && hoverSection == SECTION_LOCAL_ONLY);
    y += 24;
    contentY += 24;

    if (localOnly == 0) {
        DrawTextEx(g_font, "(none)", (Vector2){SIDEBAR_PADDING, y}, 12, 1, COLOR_TEXT_DIM);
        y += 20;
        contentY += 20;
    } else {
        int idx = 0;
        for (int i = 0; i < plugins->count; i++) {
            const PluginInfo *p = &plugins->plugins[i];
            if (p->localPath[0] != '\0' && p->remotePath[0] == '\0') {
                bool selected = (g_selection.section == SECTION_LOCAL_ONLY && g_selection.index == idx);
                bool isDragSource = g_drag.isDragging && strcmp(g_drag.pluginName, p->name) == 0;

                // Check hover
                bool hovered = false;
                if (mouseInSidebar && !g_drag.isDragging) {
                    float itemTop = contentY;
                    float itemBottom = contentY + SIDEBAR_ITEM_HEIGHT;
                    if (adjustedMouseY >= itemTop && adjustedMouseY < itemBottom) {
                        g_hoverSection = SECTION_LOCAL_ONLY;
                        g_hoverIndex = idx;
                        hovered = true;
                    }
                }

                DrawPluginItem(p, y, selected, hovered, isDragSource, COLOR_EMBER);
                y += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                contentY += SIDEBAR_ITEM_HEIGHT + SIDEBAR_ITEM_SPACING;
                idx++;
            }
        }
    }

    // Store total content height for scroll calculations
    g_totalContentHeight = contentY + SIDEBAR_PADDING;

    // Draw drop zone highlight
    if (g_drag.isDragging && mouseInSidebar) {
        Rectangle dropRect = g_sectionRects[hoverSection];
        // Adjust to screen coordinates
        dropRect.y = dropRect.y - g_scrollOffset + SIDEBAR_CONTENT_TOP;

        bool validDrop = false;
        if (g_drag.sourceSection == SECTION_LOCAL_ONLY &&
            (hoverSection == SECTION_DEVICE_ONLY || hoverSection == SECTION_SYNCED)) {
            validDrop = true;
        } else if ((g_drag.sourceSection == SECTION_SYNCED || g_drag.sourceSection == SECTION_DEVICE_ONLY) &&
                   hoverSection == SECTION_LOCAL_ONLY) {
            validDrop = true;
        }

        if (validDrop) {
            Color dropColor = ColorWithAlpha(COLOR_GOLD, 0.2f + sinf(g_animTime * 4.0f) * 0.1f);
            DrawRectangleRec(dropRect, dropColor);
            DrawRectangleLinesEx(dropRect, 2, ColorWithAlpha(COLOR_GOLD, 0.6f));
        }
    }

    EndScissorMode();

    // Draw scroll indicators
    float maxScroll = g_totalContentHeight - SIDEBAR_CONTENT_HEIGHT;
    if (maxScroll > 0) {
        // Top fade if scrolled down
        if (g_scrollOffset > 1.0f) {
            for (int i = 0; i < 20; i++) {
                float alpha = (20 - i) / 20.0f * 0.6f;
                DrawRectangle(0, SIDEBAR_CONTENT_TOP + i, SIDEBAR_WIDTH - 1, 1,
                              ColorWithAlpha(COLOR_WARM_GRAY, alpha));
            }
        }
        // Bottom fade if more content below
        if (g_scrollOffset < maxScroll - 1.0f) {
            int bottomY = SIDEBAR_CONTENT_TOP + SIDEBAR_CONTENT_HEIGHT;
            for (int i = 0; i < 20; i++) {
                float alpha = i / 20.0f * 0.6f;
                DrawRectangle(0, bottomY - 20 + i, SIDEBAR_WIDTH - 1, 1,
                              ColorWithAlpha(COLOR_WARM_GRAY, alpha));
            }
        }
    }
}

static void DrawDragGhost(void) {
    if (!g_drag.isDragging) return;

    Vector2 mouse = GetMousePosition();

    float ghostAlpha = 0.7f + sinf(g_animTime * 6.0f) * 0.2f;
    Rectangle ghostRect = {mouse.x + 10, mouse.y - 15, 150, 30};

    DrawRectangleRounded(ghostRect, 0.3f, 4, ColorWithAlpha(COLOR_FIRE_DEEP, ghostAlpha * 0.8f));
    DrawRectangleRoundedLines(ghostRect, 0.3f, 4, ColorWithAlpha(COLOR_GOLD, ghostAlpha));

    DrawTextEx(g_font, g_drag.pluginName,
               (Vector2){ghostRect.x + 8, ghostRect.y + 7},
               14, 1, ColorWithAlpha(COLOR_TEXT_BRIGHT, ghostAlpha));

    const char *hint = NULL;
    bool inSidebar = mouse.x < SIDEBAR_WIDTH && mouse.y > HEADER_HEIGHT && mouse.y < WINDOW_HEIGHT - FOOTER_HEIGHT;

    if (inSidebar) {
        float adjustedMouseY = mouse.y - SIDEBAR_CONTENT_TOP + g_scrollOffset;
        for (int i = 0; i < 3; i++) {
            if (adjustedMouseY >= g_sectionRects[i].y &&
                adjustedMouseY < g_sectionRects[i].y + g_sectionRects[i].height) {
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

    DrawTextEx(g_font, plugin->displayName, (Vector2){panelX + PANEL_PADDING, (float)y}, 32, 2, COLOR_TEXT_BRIGHT);
    y += 48;

    float pulse = (sinf(g_animTime * 2.0f) + 1.0f) * 0.5f;
    Color lineColor = ColorWithAlpha(COLOR_FIRE_DEEP, 0.4f + pulse * 0.3f);
    DrawRectangle(panelX + PANEL_PADDING, y, 200, 2, lineColor);
    y += 20;

    DrawTextEx(g_font, "llizardgui plugin", (Vector2){panelX + PANEL_PADDING, (float)y}, 16, 1, COLOR_TEXT_DIM);
    y += 30;

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

    bool canInstall = (plugin->localPath[0] != '\0' && plugin->remotePath[0] == '\0' && SshGetStatus() == SSH_STATUS_CONNECTED);
    bool canUninstall = (plugin->remotePath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED);
    bool isBusy = PluginBrowserIsBusy();

    // Check which operation is in progress
    const PluginOpState *opState = PluginBrowserGetOpState();
    bool isInstalling = (opState->operation == OP_INSTALLING && !opState->complete);
    bool isUninstalling = (opState->operation == OP_UNINSTALLING && !opState->complete);

    // Store button positions for click detection
    g_installBtn = (Rectangle){panelX + PANEL_PADDING, (float)y, BUTTON_WIDTH, BUTTON_HEIGHT};
    g_uninstallBtn = (Rectangle){panelX + PANEL_PADDING + BUTTON_WIDTH + BUTTON_SPACING, (float)y, BUTTON_WIDTH, BUTTON_HEIGHT};

    Vector2 mouse = GetMousePosition();
    bool installHovered = CheckCollisionPointRec(mouse, g_installBtn) && canInstall && !isBusy;
    bool uninstallHovered = CheckCollisionPointRec(mouse, g_uninstallBtn) && canUninstall && !isBusy;

    // ========== INSTALL BUTTON ==========
    float installPress = g_installBtnPress;

    // Lizard breathing animation when working
    float installBreath = 0.0f;
    float installWiggle = 0.0f;
    if (isInstalling) {
        installBreath = sinf(g_animTime * 3.0f) * 0.03f;  // Breathing pulse
        installWiggle = sinf(g_animTime * 8.0f) * 1.5f;   // Quick wiggle like a lizard
    }

    float installScale = 1.0f - installPress * 0.05f + installBreath;
    float installOffsetY = installPress * 2.0f;
    float installOffsetX = installWiggle;

    Rectangle installDrawRect = {
        g_installBtn.x + installOffsetX + (g_installBtn.width * (1.0f - installScale)) / 2,
        g_installBtn.y + installOffsetY + (g_installBtn.height * (1.0f - installScale)) / 2,
        g_installBtn.width * installScale,
        g_installBtn.height * installScale
    };

    Color installBg;
    if (isInstalling) {
        // Animated fire gradient when working - lizard on fire!
        float firePhase = fmodf(g_animTime * 2.0f, 1.0f);
        float firePulse = (sinf(g_animTime * 6.0f) + 1.0f) * 0.5f;
        installBg = (Color){
            (unsigned char)(180 + (int)(firePulse * 75)),  // R: 180-255
            (unsigned char)(50 + (int)(firePhase * 80)),   // G: 50-130 crawling
            (unsigned char)(10 + (int)(firePulse * 30)),   // B: 10-40
            255
        };
    } else if (canInstall && !isBusy) {
        if (installPress > 0.1f) {
            installBg = COLOR_GOLD;
        } else {
            installBg = installHovered ? COLOR_FLAME_ORANGE : COLOR_FIRE_DEEP;
        }
    } else {
        installBg = ColorWithAlpha(COLOR_FIRE_DEEP, 0.3f);
    }
    Color installTextColor = (canInstall && !isBusy) || isInstalling ? COLOR_TEXT_BRIGHT : COLOR_TEXT_DIM;

    // Glow effect when pressed or working
    if (installPress > 0.1f || isInstalling) {
        float glowIntensity = isInstalling ? (0.3f + sinf(g_animTime * 4.0f) * 0.2f) : (0.3f * installPress);
        for (int i = 8; i > 0; i--) {
            float alpha = (8 - i) / 8.0f * glowIntensity;
            Rectangle glowRect = {installDrawRect.x - i*2, installDrawRect.y - i*2,
                                  installDrawRect.width + i*4, installDrawRect.height + i*4};
            Color glowColor = isInstalling ? COLOR_FLAME_ORANGE : COLOR_GOLD;
            DrawRectangleRounded(glowRect, BUTTON_RADIUS, 4, ColorWithAlpha(glowColor, alpha));
        }
    }

    DrawRectangleRounded(installDrawRect, BUTTON_RADIUS, 4, installBg);

    // Fire crawl effect on border when working
    if (isInstalling) {
        float crawl = fmodf(g_animTime * 3.0f, 1.0f);
        // Draw animated segments around the button
        for (int seg = 0; seg < 6; seg++) {
            float segPos = fmodf(crawl + seg * 0.166f, 1.0f);
            float segAlpha = 0.5f + sinf(segPos * 3.14159f) * 0.5f;

            float px, py;
            if (segPos < 0.25f) {
                px = installDrawRect.x + segPos * 4 * installDrawRect.width;
                py = installDrawRect.y;
            } else if (segPos < 0.5f) {
                px = installDrawRect.x + installDrawRect.width;
                py = installDrawRect.y + (segPos - 0.25f) * 4 * installDrawRect.height;
            } else if (segPos < 0.75f) {
                px = installDrawRect.x + installDrawRect.width - (segPos - 0.5f) * 4 * installDrawRect.width;
                py = installDrawRect.y + installDrawRect.height;
            } else {
                px = installDrawRect.x;
                py = installDrawRect.y + installDrawRect.height - (segPos - 0.75f) * 4 * installDrawRect.height;
            }
            DrawCircle((int)px, (int)py, 4, ColorWithAlpha(COLOR_GOLD, segAlpha * 0.8f));
        }
    } else if (installHovered && installPress < 0.1f) {
        DrawRectangleRoundedLines(installDrawRect, BUTTON_RADIUS, 4, ColorWithAlpha(COLOR_GOLD, 0.8f));
    }

    const char *installLabel = isInstalling ? "INSTALLING..." : "INSTALL";
    Vector2 installSize = MeasureTextEx(g_font, installLabel, 16, 1);
    DrawTextEx(g_font, installLabel,
               (Vector2){installDrawRect.x + (installDrawRect.width - installSize.x) / 2,
                         installDrawRect.y + (installDrawRect.height - 16) / 2},
               16, 1, installTextColor);

    // ========== UNINSTALL BUTTON ==========
    float uninstallPress = g_uninstallBtnPress;

    // Lizard breathing animation when working
    float uninstallBreath = 0.0f;
    float uninstallWiggle = 0.0f;
    if (isUninstalling) {
        uninstallBreath = sinf(g_animTime * 3.0f) * 0.03f;
        uninstallWiggle = sinf(g_animTime * 8.0f) * 1.5f;
    }

    float uninstallScale = 1.0f - uninstallPress * 0.05f + uninstallBreath;
    float uninstallOffsetY = uninstallPress * 2.0f;
    float uninstallOffsetX = uninstallWiggle;

    Rectangle uninstallDrawRect = {
        g_uninstallBtn.x + uninstallOffsetX + (g_uninstallBtn.width * (1.0f - uninstallScale)) / 2,
        g_uninstallBtn.y + uninstallOffsetY + (g_uninstallBtn.height * (1.0f - uninstallScale)) / 2,
        g_uninstallBtn.width * uninstallScale,
        g_uninstallBtn.height * uninstallScale
    };

    Color uninstallBg;
    if (isUninstalling) {
        // Animated cool-down gradient - ember fading
        float coolPhase = fmodf(g_animTime * 2.0f, 1.0f);
        float coolPulse = (sinf(g_animTime * 5.0f) + 1.0f) * 0.5f;
        uninstallBg = (Color){
            (unsigned char)(120 + (int)(coolPulse * 60)),   // R: dimming red
            (unsigned char)(40 + (int)(coolPhase * 30)),    // G: slight variation
            (unsigned char)(50 + (int)(coolPulse * 30)),    // B: cooler tones
            255
        };
    } else if (canUninstall && !isBusy) {
        if (uninstallPress > 0.1f) {
            uninstallBg = COLOR_DISCONNECTED;
        } else {
            uninstallBg = uninstallHovered ? (Color){74, 64, 68, 255} : COLOR_ASH;
        }
    } else {
        uninstallBg = ColorWithAlpha(COLOR_ASH, 0.3f);
    }
    Color uninstallTextColor = (canUninstall && !isBusy) || isUninstalling ? COLOR_TEXT_WARM : COLOR_TEXT_DIM;

    // Glow effect when pressed or working
    if (uninstallPress > 0.1f || isUninstalling) {
        float glowIntensity = isUninstalling ? (0.25f + sinf(g_animTime * 4.0f) * 0.15f) : (0.3f * uninstallPress);
        for (int i = 8; i > 0; i--) {
            float alpha = (8 - i) / 8.0f * glowIntensity;
            Rectangle glowRect = {uninstallDrawRect.x - i*2, uninstallDrawRect.y - i*2,
                                  uninstallDrawRect.width + i*4, uninstallDrawRect.height + i*4};
            Color glowColor = isUninstalling ? COLOR_EMBER : COLOR_DISCONNECTED;
            DrawRectangleRounded(glowRect, BUTTON_RADIUS, 4, ColorWithAlpha(glowColor, alpha));
        }
    }

    DrawRectangleRounded(uninstallDrawRect, BUTTON_RADIUS, 4, uninstallBg);

    // Smoke/ash particle effect when uninstalling
    if (isUninstalling) {
        for (int p = 0; p < 5; p++) {
            float t = fmodf(g_animTime * 1.5f + p * 0.2f, 1.0f);
            float px = uninstallDrawRect.x + uninstallDrawRect.width * (0.2f + p * 0.15f);
            float py = uninstallDrawRect.y - t * 20;
            float alpha = (1.0f - t) * 0.4f;
            float size = 2 + t * 3;
            DrawCircle((int)px, (int)py, size, ColorWithAlpha(COLOR_TEXT_DIM, alpha));
        }
    } else if (uninstallHovered && uninstallPress < 0.1f) {
        DrawRectangleRoundedLines(uninstallDrawRect, BUTTON_RADIUS, 4, ColorWithAlpha(COLOR_DISCONNECTED, 0.8f));
    }

    const char *uninstallLabel = isUninstalling ? "REMOVING..." : "UNINSTALL";
    Vector2 uninstallSize = MeasureTextEx(g_font, uninstallLabel, 16, 1);
    DrawTextEx(g_font, uninstallLabel,
               (Vector2){uninstallDrawRect.x + (uninstallDrawRect.width - uninstallSize.x) / 2,
                         uninstallDrawRect.y + (uninstallDrawRect.height - 16) / 2},
               16, 1, uninstallTextColor);

    y += BUTTON_HEIGHT + 30;

    // Progress bar (opState already defined above)
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

    const char *instructions = "Drag to install/uninstall  |  Scroll: Mouse wheel  |  Tab: Switch section  |  R: Refresh";
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
    bool mouseInSidebar = (mouse.x < SIDEBAR_WIDTH && mouse.y > HEADER_HEIGHT && mouse.y < WINDOW_HEIGHT - FOOTER_HEIGHT);

    // Mouse wheel scrolling
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && mouseInSidebar) {
        float scrollAmount = 40.0f;
        g_targetScroll -= wheel * scrollAmount;

        // Clamp
        float maxScroll = g_totalContentHeight - SIDEBAR_CONTENT_HEIGHT;
        if (maxScroll < 0) maxScroll = 0;
        if (g_targetScroll < 0) g_targetScroll = 0;
        if (g_targetScroll > maxScroll) g_targetScroll = maxScroll;
    }

    // Keyboard navigation
    if (IsKeyPressed(KEY_TAB)) {
        int tries = 3;
        do {
            g_selection.section = (g_selection.section + 1) % 3;
            g_selection.index = 0;
            tries--;
        } while (GetSectionCount(plugins, g_selection.section) == 0 && tries > 0);
        ScrollToSelection(plugins);
    }

    int currentCount = GetSectionCount(plugins, g_selection.section);
    if (IsKeyPressed(KEY_DOWN) && currentCount > 0) {
        g_selection.index = (g_selection.index + 1) % currentCount;
        ScrollToSelection(plugins);
    }
    if (IsKeyPressed(KEY_UP) && currentCount > 0) {
        g_selection.index = (g_selection.index - 1 + currentCount) % currentCount;
        ScrollToSelection(plugins);
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
            g_installBtnPress = 1.0f;
            strncpy(g_lastOpPlugin, selectedPlugin->name, sizeof(g_lastOpPlugin) - 1);
            g_lastOpType = OP_INSTALLING;
            g_lastOpComplete = false;
            PluginBrowserInstall(selectedPlugin->name);
            g_needsRefresh = true;
        }
    }
    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && selectedPlugin && !isBusy) {
        if (selectedPlugin->remotePath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED) {
            g_uninstallBtnPress = 1.0f;
            strncpy(g_lastOpPlugin, selectedPlugin->name, sizeof(g_lastOpPlugin) - 1);
            g_lastOpType = OP_UNINSTALLING;
            g_lastOpComplete = false;
            PluginBrowserUninstall(selectedPlugin->name);
            g_needsRefresh = true;
        }
    }

    // Mouse click selection in sidebar
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && mouseInSidebar && !isBusy) {
        float adjustedMouseY = mouse.y - SIDEBAR_CONTENT_TOP + g_scrollOffset;

        for (int section = 0; section < 3; section++) {
            Rectangle sectionRect = g_sectionRects[section];
            if (adjustedMouseY >= sectionRect.y && adjustedMouseY < sectionRect.y + sectionRect.height) {
                // Click is in this section, find which item
                float itemY = sectionRect.y + 24;  // After section header
                int count = GetSectionCount(plugins, section);

                // Skip "(none)" text if empty
                if (count == 0) break;

                for (int i = 0; i < count; i++) {
                    float itemTop = itemY;
                    float itemBottom = itemY + SIDEBAR_ITEM_HEIGHT;
                    if (adjustedMouseY >= itemTop && adjustedMouseY < itemBottom) {
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

    // Mouse click on main panel buttons (use stored button rectangles from DrawMainPanel)
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && selectedPlugin && !isBusy && mouse.x >= PANEL_START_X) {
        if (CheckCollisionPointRec(mouse, g_installBtn)) {
            if (selectedPlugin->localPath[0] != '\0' && selectedPlugin->remotePath[0] == '\0' &&
                SshGetStatus() == SSH_STATUS_CONNECTED) {
                printf("Salamander: Installing %s (button click)\n", selectedPlugin->name);
                g_installBtnPress = 1.0f;  // Trigger press animation
                strncpy(g_lastOpPlugin, selectedPlugin->name, sizeof(g_lastOpPlugin) - 1);
                g_lastOpType = OP_INSTALLING;
                g_lastOpComplete = false;
                PluginBrowserInstall(selectedPlugin->name);
                g_needsRefresh = true;
            }
        } else if (CheckCollisionPointRec(mouse, g_uninstallBtn)) {
            if (selectedPlugin->remotePath[0] != '\0' && SshGetStatus() == SSH_STATUS_CONNECTED) {
                printf("Salamander: Uninstalling %s (button click)\n", selectedPlugin->name);
                g_uninstallBtnPress = 1.0f;  // Trigger press animation
                strncpy(g_lastOpPlugin, selectedPlugin->name, sizeof(g_lastOpPlugin) - 1);
                g_lastOpType = OP_UNINSTALLING;
                g_lastOpComplete = false;
                PluginBrowserUninstall(selectedPlugin->name);
                g_needsRefresh = true;
            }
        }
    }

    // Update drag state
    if (g_drag.isDragging) {
        g_drag.currentPos = mouse;
        g_drag.dragTime += GetFrameTime();

        float dragDist = sqrtf(powf(mouse.x - g_drag.startPos.x, 2) + powf(mouse.y - g_drag.startPos.y, 2));
        if (dragDist < 5 && g_drag.dragTime < 0.15f && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            g_drag.isDragging = false;
        }

        // Handle drop
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            if (mouseInSidebar && !isBusy && SshGetStatus() == SSH_STATUS_CONNECTED) {
                float adjustedMouseY = mouse.y - SIDEBAR_CONTENT_TOP + g_scrollOffset;

                for (int section = 0; section < 3; section++) {
                    Rectangle sectionRect = g_sectionRects[section];
                    if (adjustedMouseY >= sectionRect.y && adjustedMouseY < sectionRect.y + sectionRect.height) {
                        if (g_drag.sourceSection == SECTION_LOCAL_ONLY &&
                            (section == SECTION_DEVICE_ONLY || section == SECTION_SYNCED)) {
                            printf("Salamander: Installing %s (dragged to device)\n", g_drag.pluginName);
                            g_installBtnPress = 1.0f;
                            strncpy(g_lastOpPlugin, g_drag.pluginName, sizeof(g_lastOpPlugin) - 1);
                            g_lastOpType = OP_INSTALLING;
                            g_lastOpComplete = false;
                            PluginBrowserInstall(g_drag.pluginName);
                            g_needsRefresh = true;
                        } else if ((g_drag.sourceSection == SECTION_SYNCED || g_drag.sourceSection == SECTION_DEVICE_ONLY) &&
                                   section == SECTION_LOCAL_ONLY) {
                            printf("Salamander: Uninstalling %s (dragged to local)\n", g_drag.pluginName);
                            g_uninstallBtnPress = 1.0f;
                            strncpy(g_lastOpPlugin, g_drag.pluginName, sizeof(g_lastOpPlugin) - 1);
                            g_lastOpType = OP_UNINSTALLING;
                            g_lastOpComplete = false;
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

        // Update animations
        UpdateScroll(deltaTime);
        UpdateButtonAnimations(deltaTime);
        UpdateToast(deltaTime);

        // Check for operation completion and show toast
        const PluginOpState *opState = PluginBrowserGetOpState();
        if (!g_lastOpComplete && opState->complete && g_lastOpPlugin[0] != '\0') {
            g_lastOpComplete = true;
            g_lastOpSuccess = opState->success;
            ShowToast(g_lastOpPlugin, opState->success, g_lastOpType == OP_INSTALLING);
        }

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
        DrawToast();

        EndDrawing();
    }

    PluginBrowserShutdown();
    SshShutdown();
    UnloadAppFont();
    CloseWindow();

    return 0;
}
