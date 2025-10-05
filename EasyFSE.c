#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shlobj_core.h>

#define MAX_PATH 260

// Configuration
struct Config {
    const char* vivetool_url;
    const char* physpanel_url;
    const char* feature_ids;
} config = {
    "https://github.com/thebookisclosed/ViVe/releases/download/v0.3.4/ViVeTool-v0.3.4-IntelAmd.zip",
    "https://github.com/riverar/physpanel/releases/download/v0.1.0/physpanel_0.1.0_x86_64-pc-windows-msvc.zip",
    "52580392,50902630"
};

volatile int progress_active = 0;

// Progress bar animation thread
DWORD WINAPI show_progress_bar(LPVOID lpParam) {
    const char* label = (const char*)lpParam;
    const char* states[] = {"[          ]", "[=         ]", "[==        ]", "[===       ]", "[====      ]",
                            "[=====     ]", "[======    ]", "[=======   ]", "[========  ]", "[========= ]"};
    int step = 0;
    printf("\n%s: %s", label, states[0]);
    while (progress_active) {
        Sleep(200);  // Faster updates
        step = (step + 1) % 10;
        printf("\r%s: %s", label, states[step]);
        fflush(stdout);
    }
    printf("\r%s: [==========] Done!\n", label);
    return 0;
}

// Check if running as admin
int is_admin() {
    BOOL is_elevated = FALSE;
    HANDLE token = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            is_elevated = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return is_elevated;
}

// Get Documents folder path
char* get_documents_path() {
    static char path[MAX_PATH];
    PWSTR wpath;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Documents, 0, NULL, &wpath))) {
        WideCharToMultiByte(CP_ACP, 0, wpath, -1, path, MAX_PATH, NULL, NULL);
        CoTaskMemFree(wpath);
    } else {
        strcpy(path, "C:\\Users\\%USERNAME%\\Documents");
    }
    return path;
}

// Build path safely
void build_path(char* dest, size_t dest_size, const char* base, const char* subpath) {
    if (strlen(base) + strlen(subpath) + 2 > dest_size) {
        fprintf(stderr, "Path too long: %s\\%s\n", base, subpath);
        dest[0] = '\0';
        return;
    }
    snprintf(dest, dest_size, "%s\\%s", base, subpath);
}

// Create nested directories
int create_nested_dir(const char* path) {
    char temp[MAX_PATH];
    strncpy(temp, path, MAX_PATH);
    char* p = temp;
    if (p[1] == ':') p += 3;  // Skip drive letter (e.g., "C:\")
    while ((p = strchr(p, '\\')) != NULL) {
        *p = '\0';
        CreateDirectoryA(temp, NULL);
        *p++ = '\\';
    }
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// Execute PowerShell command with progress bar
int execute_powershell(const char* cmd, const char* label) {
    HANDLE thread = CreateThread(NULL, 0, show_progress_bar, (LPVOID)label, 0, NULL);
    if (!thread) {
        printf("Failed to create progress thread for %s!\n", label);
        return 0;
    }
    progress_active = 1;
    int result = system(cmd);
    progress_active = 0;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    if (result != 0) {
        printf("%s failed! Error code: %d\n", label, result);
        return 0;
    }
    return 1;
}

// Download file
int download_file(const char* url, const char* local_path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "powershell -Command \"Invoke-WebRequest -Uri '%s' -OutFile '%s'\"",
             url, local_path);
    return execute_powershell(cmd, "Downloading");
}

// Unzip file
int unzip_file(const char* zip_path, const char* dest_dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"",
             zip_path, dest_dir);
    return execute_powershell(cmd, "Unzipping");
}

// Check if file exists
int file_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

// Enable features with ViVeTool
int enable_features(const char* exe_path) {
    printf("Enabling features...\n");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "\"%s\" /enable /id:%s", exe_path, config.feature_ids);
    int result = system(cmd);
    if (result != 0) {
        printf("Failed to enable features! Error code: %d\n", result);
        return 0;
    }
    printf("Hidden features enabled successfully!\n");
    return 1;
}

// Add DeviceForm registry value
int add_device_form() {
    HKEY hKey;
    const char* reg_path = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OEM";
    DWORD device_form_value = 0x2E;

    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, reg_path, 0, KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS) {
        printf("Failed to open registry key! Error code: %ld\n", result);
        return 0;
    }

    result = RegSetValueExA(hKey, "DeviceForm", 0, REG_DWORD, (const BYTE*)&device_form_value, sizeof(device_form_value));
    if (result != ERROR_SUCCESS) {
        printf("Failed to set DeviceForm value! Error code: %ld\n", result);
        RegCloseKey(hKey);
        return 0;
    }

    printf("DeviceForm added & set to 0x2E successfully!\n");
    RegCloseKey(hKey);
    return 1;
}

