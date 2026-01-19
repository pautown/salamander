#include "plugin_browser.h"
#include "ssh_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

// ============================================================================
// Plugin Browser Implementation
// ============================================================================

static char g_localPath[512] = "";
static PluginList g_plugins = {0};
static PluginOpState g_opState = {0};

// Convert filename to display name (nowplaying.so -> Now Playing)
static void MakeDisplayName(const char *filename, char *display, size_t maxLen) {
    // Remove .so extension
    char base[64];
    strncpy(base, filename, sizeof(base) - 1);
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    // Convert underscores to spaces and capitalize words
    size_t outIdx = 0;
    bool capitalizeNext = true;

    for (size_t i = 0; base[i] && outIdx < maxLen - 1; i++) {
        char c = base[i];
        if (c == '_') {
            display[outIdx++] = ' ';
            capitalizeNext = true;
        } else if (capitalizeNext && isalpha(c)) {
            display[outIdx++] = (char)toupper(c);
            capitalizeNext = false;
        } else {
            display[outIdx++] = c;
            capitalizeNext = false;
        }
    }
    display[outIdx] = '\0';
}

// Extract plugin name from path (removes directory and .so extension)
static void ExtractPluginName(const char *path, char *name, size_t maxLen) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    strncpy(name, base, maxLen - 1);
    name[maxLen - 1] = '\0';

    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
}

void PluginBrowserInit(const char *localPluginDir) {
    memset(&g_plugins, 0, sizeof(g_plugins));
    memset(&g_opState, 0, sizeof(g_opState));

    if (localPluginDir) {
        strncpy(g_localPath, localPluginDir, sizeof(g_localPath) - 1);
    }
}

void PluginBrowserShutdown(void) {
    memset(&g_plugins, 0, sizeof(g_plugins));
}

void PluginBrowserSetLocalPath(const char *path) {
    if (path) {
        strncpy(g_localPath, path, sizeof(g_localPath) - 1);
    }
}

const char *PluginBrowserGetLocalPath(void) {
    return g_localPath;
}

// Find or create plugin entry by name
static PluginInfo *FindOrCreatePlugin(const char *name) {
    // First try to find existing
    for (int i = 0; i < g_plugins.count; i++) {
        if (strcmp(g_plugins.plugins[i].name, name) == 0) {
            return &g_plugins.plugins[i];
        }
    }

    // Create new if space available
    if (g_plugins.count < MAX_PLUGINS) {
        PluginInfo *p = &g_plugins.plugins[g_plugins.count++];
        memset(p, 0, sizeof(*p));
        strncpy(p->name, name, sizeof(p->name) - 1);
        MakeDisplayName(name, p->displayName, sizeof(p->displayName));
        p->status = PLUGIN_LOCAL_ONLY;
        return p;
    }

    return NULL;
}

// Scan local directory for .so files
static void ScanLocalPlugins(void) {
    if (g_localPath[0] == '\0') {
        printf("Plugins: No local path configured\n");
        return;
    }

    printf("Plugins: Scanning local directory: %s\n", g_localPath);

    DIR *dir = opendir(g_localPath);
    if (!dir) {
        printf("Plugins: Cannot open local directory: %s\n", g_localPath);
        printf("Plugins: (You can specify a different path as command line argument)\n");
        return;
    }

    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Only process .so files
        size_t len = strlen(entry->d_name);
        if (len > 3 && strcmp(entry->d_name + len - 3, ".so") == 0) {
            char name[64];
            ExtractPluginName(entry->d_name, name, sizeof(name));

            PluginInfo *plugin = FindOrCreatePlugin(name);
            if (plugin) {
                snprintf(plugin->localPath, sizeof(plugin->localPath),
                         "%s/%s", g_localPath, entry->d_name);

                // Get file size
                struct stat st;
                if (stat(plugin->localPath, &st) == 0) {
                    plugin->localSize = st.st_size;
                }
                found++;
                printf("Plugins:   Found local: %s (%ld bytes)\n", name, plugin->localSize);
            }
        }
    }

    printf("Plugins: Found %d local plugins\n", found);
    closedir(dir);
}

