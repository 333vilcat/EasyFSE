#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shlobj.h>  // For SHGetFolderPath

#define VIVETOOL_URL "https://github.com/thebookisclosed/ViVe/releases/download/v0.3.4/ViVeTool-v0.3.4-IntelAmd.zip"
#define PHYSPANEL_URL "https://github.com/riverar/physpanel/releases/download/v0.1.0/physpanel_0.1.0_x86_64-pc-windows-msvc.zip"
#define VIVETOOL_ZIP_PATH "%UserProfile%\\Documents\\FSE\\ViVeTool-v0.3.4-IntelAmd.zip"
#define PHYSPANEL_ZIP_PATH "%UserProfile%\\Documents\\FSE\\physpanel_0.1.0_x86_64-pc-windows-msvc.zip"
#define VIVETOOL_DIR "%UserProfile%\\Documents\\FSE\\ViVeTool"
#define PHYSPANEL_DIR "%UserProfile%\\Documents\\FSE\\physpanel"
#define VIVETOOL_EXE "%UserProfile%\\Documents\\FSE\\ViVeTool\\ViVeTool.exe"
#define PHYSPANEL_EXE "%UserProfile%\\Documents\\FSE\\physpanel\\physpanel.exe"
#define FEATURE_IDS "52580392,50902630"

volatile int progress_active = 0;  // Flag to control progress bar thread

// Progress bar animation thread
DWORD WINAPI show_progress_bar(LPVOID lpParam) {
    const char* label = (const char*)lpParam;
    const char* states[] = {"[          ]", "[=         ]", "[==        ]", "[===       ]", "[====      ]",
                            "[=====     ]", "[======    ]", "[=======   ]", "[========  ]", "[========= ]"};
    int max_steps = 10;
    int step = 0;

    printf("\n%s: %s", label, states[0]);
    while (progress_active) {
        Sleep(500);  // Update every 500ms
        step = (step + 1) % max_steps;
        printf("\r%s: %s", label, states[step]);  // Overwrite line
        fflush(stdout);
    }
    printf("\r%s: [==========] Done!\n", label);  // Final state
    return 0;
}

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

char* get_documents_path() {
    char* path = malloc(MAX_PATH);
    if (SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, path) != S_OK) {
        strcpy(path, "C:\\Users\\%USERNAME%\\Documents");  // Fallback
    }
    return path;
}

int download_file(const char* url, const char* local_path) {
    HANDLE thread = NULL;
    progress_active = 1;
    thread = CreateThread(NULL, 0, show_progress_bar, (LPVOID)"Downloading", 0, NULL);
    if (!thread) {
        printf("Failed to create progress thread!\n");
        progress_active = 0;
        return 0;
    }

    char cmd[512];
    sprintf(cmd, "powershell -Command \"Invoke-WebRequest -Uri '%s' -OutFile '%s'\"", url, local_path);
    int result = system(cmd);
    progress_active = 0;  // Stop progress bar
    WaitForSingleObject(thread, INFINITE);  // Wait for thread to finish
    CloseHandle(thread);

    if (result != 0) {
        printf("Download failed! Error code: %d\n", result);
        return 0;
    }
    return 1;
}

