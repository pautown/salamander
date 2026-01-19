#ifndef SALAMANDER_THEME_H
#define SALAMANDER_THEME_H

#include "raylib.h"

// ============================================================================
// Fire Salamander Color Palette
// ============================================================================

// Primary fire colors
static const Color COLOR_FIRE_DEEP     = {229, 66, 11, 255};    // #E5420B - Primary accent
static const Color COLOR_EMBER         = {245, 166, 35, 255};   // #F5A623 - Warm glow
static const Color COLOR_GOLD          = {255, 200, 80, 255};   // #FFC850 - Highlights
static const Color COLOR_FLAME_ORANGE  = {255, 120, 50, 255};   // Flame midtone

// Background colors
static const Color COLOR_CHARCOAL_DARK = {26, 26, 26, 255};     // #1A1A1A - Main background
static const Color COLOR_WARM_GRAY     = {42, 36, 38, 255};     // #2A2426 - Cards
static const Color COLOR_ASH           = {54, 48, 50, 255};     // Lighter cards

// Text colors
static const Color COLOR_TEXT_BRIGHT   = {255, 250, 245, 255};  // Near white
static const Color COLOR_TEXT_WARM     = {220, 200, 180, 255};  // Warm secondary
static const Color COLOR_TEXT_DIM      = {140, 130, 125, 255};  // Dimmed text

// Status colors
static const Color COLOR_CONNECTED     = {100, 220, 100, 255};  // Green for connected
static const Color COLOR_DISCONNECTED  = {220, 80, 80, 255};    // Red for disconnected
static const Color COLOR_INSTALLING    = {100, 180, 255, 255};  // Blue for progress

// Selection and interaction
static const Color COLOR_CARD_SELECTED = {58, 48, 52, 255};     // Selected card background
static const Color COLOR_CARD_HOVER    = {48, 42, 44, 255};     // Hover state

// ============================================================================
// Layout Constants
// ============================================================================

// Window
#define WINDOW_WIDTH  900
#define WINDOW_HEIGHT 600
#define TARGET_FPS    60

// Header
#define HEADER_HEIGHT 56
#define HEADER_PADDING 20

// Sidebar
#define SIDEBAR_WIDTH 240
#define SIDEBAR_PADDING 16
#define SIDEBAR_ITEM_HEIGHT 40
#define SIDEBAR_ITEM_SPACING 4
#define SIDEBAR_SECTION_SPACING 24

// Main panel
#define PANEL_PADDING 24
#define PANEL_START_X (SIDEBAR_WIDTH + 1)

// Buttons
#define BUTTON_WIDTH 140
#define BUTTON_HEIGHT 44
#define BUTTON_SPACING 16
#define BUTTON_RADIUS 0.2f

// Progress bar
#define PROGRESS_HEIGHT 24
#define PROGRESS_RADIUS 0.3f

// Footer
#define FOOTER_HEIGHT 40

// ============================================================================
// Animation Constants
// ============================================================================

#define SCROLL_SPEED       12.0f
#define PULSE_SPEED        2.0f
#define GLOW_SPEED         1.5f
#define SELECTION_SPEED    8.0f

// Ember glow animation
#define EMBER_GLOW_MIN     0.3f
#define EMBER_GLOW_MAX     0.8f

// ============================================================================
// Helper Macros
// ============================================================================

// Lerp for smooth animations
#define LERP(a, b, t) ((a) + ((b) - (a)) * (t))

// Clamp value to range
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

// Color with modified alpha
static inline Color ColorWithAlpha(Color c, float alpha) {
    return (Color){c.r, c.g, c.b, (unsigned char)(255 * CLAMP(alpha, 0.0f, 1.0f))};
}

#endif // SALAMANDER_THEME_H