// Scan remote device for plugins
static void ScanRemotePlugins(void) {
    if (SshGetStatus() != SSH_STATUS_CONNECTED) {
        printf("Plugins: Skipping remote scan (device not connected)\n");
        return;
    }

    printf("Plugins: Scanning device at %s...\n", SSH_PLUGIN_PATH);

    SshResult result = SshListDirectory(SSH_PLUGIN_PATH "/*.so");
    if (!result.success) {
        printf("Plugins: No plugins found on device (or error listing)\n");
        return;
    }

    // Parse output line by line
    char *line = strtok(result.output, "\n");
    while (line) {
        // Extract just the filename
        const char *basename = strrchr(line, '/');
        basename = basename ? basename + 1 : line;

        size_t len = strlen(basename);
        if (len > 3 && strcmp(basename + len - 3, ".so") == 0) {
            char name[64];
            ExtractPluginName(basename, name, sizeof(name));

            PluginInfo *plugin = FindOrCreatePlugin(name);
            if (plugin) {
                snprintf(plugin->remotePath, sizeof(plugin->remotePath),
                         "%s/%s.so", SSH_PLUGIN_PATH, name);

                // Get remote file size
                plugin->remoteSize = SshGetFileSize(plugin->remotePath);

                // Update status based on local presence
                if (plugin->localPath[0] != '\0') {
                    plugin->status = PLUGIN_INSTALLED;
                } else {
                    plugin->status = PLUGIN_DEVICE_ONLY;
                }
            }
        }

        line = strtok(NULL, "\n");
    }
}

void PluginBrowserRefresh(void) {
    g_opState.operation = OP_REFRESHING;
    g_opState.progress = 0.0f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Scanning local plugins...");

    // Reset plugin list
    g_plugins.count = 0;

    // Scan local
    ScanLocalPlugins();

    g_opState.progress = 0.5f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Scanning device plugins...");

    // Scan remote
    ScanRemotePlugins();

    g_opState.operation = OP_NONE;
    g_opState.complete = true;
    g_opState.success = true;
    g_opState.progress = 1.0f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Found %d plugins", g_plugins.count);
}

const PluginList *PluginBrowserGetList(void) {
    return &g_plugins;
}

const PluginInfo *PluginBrowserGetPlugin(int index) {
    if (index < 0 || index >= g_plugins.count) return NULL;
    return &g_plugins.plugins[index];
}

const PluginInfo *PluginBrowserFindPlugin(const char *name) {
    for (int i = 0; i < g_plugins.count; i++) {
        if (strcmp(g_plugins.plugins[i].name, name) == 0) {
            return &g_plugins.plugins[i];
        }
    }
    return NULL;
}

// Progress callback for SCP
static void InstallProgressCallback(float progress, const char *message, void *userData) {
    (void)userData;
    g_opState.progress = progress;
    snprintf(g_opState.message, sizeof(g_opState.message), "%s", message);
}

bool PluginBrowserInstall(const char *pluginName) {
    const PluginInfo *plugin = PluginBrowserFindPlugin(pluginName);
    if (!plugin || plugin->localPath[0] == '\0') {
        snprintf(g_opState.message, sizeof(g_opState.message), "Plugin not found locally");
        return false;
    }

    if (SshGetStatus() != SSH_STATUS_CONNECTED) {
        snprintf(g_opState.message, sizeof(g_opState.message), "Device not connected");
        return false;
    }

    g_opState.operation = OP_INSTALLING;
    g_opState.progress = 0.0f;
    g_opState.complete = false;
    strncpy(g_opState.pluginName, pluginName, sizeof(g_opState.pluginName) - 1);
    snprintf(g_opState.message, sizeof(g_opState.message), "Installing %s...", pluginName);

    // Step 1: Enable read-write mode
    printf("Install: Enabling read-write mode...\n");
    g_opState.progress = 0.1f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Enabling write mode...");
    SshResult rwResult = SshExecute("mount -o remount,rw /");
    if (!rwResult.success) {
        printf("Install: Warning - could not remount rw: %s\n", rwResult.output);
    }

    // Step 2: Ensure remote directory exists
    printf("Install: Creating plugin directory...\n");
    g_opState.progress = 0.2f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Creating directory...");
    SshExecute("mkdir -p " SSH_PLUGIN_PATH);

    // Step 3: Build remote path and copy file
    char remotePath[512];
    snprintf(remotePath, sizeof(remotePath), "%s/%s.so", SSH_PLUGIN_PATH, pluginName);

    printf("Install: Copying %s to %s...\n", plugin->localPath, remotePath);
    bool success = SshCopyToDevice(plugin->localPath, remotePath,
                                   InstallProgressCallback, NULL);

    // Step 4: Sync filesystem
    if (success) {
        printf("Install: Syncing filesystem...\n");
        g_opState.progress = 0.95f;
        snprintf(g_opState.message, sizeof(g_opState.message), "Syncing...");
        SshExecute("sync");
    }

    g_opState.complete = true;
    g_opState.success = success;
    g_opState.operation = OP_NONE;

    if (success) {
        printf("Install: Successfully installed %s\n", pluginName);
        snprintf(g_opState.message, sizeof(g_opState.message), "Installed %s", pluginName);
    } else {
        printf("Install: Failed to install %s\n", pluginName);
    }

    return success;
}