int unzip_file(const char* zip_path, const char* dest_dir) {
    HANDLE thread = NULL;
    progress_active = 1;
    thread = CreateThread(NULL, 0, show_progress_bar, (LPVOID)"Unzipping", 0, NULL);
    if (!thread) {
        printf("Failed to create progress thread!\n");
        progress_active = 0;
        return 0;
    }

    char cmd[512];
    sprintf(cmd, "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"", zip_path, dest_dir);
    int result = system(cmd);
    progress_active = 0;  // Stop progress bar
    WaitForSingleObject(thread, INFINITE);  // Wait for thread to finish
    CloseHandle(thread);

    if (result != 0) {
        printf("Unzip failed! Error code: %d\n", result);
        return 0;
    }
    return 1;
}

int vivetool_exists(const char* exe_path) {
    DWORD attr = GetFileAttributesA(exe_path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

int physpanel_exists(const char* exe_path) {
    DWORD attr = GetFileAttributesA(exe_path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

int enable_features(const char* exe_path) {
    printf("Enabling features...\n");
    char cmd[512];
    sprintf(cmd, "\"%s\" /enable /id:%s", exe_path, FEATURE_IDS);
    int result = system(cmd);
    if (result != 0) {
        printf("Failed to enable features! Error code: %d\n", result);
        return 0;
    }
    printf("Hidden features enabled successfully!\n");
    return 1;
}

int add_device_form() {
    HKEY hKey;
    const char* reg_path = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OEM";
    DWORD device_form_value = 0x2E;  // Hexadecimal 2E (46 in decimal)

    // Open the registry key
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, reg_path, 0, KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS) {
        printf("Failed to open registry key! Error code: %ld\n", result);
        return 0;
    }

    // Set the DeviceForm DWORD value
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

int set_panel() {
    char* docs_path = get_documents_path();
    char zip_path[MAX_PATH], dir_path[MAX_PATH], exe_path[MAX_PATH], fse_path[MAX_PATH];
    sprintf(fse_path, "%s\\FSE", docs_path);
    sprintf(zip_path, "%s\\physpanel_0.1.0_x86_64-pc-windows-msvc.zip", fse_path);
    sprintf(dir_path, "%s\\physpanel", fse_path);
    sprintf(exe_path, "%s\\physpanel.exe", dir_path);

    // Create nested directories if needed
    CreateDirectoryA(fse_path, NULL);
    CreateDirectoryA(dir_path, NULL);

    if (!physpanel_exists(exe_path)) {
        printf("Downloading Physpanel...\n");
        if (!download_file(PHYSPANEL_URL, zip_path)) {
            free(docs_path);
            return 0;
        }
        printf("Unzipping Physpanel...\n");
        if (!unzip_file(zip_path, dir_path)) {
            free(docs_path);
            return 0;
        }
        // Clean up zip
        DeleteFileA(zip_path);
    } else {
        printf("Physpanel already exists. Skipping download.\n");
    }

    // Create scheduled task
    printf("Creating SetPanelDimensions task...\n");
    char cmd[512];
    sprintf(cmd, "schtasks /create /tn \"SetPanelDimensions\" /tr \"\\\"%s\\physpanel.exe\\\" set 155 87\" /sc onstart /ru SYSTEM /rl highest /f", dir_path);
    int result = system(cmd);
    if (result != 0) {
        printf("Failed to create SetPanelDimensions task! Error code: %d\n", result);
        free(docs_path);
        return 0;
    }

    printf("\n");
    free(docs_path);
    return 1;
}

void show_menu() {
    printf("\n  ==== Easy FSE ====\n");
    printf("\n");
    printf("   made by: kitten\n");
    printf("\n  ==================\n");
    printf("\n");
    printf("  ---------------------------------------------------------------------\n");
    printf("  This tool enables Full Screen Experience on PC's & Laptop's.\n");
    printf("\n");
    printf("  WARNING! Windows 11 25H2 (OS Build 26200.6725) or better is required!\n");
    printf("  ---------------------------------------------------------------------\n");
    printf("\n");
    printf("  1. Enable Hidden Features\n");
    printf("  2. Add DeviceForm\n");
    printf("  3. Set Panel\n");
    printf("\n");
    printf("  Reboot for changes to take effect.\n");
    printf("  -----------------------------------------\n");
    printf("\n");
    printf("  4. Reboot\n");
    printf("  0. Exit\n");
    printf("\n");
    printf("  Choice: ");
}

int main() {
    if (!is_admin()) {
        printf("Error: This app must be run as Administrator.\n");
        printf("Right-click the executable and select 'Run as administrator'.\n");
        system("pause");
        return 1;
    }

    char choice;
    do {
        show_menu();
        scanf(" %c", &choice);

        if (choice == '0') {
            printf("Exiting.\n");
            return 0;
        }

        if (choice == '4') {
            printf("Rebooting system...\n");
            system("shutdown /r /t 0");
            return 0;
        }

        if (choice != '1' && choice != '2' && choice != '3') {
            printf("Invalid choice. Please select 1, 2, 3, 4, or 0.\n");
            continue;
        }

        int action_success = 0;
        if (choice == '1') {
            char* docs_path = get_documents_path();
            char zip_path[MAX_PATH], dir_path[MAX_PATH], exe_path[MAX_PATH], fse_path[MAX_PATH];
            sprintf(fse_path, "%s\\FSE", docs_path);
            sprintf(zip_path, "%s\\ViVeTool-v0.3.4-IntelAmd.zip", fse_path);
            sprintf(dir_path, "%s\\ViVeTool", fse_path);
            sprintf(exe_path, "%s\\ViVeTool.exe", dir_path);

            // Create nested directories if needed
            CreateDirectoryA(fse_path, NULL);
            CreateDirectoryA(dir_path, NULL);

            if (!vivetool_exists(exe_path)) {
                printf("Downloading ViVeTool...\n");
                if (!download_file(VIVETOOL_URL, zip_path)) {
                    free(docs_path);
                    system("pause");
                    continue;
                }
                printf("Unzipping ViVeTool...\n");
                if (!unzip_file(zip_path, dir_path)) {
                    free(docs_path);
                    system("pause");
                    continue;
                }
                // Clean up zip
                DeleteFileA(zip_path);
            } else {
                printf("ViVeTool already exists. Skipping download.\n");
            }

            action_success = enable_features(exe_path);
            free(docs_path);
        } else if (choice == '2') {
            action_success = add_device_form();
        } else if (choice == '3') {
            action_success = set_panel();
        }

        if (!action_success) {
            system("pause");
            continue;  // Return to menu on failure
        }

        // Delay 3 seconds before clearing screen
        Sleep(3000);
        // Clear screen and return to menu
        system("cls");

    } while (1);

    return 0;
}