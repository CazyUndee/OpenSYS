/*
 * ui_command.h - Plan 0 Unified Command Interface
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

#ifndef UI_COMMAND_H
#define UI_COMMAND_H

#include "ui_state.h"

// Command parsing result
typedef struct {
    intent_t intent;
    char error[256];
    int success;
} command_result_t;

// CLI command parsing
command_result_t ui_command_parse(const char* command);

// GUI event translation
command_result_t ui_translate_click(int x, int y);
command_result_t ui_translate_key_press(const char* key);
command_result_t ui_translate_drag(const char* node_id, int dx, int dy);

// Command execution
int ui_command_execute(const char* command);
int ui_command_execute_intent(intent_t* intent);

// Built-in command handlers
command_result_t cmd_open_window(const char* app_id);
command_result_t cmd_close_window(const char* window_id);
command_result_t cmd_move_window(const char* window_id, int x, int y);
command_result_t cmd_focus_window(const char* window_id);
command_result_t cmd_list_windows(void);

// Initialize command system
void ui_command_init(void);

#endif // UI_COMMAND_H
