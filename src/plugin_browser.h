#ifndef PLUGIN_BROWSER_H
#define PLUGIN_BROWSER_H

#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// Plugin Browser - Local and remote plugin discovery and management
// ============================================================================

// Maximum number of plugins to track
#define MAX_PLUGINS 64

// Plugin installation status
typedef enum {
    PLUGIN_LOCAL_ONLY,      // Only on local machine
    PLUGIN_INSTALLED,       // On both local and device
    PLUGIN_DEVICE_ONLY      // Only on device (unknown plugin)
} PluginStatus;

// Plugin info structure
typedef struct {
    char name[64];          // Plugin name (without .so extension)
    char displayName[64];   // Human-readable name
    char localPath[512];    // Full path on local machine (empty if not local)
    char remotePath[512];   // Full path on device (empty if not on device)
    long localSize;         // File size in bytes (local)
    long remoteSize;        // File size in bytes (device)
    PluginStatus status;    // Installation status
} PluginInfo;

// Plugin list
typedef struct {
    PluginInfo plugins[MAX_PLUGINS];
    int count;
} PluginList;

// Operation state for async operations
typedef enum {
    OP_NONE,
    OP_INSTALLING,
    OP_UNINSTALLING,
    OP_REFRESHING
} PluginOperation;

typedef struct {
    PluginOperation operation;
    char pluginName[64];
    float progress;
    char message[256];
    bool complete;
    bool success;
} PluginOpState;

// Initialize plugin browser
// localPluginDir: path to local plugins (e.g., "../build-armv7-drm/")
void PluginBrowserInit(const char *localPluginDir);

// Shutdown and cleanup
void PluginBrowserShutdown(void);

// Set local plugin directory
void PluginBrowserSetLocalPath(const char *path);

// Get local plugin directory
const char *PluginBrowserGetLocalPath(void);

// Refresh plugin lists (scans local and remote)
// This is synchronous and may take time
void PluginBrowserRefresh(void);

// Get merged plugin list
const PluginList *PluginBrowserGetList(void);

// Get plugin at index
const PluginInfo *PluginBrowserGetPlugin(int index);

// Find plugin by name
const PluginInfo *PluginBrowserFindPlugin(const char *name);

// Install a plugin to device
// Returns true if operation started successfully
bool PluginBrowserInstall(const char *pluginName);

// Uninstall a plugin from device
bool PluginBrowserUninstall(const char *pluginName);

// Get current operation state
const PluginOpState *PluginBrowserGetOpState(void);

// Check if an operation is in progress
bool PluginBrowserIsBusy(void);

// Format file size as human-readable string
void FormatFileSize(long bytes, char *buffer, size_t bufSize);

#endif // PLUGIN_BROWSER_H
