/*
 * ui_command.c - Plan 0 Unified Command Interface Implementation
 *
 * Copyright (C) 2026 CazyUndee
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui_command.h"
#include "ui_state.h"
#include "vga.h"
#include "process.h"
#include <stddef.h>
#include <stdint.h>
#include "kstring.h"

// Parse natural language command into intent
command_result_t ui_command_parse(const char* command) {
    command_result_t result = {0};
    result.success = 1;
    
    if (!command || k_strlen(command) == 0) {
        k_strcpy(result.error, "Empty command");
        result.success = 0;
        return result;
    }
    
    // Make a working copy
    char cmd[256];
    k_strcpy(cmd, command);
    char* trimmed = k_trim(cmd);
    
    // Parse window commands
    if (k_strstr(trimmed, "open window") || k_strstr(trimmed, "open application")) {
        char* app_id = trimmed;
        while (*app_id && *app_id != ' ') app_id++;
        if (*app_id == ' ') {
            app_id++;
            while (*app_id == ' ') app_id++;
        }
        if (*app_id) {
            result = cmd_open_window(app_id);
        } else {
            k_strcpy(result.error, "Missing application ID");
            result.success = 0;
        }
    }
    else if (k_strstr(trimmed, "close window")) {
        char* win_id = trimmed;
        while (*win_id && *win_id != ' ') win_id++;
        if (*win_id == ' ') {
            win_id++;
            while (*win_id == ' ') win_id++;
        }
        if (*win_id) {
            result = cmd_close_window(win_id);
        } else {
            k_strcpy(result.error, "Missing window ID");
            result.success = 0;
        }
    }
    else if (k_strstr(trimmed, "move window")) {
        char* win_id = trimmed;
        while (*win_id && *win_id != ' ') win_id++;
        if (*win_id == ' ') {
            win_id++;
            while (*win_id == ' ') win_id++;
        }
        char* x_str = win_id;
        while (*x_str && *x_str != ' ') x_str++;
        if (*x_str == ' ') {
            *x_str = 0;
            x_str++;
            while (*x_str == ' ') x_str++;
        }
        char* y_str = x_str;
        while (*y_str && *y_str != ' ') y_str++;
        if (*y_str == ' ') {
            *y_str = 0;
            y_str++;
            while (*y_str == ' ') y_str++;
        }
        
        if (*win_id && *x_str && *y_str) {
            // Convert strings to integers (simplified)
            int x = 0, y = 0;
            char* p = x_str;
            while (*p >= '0' && *p <= '9') {
                x = x * 10 + (*p - '0');
                p++;
            }
            p = y_str;
            while (*p >= '0' && *p <= '9') {
                y = y * 10 + (*p - '0');
                p++;
            }
            result = cmd_move_window(win_id, x, y);
        } else {
            k_strcpy(result.error, "Usage: move window <id> <x> <y>");
            result.success = 0;
        }
    }
    else if (k_strstr(trimmed, "focus window")) {
        char* win_id = trimmed;
        while (*win_id && *win_id != ' ') win_id++;
        if (*win_id == ' ') {
            win_id++;
            while (*win_id == ' ') win_id++;
        }
        if (*win_id) {
            result = cmd_focus_window(win_id);
        } else {
            k_strcpy(result.error, "Missing window ID");
            result.success = 0;
        }
    }
    else if (k_strstr(trimmed, "list windows") || k_strstr(trimmed, "show windows")) {
        result = cmd_list_windows();
    }
    else {
        k_strcpy(result.error, "Unknown command");
        result.success = 0;
    }
    
    return result;
}

// Translate GUI click to intent
command_result_t ui_translate_click(int x, int y) {
    command_result_t result = {0};
    result.success = 1;
    
    ui_state_t* state = ui_state_get();
    
    // Find which window was clicked
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t* win = &state->windows[i];
        if (win->id[0] && win->visible) {
            if (x >= win->x && x < win->x + win->width &&
                y >= win->y && y < win->y + win->height) {
                
                // Find which node was clicked within the window
                for (int j = 0; j < MAX_NODES; j++) {
                    node_t* node = &state->nodes[j];
                    if (node->id[0] && node->visible && 
                        k_strcmp(node->parent_id, win->id) == 0) {
                        
                        int node_x = win->x + node->x;
                        int node_y = win->y + node->y;
                        
                        if (x >= node_x && x < node_x + node->width &&
                            y >= node_y && y < node_y + node->height) {

                            result.intent = intent_click_node(node->graph_id);
                            return result;
                        }
                    }
                }

                // Clicked on window but no node - focus window
                result.intent = intent_click_node(win->graph_id);
                return result;
            }
        }
    }
    
    k_strcpy(result.error, "No window clicked");
    result.success = 0;
    return result;
}

// Execute command
int ui_command_execute(const char* command) {
    command_result_t result = ui_command_parse(command);
    if (!result.success) {
        terminal_writestring("Error: ");
        terminal_writestring_nl(result.error);
        return -1;
    }
    
    return ui_state_execute_intent(&result.intent);
}

// Execute intent directly
int ui_command_execute_intent(intent_t* intent) {
    return ui_state_execute_intent(intent);
}

// Built-in command implementations
command_result_t cmd_open_window(const char* app_id) {
    command_result_t result = {0};
    result.success = 1;
    
    // Create window ID from app ID
    char win_id[64];
    k_strcpy(win_id, "win_");
    k_strcpy(win_id + 4, app_id);
    
    // Create the window
    if (ui_state_create_window(win_id, app_id, 100, 100, 400, 300) >= 0) {
        result.intent = intent_create_window(0, app_id);
        terminal_writestring("Opened window: ");
        terminal_writestring_nl(app_id);
    } else {
        k_strcpy(result.error, "Failed to create window");
        result.success = 0;
    }
    
    return result;
}

command_result_t cmd_close_window(const char* window_id) {
    command_result_t result = {0};
    result.success = 1;
    
    window_t* win = ui_state_get_window(window_id);
    if (win) {
        result.intent = intent_create_window(0, "");
        result.intent.type = INTENT_DESTROY_WINDOW;
        result.intent.target_id = win->graph_id;
        win->id[0] = 0;
        terminal_writestring("Closed window: ");
        terminal_writestring_nl(window_id);
    } else {
        k_strcpy(result.error, "Window not found");
        result.success = 0;
    }
    
    return result;
}

command_result_t cmd_move_window(const char* window_id, int x, int y) {
    command_result_t result = {0};
    result.success = 1;
    
    window_t* win = ui_state_get_window(window_id);
    if (win) {
        result.intent = intent_move_window(win->graph_id, x, y);
        terminal_writestring("Moved window ");
        terminal_writestring(window_id);
        terminal_writestring(" to ");
        terminal_put_dec(x);
        terminal_putchar(',');
        terminal_put_dec(y);
        terminal_writestring_nl("");
    } else {
        k_strcpy(result.error, "Window not found");
        result.success = 0;
    }
    
    return result;
}

command_result_t cmd_focus_window(const char* window_id) {
    command_result_t result = {0};
    result.success = 1;

    window_t* win = ui_state_get_window(window_id);
    if (win) {
        result.intent = intent_create_window(0, "");
        result.intent.type = INTENT_FOCUS_WINDOW;
        result.intent.target_id = win->graph_id;
        ui_state_set_focus(window_id, "");
        terminal_writestring("Focused window: ");
        terminal_writestring_nl(window_id);
    } else {
        k_strcpy(result.error, "Window not found");
        result.success = 0;
    }

    return result;
}

command_result_t cmd_list_windows(void) {
    command_result_t result = {0};
    result.success = 1;
    
    ui_state_t* state = ui_state_get();
    
    terminal_writestring_nl("Open Windows:");
    terminal_writestring_nl("-------------");
    
    int count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t* win = &state->windows[i];
        if (win->id[0] && win->visible) {
            terminal_writestring("  ");
            terminal_writestring(win->id);
            terminal_writestring(" - ");
            terminal_writestring(win->title);
            terminal_writestring(" (");
            terminal_put_dec(win->x);
            terminal_putchar(',');
            terminal_put_dec(win->y);
            terminal_writestring(" ");
            terminal_put_dec(win->width);
            terminal_putchar('x');
            terminal_put_dec(win->height);
            terminal_writestring_nl(")");
            count++;
        }
    }
    
    if (count == 0) {
        terminal_writestring_nl("  (no windows open)");
    }
    
    return result;
}

// Initialize command system
void ui_command_init(void) {
    ui_state_init();
}