bool PluginBrowserUninstall(const char *pluginName) {
    const PluginInfo *plugin = PluginBrowserFindPlugin(pluginName);
    if (!plugin || plugin->remotePath[0] == '\0') {
        snprintf(g_opState.message, sizeof(g_opState.message), "Plugin not installed on device");
        return false;
    }

    if (SshGetStatus() != SSH_STATUS_CONNECTED) {
        snprintf(g_opState.message, sizeof(g_opState.message), "Device not connected");
        return false;
    }

    g_opState.operation = OP_UNINSTALLING;
    g_opState.progress = 0.0f;
    g_opState.complete = false;
    strncpy(g_opState.pluginName, pluginName, sizeof(g_opState.pluginName) - 1);
    snprintf(g_opState.message, sizeof(g_opState.message), "Uninstalling %s...", pluginName);

    // Step 1: Enable read-write mode
    printf("Uninstall: Enabling read-write mode...\n");
    g_opState.progress = 0.1f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Enabling write mode...");
    SshResult rwResult = SshExecute("mount -o remount,rw /");
    if (!rwResult.success) {
        printf("Uninstall: Warning - could not remount rw: %s\n", rwResult.output);
    }

    // Step 2: Stop llizardgui service if running (to release any file handles)
    printf("Uninstall: Stopping llizardgui service...\n");
    g_opState.progress = 0.2f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Stopping service...");
    SshExecute("sv stop llizardgui 2>/dev/null || true");
    // Also try killing the process directly in case it's not running as a service
    SshExecute("pkill -f llizardgui-host 2>/dev/null || true");

    // Step 3: Delete the plugin file
    printf("Uninstall: Deleting plugin file: %s\n", plugin->remotePath);
    g_opState.progress = 0.4f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Deleting plugin...");

    char deleteCmd[1024];
    snprintf(deleteCmd, sizeof(deleteCmd), "rm -f '%s'", plugin->remotePath);
    SshResult deleteResult = SshExecute(deleteCmd);
    printf("Uninstall: Delete result: success=%d, output=%s\n", deleteResult.success, deleteResult.output);

    // Step 4: Delete any related config/data files for this plugin
    printf("Uninstall: Cleaning up related files for %s...\n", pluginName);
    g_opState.progress = 0.6f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Cleaning up...");

    // Delete plugin config directory if it exists
    char cleanupCmd[1024];
    snprintf(cleanupCmd, sizeof(cleanupCmd),
             "rm -rf /var/lib/llizard/plugins/%s 2>/dev/null || true; "
             "rm -rf /tmp/llizard/%s 2>/dev/null || true; "
             "rm -f /etc/llizard/plugins/%s.conf 2>/dev/null || true",
             pluginName, pluginName, pluginName);
    SshExecute(cleanupCmd);

    // Step 5: Sync filesystem
    printf("Uninstall: Syncing filesystem...\n");
    g_opState.progress = 0.8f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Syncing...");
    SshExecute("sync");

    // Step 6: Restart llizardgui service
    printf("Uninstall: Restarting llizardgui service...\n");
    g_opState.progress = 0.9f;
    snprintf(g_opState.message, sizeof(g_opState.message), "Restarting service...");
    SshExecute("sv start llizardgui 2>/dev/null || true");

    // Verify the file is actually deleted
    bool success = !SshFileExists(plugin->remotePath);

    g_opState.complete = true;
    g_opState.success = success;
    g_opState.progress = 1.0f;
    g_opState.operation = OP_NONE;

    if (success) {
        printf("Uninstall: Successfully uninstalled %s\n", pluginName);
        snprintf(g_opState.message, sizeof(g_opState.message), "Uninstalled %s", pluginName);
    } else {
        printf("Uninstall: Failed to uninstall %s (file may still exist)\n", pluginName);
        snprintf(g_opState.message, sizeof(g_opState.message), "Failed to uninstall %s", pluginName);
    }

    return success;
}

const PluginOpState *PluginBrowserGetOpState(void) {
    return &g_opState;
}

bool PluginBrowserIsBusy(void) {
    return g_opState.operation != OP_NONE;
}

void FormatFileSize(long bytes, char *buffer, size_t bufSize) {
    if (bytes < 0) {
        snprintf(buffer, bufSize, "Unknown");
    } else if (bytes < 1024) {
        snprintf(buffer, bufSize, "%ld B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, bufSize, "%.1f KB", bytes / 1024.0f);
    } else {
        snprintf(buffer, bufSize, "%.1f MB", bytes / (1024.0f * 1024.0f));
    }
}