// Set panel dimensions with physpanel
int set_panel() {
    char* docs_path = get_documents_path();
    char fse_path[MAX_PATH], zip_path[MAX_PATH], dir_path[MAX_PATH], exe_path[MAX_PATH];
    build_path(fse_path, MAX_PATH, docs_path, "FSE");
    build_path(zip_path, MAX_PATH, fse_path, "physpanel_0.1.0_x86_64-pc-windows-msvc.zip");
    build_path(dir_path, MAX_PATH, fse_path, "physpanel");
    build_path(exe_path, MAX_PATH, dir_path, "physpanel.exe");

    if (!create_nested_dir(fse_path) || !create_nested_dir(dir_path)) {
        printf("Failed to create directories!\n");
        return 0;
    }

    if (!file_exists(exe_path)) {
        printf("Downloading Physpanel...\n");
        if (!download_file(config.physpanel_url, zip_path)) return 0;
        printf("Unzipping Physpanel...\n");
        if (!unzip_file(zip_path, dir_path)) return 0;
        DeleteFileA(zip_path);
    } else {
        printf("Physpanel already exists. Skipping download.\n");
    }

    printf("Setting panel dimensions...\n");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "schtasks /create /tn \"SetPanelDimensions\" /tr \"\\\"%s\\\" set 155 87\" /sc onstart /ru SYSTEM /rl highest /f", dir_path);
    return execute_powershell(cmd, "Panel dimensions set to 155x87 successfully! ");
}

// Show menu
void show_menu() {
    printf("\n  ==== Easy FSE ====\n\n");
    printf("   made by: kitten\n\n");
    printf("  ==================\n\n");
    printf("  ---------------------------------------------------------------------\n");
    printf("  This tool enables Full Screen Experience on PC's & Laptop's.\n\n");
    printf("  WARNING! Windows 11 25H2 (OS Build 26200.6725) or better is required!\n");
    printf("  ---------------------------------------------------------------------\n\n");
    printf("  1. Enable Hidden Features\n");
    printf("  2. Add DeviceForm\n");
    printf("  3. Set Panel\n\n");
    printf("  Reboot for changes to take effect.\n");
    printf("  -----------------------------------------\n\n");
    printf("  4. Reboot\n");
    printf("  0. Exit\n\n");
    printf("  Choice: ");
}

int main() {
    if (!is_admin()) {
        printf("Error: This app must be run as Administrator.\n");
        printf("Right-click the executable and select 'Run as administrator'.\n");
        system("pause");
        return 1;
    }

    while (1) {
        system("cls");
        show_menu();
        char choice;
        scanf(" %c", &choice);
        switch (choice) {
            case '0':
                printf("Exiting.\n");
                return 0;
            case '4':
                printf("Rebooting system...\n");
                system("shutdown /r /t 0");
                return 0;
            case '1': {
                char* docs_path = get_documents_path();
                char fse_path[MAX_PATH], zip_path[MAX_PATH], dir_path[MAX_PATH], exe_path[MAX_PATH];
                build_path(fse_path, MAX_PATH, docs_path, "FSE");
                build_path(zip_path, MAX_PATH, fse_path, "ViVeTool-v0.3.4-IntelAmd.zip");
                build_path(dir_path, MAX_PATH, fse_path, "ViVeTool");
                build_path(exe_path, MAX_PATH, dir_path, "ViVeTool.exe");

                if (!create_nested_dir(fse_path) || !create_nested_dir(dir_path)) {
                    printf("Failed to create directories!\n");
                    system("pause");
                    break;
                }

                if (!file_exists(exe_path)) {
                    printf("Downloading ViVeTool...\n");
                    if (!download_file(config.vivetool_url, zip_path)) {
                        system("pause");
                        break;
                    }
                    printf("Unzipping ViVeTool...\n");
                    if (!unzip_file(zip_path, dir_path)) {
                        system("pause");
                        break;
                    }
                    DeleteFileA(zip_path);
                } else {
                    printf("ViVeTool already exists. Skipping download.\n");
                }

                if (!enable_features(exe_path)) system("pause");
                break;
            }
            case '2':
                if (!add_device_form()) system("pause");
                break;
            case '3':
                if (!set_panel()) system("pause");
                break;
            default:
                printf("Invalid choice. Please select 1, 2, 3, 4, or 0.\n");
                system("pause");
                break;
        }
        Sleep(3000);  // Delay before next iteration
    }
    return 0;
}