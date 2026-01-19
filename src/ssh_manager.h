#ifndef SSH_MANAGER_H
#define SSH_MANAGER_H

#include <stdbool.h>

// ============================================================================
// SSH Manager - CarThing SSH/SCP operations via sshpass + popen
// ============================================================================

// Default CarThing connection settings
#define SSH_DEFAULT_HOST "172.16.42.2"
#define SSH_DEFAULT_USER "root"
#define SSH_DEFAULT_PASS "llizardos"
#define SSH_PLUGIN_PATH  "/tmp/plugins"

// Connection status
typedef enum {
    SSH_STATUS_UNKNOWN,
    SSH_STATUS_CONNECTED,
    SSH_STATUS_DISCONNECTED,
    SSH_STATUS_CHECKING
} SshConnectionStatus;

// Operation result
typedef struct {
    bool success;
    char output[4096];
    int exitCode;
} SshResult;

// Progress callback for file transfers
typedef void (*SshProgressCallback)(float progress, const char *message, void *userData);

// Initialize SSH manager with connection settings
void SshInit(const char *host, const char *user, const char *password);

// Shutdown and cleanup
void SshShutdown(void);

// Get current connection settings
const char *SshGetHost(void);
const char *SshGetUser(void);

// Check connection status (fast ping test)
// This is non-blocking and updates internal state
void SshCheckConnection(void);

// Get cached connection status
SshConnectionStatus SshGetStatus(void);

// Execute a remote command
// Returns result with output and exit code
SshResult SshExecute(const char *command);

// List files in a remote directory
// Returns result with newline-separated file paths
SshResult SshListDirectory(const char *remotePath);

// Copy a file to the device
// progressCb is optional, can be NULL
bool SshCopyToDevice(const char *localPath, const char *remotePath,
                     SshProgressCallback progressCb, void *userData);

// Delete a file on the device
bool SshDeleteFile(const char *remotePath);

// Get file size on device (returns -1 on error)
long SshGetFileSize(const char *remotePath);

// Check if a file exists on device
bool SshFileExists(const char *remotePath);

#endif // SSH_MANAGER_H
