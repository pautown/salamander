#include "ssh_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// ============================================================================
// SSH Manager Implementation using sshpass + popen
// ============================================================================

static char g_host[256] = SSH_DEFAULT_HOST;
static char g_user[64] = SSH_DEFAULT_USER;
static char g_pass[64] = SSH_DEFAULT_PASS;
static SshConnectionStatus g_status = SSH_STATUS_UNKNOWN;

// SSH options for faster connections (no host key checking, short timeouts)
#define SSH_OPTS "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=3 -o BatchMode=no"

void SshInit(const char *host, const char *user, const char *password) {
    if (host) strncpy(g_host, host, sizeof(g_host) - 1);
    if (user) strncpy(g_user, user, sizeof(g_user) - 1);
    if (password) strncpy(g_pass, password, sizeof(g_pass) - 1);
    g_status = SSH_STATUS_UNKNOWN;

    printf("SSH: Initialized connection settings:\n");
    printf("SSH:   Host: %s\n", g_host);
    printf("SSH:   User: %s\n", g_user);
    printf("SSH:   Pass: %s\n", g_pass);
}

void SshShutdown(void) {
    g_status = SSH_STATUS_UNKNOWN;
}

const char *SshGetHost(void) {
    return g_host;
}

const char *SshGetUser(void) {
    return g_user;
}

SshConnectionStatus SshGetStatus(void) {
    return g_status;
}

// Build sshpass command prefix
static void BuildSshpassPrefix(char *buffer, size_t bufSize) {
    snprintf(buffer, bufSize, "sshpass -p '%s' ssh %s %s@%s",
             g_pass, SSH_OPTS, g_user, g_host);
}

// Build scp command prefix
static void BuildScpPrefix(char *buffer, size_t bufSize) {
    snprintf(buffer, bufSize, "sshpass -p '%s' scp %s",
             g_pass, SSH_OPTS);
}

void SshCheckConnection(void) {
    g_status = SSH_STATUS_CHECKING;

    printf("SSH: Checking connection to %s@%s...\n", g_user, g_host);

    // Quick ping-style check using ssh echo
    char cmd[512];
    char sshPrefix[256];
    BuildSshpassPrefix(sshPrefix, sizeof(sshPrefix));
    snprintf(cmd, sizeof(cmd), "%s 'echo ok' 2>/dev/null", sshPrefix);

    printf("SSH: Running: sshpass -p '%s' ssh %s %s@%s 'echo ok'\n", g_pass, SSH_OPTS, g_user, g_host);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        printf("SSH: Failed to execute ssh command\n");
        g_status = SSH_STATUS_DISCONNECTED;
        return;
    }

    char output[64] = {0};
    if (fgets(output, sizeof(output), fp) != NULL) {
        if (strstr(output, "ok") != NULL) {
            printf("SSH: Connection successful!\n");
            g_status = SSH_STATUS_CONNECTED;
        } else {
            printf("SSH: Unexpected response: %s\n", output);
            g_status = SSH_STATUS_DISCONNECTED;
        }
    } else {
        printf("SSH: No response (device may be offline or unreachable)\n");
        g_status = SSH_STATUS_DISCONNECTED;
    }

    pclose(fp);
}

SshResult SshExecute(const char *command) {
    SshResult result = {0};
    result.success = false;
    result.exitCode = -1;

    char cmd[2048];
    char sshPrefix[256];
    BuildSshpassPrefix(sshPrefix, sizeof(sshPrefix));
    snprintf(cmd, sizeof(cmd), "%s '%s' 2>&1", sshPrefix, command);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(result.output, sizeof(result.output), "Failed to execute command");
        return result;
    }

    size_t totalRead = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t len = strlen(buffer);
        if (totalRead + len < sizeof(result.output) - 1) {
            strcpy(result.output + totalRead, buffer);
            totalRead += len;
        }
    }

    int status = pclose(fp);
    result.exitCode = WEXITSTATUS(status);
    result.success = (result.exitCode == 0);

    return result;
}

SshResult SshListDirectory(const char *remotePath) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ls -1 %s 2>/dev/null", remotePath);
    return SshExecute(cmd);
}

bool SshCopyToDevice(const char *localPath, const char *remotePath,
                     SshProgressCallback progressCb, void *userData) {
    // Check if local file exists
    if (access(localPath, R_OK) != 0) {
        if (progressCb) progressCb(0.0f, "Local file not found", userData);
        return false;
    }

    if (progressCb) progressCb(0.1f, "Starting transfer...", userData);

    char cmd[2048];
    char scpPrefix[256];
    BuildScpPrefix(scpPrefix, sizeof(scpPrefix));
    snprintf(cmd, sizeof(cmd), "%s '%s' %s@%s:'%s' 2>&1",
             scpPrefix, localPath, g_user, g_host, remotePath);

    if (progressCb) progressCb(0.3f, "Transferring...", userData);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        if (progressCb) progressCb(0.0f, "Transfer failed", userData);
        return false;
    }

    // Read any output (errors)
    char output[1024] = {0};
    size_t totalRead = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t len = strlen(buffer);
        if (totalRead + len < sizeof(output) - 1) {
            strcpy(output + totalRead, buffer);
            totalRead += len;
        }
    }

    if (progressCb) progressCb(0.9f, "Finishing...", userData);

    int status = pclose(fp);
    int exitCode = WEXITSTATUS(status);

    if (exitCode == 0) {
        if (progressCb) progressCb(1.0f, "Complete", userData);
        return true;
    } else {
        if (progressCb) progressCb(0.0f, output[0] ? output : "Transfer failed", userData);
        return false;
    }
}

bool SshDeleteFile(const char *remotePath) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -f '%s'", remotePath);
    SshResult result = SshExecute(cmd);
    return result.success;
}

long SshGetFileSize(const char *remotePath) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "stat -c %%s '%s' 2>/dev/null || echo -1", remotePath);
    SshResult result = SshExecute(cmd);

    if (!result.success) return -1;

    long size = atol(result.output);
    return size;
}

bool SshFileExists(const char *remotePath) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "test -f '%s' && echo exists", remotePath);
    SshResult result = SshExecute(cmd);
    return result.success && strstr(result.output, "exists") != NULL;
}
