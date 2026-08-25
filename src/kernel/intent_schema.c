/*
 * intent_schema.c - Intent Schema Contract (Phase 2)
 *
 * External table defining all intent semantics — single source of truth.
 * Enables shared validation, audit logging, dry-run gates, and help generation.
 *
 * Copyright (C) 2026 CazyUndee
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "intent_dispatcher.h"
#include "vga.h"
#include "serial.h"
#include <stddef.h>
#include <stdint.h>

// ============================================================
// Role specification arrays — each intent type references one
// ============================================================

// No parameters (uses target_id or int_params only)
static const role_spec_t roles_none[] = {};

// Window creation: parent via target_id, optional title via param1
static const role_spec_t roles_title[] = {
    { ROLE_SOURCE_FILE, ROLE_ATTR_NONE, "title", "string" },
};

// Text content (no path — param1 = text string)
static const role_spec_t roles_text[] = {
    { ROLE_PATTERN, ROLE_ATTR_REQUIRED, "text", "string" },
};

// Key press (int_param1 = keycode, no string params)
static const role_spec_t roles_key[] = {};

// Process name
static const role_spec_t roles_process[] = {
    { ROLE_SOURCE_FILE, ROLE_ATTR_REQUIRED, "process name", "string" },
};

// ============================================================
// Filesystem operation roles (700-series — the designed use case)
// ============================================================

// copy/move: source file (param1) + destination file (param2)
static const role_spec_t roles_copy_move[] = {
    { ROLE_SOURCE_FILE, ROLE_ATTR_REQUIRED, "source", "path" },
    { ROLE_DEST_FILE,   ROLE_ATTR_REQUIRED, "destination", "path" },
};

// create dir / remove dir: directory path (param1)
static const role_spec_t roles_dir_op[] = {
    { ROLE_DEST_DIR, ROLE_ATTR_REQUIRED, "directory", "path" },
};

// search: pattern (param1)
static const role_spec_t roles_search[] = {
    { ROLE_PATTERN, ROLE_ATTR_REQUIRED, "pattern", "string" },
};

// chdir: directory path (param1) — but may be empty with mode flags
// (mode 1 = parent, mode 2 = home). So role is NOT required here.
static const role_spec_t roles_chdir[] = {
    { ROLE_DEST_DIR, ROLE_ATTR_NONE, "directory", "path" },
};

// print_cwd: no input roles (param1 used as output buffer)
static const role_spec_t roles_print_cwd[] = {};

// ============================================================
// INTENT_SCHEMAS — canonical contract table
// Indexed by intent_type_t enum value using designated initializers.
// Unused indices are zero-initialized (NULL roles, DOMAIN_SHELL, EFFECT_NONE).
// ============================================================

const intent_schema_t INTENT_SCHEMAS[INTENT_TYPE_COUNT] = {

    // --- Window management (100-106) ---
    [INTENT_CREATE_WINDOW] = {
        .roles = roles_title,
        .role_count = 1,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Create a new window with an optional title"
    },
    [INTENT_DESTROY_WINDOW] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_DELETE,
        .description = "Destroy an existing window"
    },
    [INTENT_MOVE_WINDOW] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Move a window to new x,y coordinates"
    },
    [INTENT_RESIZE_WINDOW] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Resize a window to new width,height"
    },
    [INTENT_SHOW_WINDOW] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Show a hidden window"
    },
    [INTENT_HIDE_WINDOW] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Hide a visible window"
    },
    [INTENT_FOCUS_WINDOW] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Focus (select) a window"
    },

    // --- Node management (200-206) ---
    [INTENT_CREATE_NODE] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Create a new UI node"
    },
    [INTENT_DESTROY_NODE] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_DELETE,
        .description = "Destroy a UI node"
    },
    [INTENT_MOVE_NODE] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Move a node to new coordinates"
    },
    [INTENT_RESIZE_NODE] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Resize a node"
    },
    [INTENT_SET_NODE_TEXT] = {
        .roles = roles_text,
        .role_count = 1,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Set the text content of a node"
    },
    [INTENT_SET_NODE_VALUE] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Set the integer value of a node"
    },
    [INTENT_SET_NODE_FLAGS] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Set the flags on a node"
    },

    // --- Interaction (300-304) ---
    [INTENT_CLICK_NODE] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Click a UI node"
    },
    [INTENT_DOUBLE_CLICK_NODE] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Double-click a UI node"
    },
    [INTENT_RIGHT_CLICK_NODE] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Right-click a UI node"
    },
    [INTENT_KEY_PRESS] = {
        .roles = roles_key,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Simulate a key press"
    },
    [INTENT_TEXT_INPUT] = {
        .roles = roles_text,
        .role_count = 1,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_WRITE,
        .description = "Input text into a focused node"
    },

    // --- System (400-403) ---
    [INTENT_SHUTDOWN] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_POLICY,
        .effect = EFFECT_WRITE,
        .description = "Shut down the system"
    },
    [INTENT_REBOOT] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_POLICY,
        .effect = EFFECT_WRITE,
        .description = "Reboot the system"
    },
    [INTENT_SUSPEND] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_POLICY,
        .effect = EFFECT_WRITE,
        .description = "Suspend the system"
    },
    [INTENT_DEBUG_INFO] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_READ,
        .description = "Display kernel debug information"
    },

    // --- Process management (600-603) ---
    [INTENT_START_PROCESS] = {
        .roles = roles_process,
        .role_count = 1,
        .domain = DOMAIN_POLICY,
        .effect = EFFECT_WRITE,
        .description = "Start a new process"
    },
    [INTENT_STOP_PROCESS] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_POLICY,
        .effect = EFFECT_DELETE,
        .description = "Stop a running process"
    },
    [INTENT_PAUSE_PROCESS] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_POLICY,
        .effect = EFFECT_WRITE,
        .description = "Pause a running process"
    },
    [INTENT_RESUME_PROCESS] = {
        .roles = roles_none,
        .role_count = 0,
        .domain = DOMAIN_POLICY,
        .effect = EFFECT_WRITE,
        .description = "Resume a paused process"
    },

    // --- Filesystem operations (700-706, v0.5.0 intent model completion) ---
    [INTENT_FS_COPY_FILE] = {
        .roles = roles_copy_move,
        .role_count = 2,
        .domain = DOMAIN_VFS,
        .effect = EFFECT_WRITE,
        .description = "Copy a file from source path to destination path"
    },
    [INTENT_FS_MOVE_FILE] = {
        .roles = roles_copy_move,
        .role_count = 2,
        .domain = DOMAIN_VFS,
        .effect = EFFECT_WRITE,
        .description = "Move (rename) a file from source path to destination path"
    },
    [INTENT_FS_CREATE_DIR] = {
        .roles = roles_dir_op,
        .role_count = 1,
        .domain = DOMAIN_VFS,
        .effect = EFFECT_WRITE,
        .description = "Create a new directory"
    },
    [INTENT_FS_REMOVE_DIR] = {
        .roles = roles_dir_op,
        .role_count = 1,
        .domain = DOMAIN_VFS,
        .effect = EFFECT_DELETE,
        .description = "Remove an empty directory"
    },
    [INTENT_FS_SEARCH] = {
        .roles = roles_search,
        .role_count = 1,
        .domain = DOMAIN_VFS,
        .effect = EFFECT_READ,
        .description = "Search for files matching a pattern"
    },
    [INTENT_FS_CHDIR] = {
        .roles = roles_chdir,
        .role_count = 1,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_READ,
        .description = "Change the current working directory"
    },
    [INTENT_FS_PRINT_CWD] = {
        .roles = roles_print_cwd,
        .role_count = 0,
        .domain = DOMAIN_SHELL,
        .effect = EFFECT_READ,
        .description = "Print the current working directory path"
    },
};

// ============================================================
// Helpers
// ============================================================

const char* domain_name(mutation_domain_t d) {
    switch (d) {
        case DOMAIN_SHELL:  return "shell";
        case DOMAIN_VFS:    return "vfs";
        case DOMAIN_POLICY: return "policy";
        default:            return "unknown";
    }
}

const char* effect_name(effect_t e) {
    switch (e) {
        case EFFECT_NONE:   return "none";
        case EFFECT_READ:   return "read";
        case EFFECT_WRITE:  return "write";
        case EFFECT_DELETE: return "delete";
        default:            return "unknown";
    }
}

const char* intent_name(intent_type_t type) {
    switch (type) {
        // Window
        case INTENT_CREATE_WINDOW:   return "create_window";
        case INTENT_DESTROY_WINDOW:  return "destroy_window";
        case INTENT_MOVE_WINDOW:     return "move_window";
        case INTENT_RESIZE_WINDOW:   return "resize_window";
        case INTENT_SHOW_WINDOW:     return "show_window";
        case INTENT_HIDE_WINDOW:     return "hide_window";
        case INTENT_FOCUS_WINDOW:    return "focus_window";
        // Node
        case INTENT_CREATE_NODE:     return "create_node";
        case INTENT_DESTROY_NODE:    return "destroy_node";
        case INTENT_MOVE_NODE:       return "move_node";
        case INTENT_RESIZE_NODE:     return "resize_node";
        case INTENT_SET_NODE_TEXT:   return "set_node_text";
        case INTENT_SET_NODE_VALUE:  return "set_node_value";
        case INTENT_SET_NODE_FLAGS:  return "set_node_flags";
        // Interaction
        case INTENT_CLICK_NODE:       return "click_node";
        case INTENT_DOUBLE_CLICK_NODE: return "double_click_node";
        case INTENT_RIGHT_CLICK_NODE: return "right_click_node";
        case INTENT_KEY_PRESS:       return "key_press";
        case INTENT_TEXT_INPUT:      return "text_input";
        // System
        case INTENT_SHUTDOWN:   return "shutdown";
        case INTENT_REBOOT:     return "reboot";
        case INTENT_SUSPEND:    return "suspend";
        case INTENT_DEBUG_INFO: return "debug_info";
        // Process
        case INTENT_START_PROCESS:  return "start_process";
        case INTENT_STOP_PROCESS:   return "stop_process";
        case INTENT_PAUSE_PROCESS:  return "pause_process";
        case INTENT_RESUME_PROCESS: return "resume_process";
        // Filesystem operations (v0.5.0)
        case INTENT_FS_COPY_FILE:   return "fs_copy_file";
        case INTENT_FS_MOVE_FILE:   return "fs_move_file";
        case INTENT_FS_CREATE_DIR:  return "fs_create_dir";
        case INTENT_FS_REMOVE_DIR:  return "fs_remove_dir";
        case INTENT_FS_SEARCH:      return "fs_search";
        case INTENT_FS_CHDIR:       return "fs_chdir";
        case INTENT_FS_PRINT_CWD:   return "fs_print_cwd";
        default:                    return "unknown";
    }
}

// ============================================================
// Schema lookup
// ============================================================

static const intent_schema_t* schema_for_type(uint32_t type) {
    if (type < INTENT_TYPE_COUNT) {
        const intent_schema_t* s = &INTENT_SCHEMAS[type];
        // Zero-initialized entries have NULL roles — treat as absent
        if (s->description != NULL) {
            return s;
        }
    }
    return NULL;
}

// ============================================================
// Role value extraction
//
// Maps canonical role IDs to the actual intent_t field that
// carries that role's value. Every intent type uses the same
// field-to-role mapping, so one function suffices for all.
// ============================================================

const char* extract_role_value(const intent_t* intent, role_id_t role) {
    if (!intent) return NULL;

    switch (role) {
        case ROLE_SOURCE_FILE:
            return intent->param1;
        case ROLE_SOURCE_DIR:
            return intent->param1;
        case ROLE_DEST_FILE:
            return intent->param2;
        case ROLE_DEST_DIR:
            return intent->param1;
        case ROLE_PATTERN:
            return intent->param1;
        case ROLE_SCOPE:
            return intent->param2;
        case ROLE_MUTATION_DOMAIN:
            // Not a string-carrying role — cast is for uniform interface
            return (const char*)&intent->int_param1;
        default:
            return NULL;
    }
}

// ============================================================
// Schema-aware validation
//
// Checks required-role constraints defined by the schema.
// Does NOT replace handler-level validation (e.g. CHDIR's
// mode-flag semantics); it catches common missing-param errors
// early so individual handlers don't each repeat the same
// `if (param1[0] == 0) return ERR_INVALID_PARAMS` check.
// ============================================================

error_t validate_intent_against_schema(const intent_t* intent) {
    if (!intent) return ERR_INVALID_INTENT;

    const intent_schema_t* schema = schema_for_type(intent->type);
    if (!schema) {
        // No schema entry = unknown intent type. Accept but annotate.
        // (The existing validate_intent() already catches out-of-range.)
        return ERR_SUCCESS;
    }

    for (size_t i = 0; i < schema->role_count; i++) {
        const role_spec_t* spec = &schema->roles[i];

        if (spec->attrs & ROLE_ATTR_REQUIRED) {
            const char* val = extract_role_value(intent, spec->role);
            if (!val || val[0] == '\0') {
                return ERR_INVALID_PARAMS;
            }
        }
    }

    return ERR_SUCCESS;
}

// ============================================================
// Dry-run / confirmation gates
//
// DELETE-effect intents may need user confirmation.
// READ-effect intents are always dry-run safe.
// ============================================================

bool intent_is_dry_run_safe(intent_type_t type) {
    const intent_schema_t* s = schema_for_type(type);
    if (!s) return false;
    return s->effect == EFFECT_READ;
}

bool intent_requires_confirmation(intent_type_t type) {
    const intent_schema_t* s = schema_for_type(type);
    if (!s) return false;

    // DELETE-effect and POLICY-domain intents always need confirmation
    if (s->effect == EFFECT_DELETE) return true;
    if (s->domain == DOMAIN_POLICY) return true;

    // SHELL-domain READ-effect (e.g. chdir, pwd) never needs confirmation
    return false;
}

// Helper: write a decimal uint64 to serial (no serial_put_dec available)
static void serial_put_dec(uint64_t n) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (n == 0) buf[--i] = '0';
    else while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    serial_writestring(&buf[i]);
}

// Write a line to both VGA terminal and serial
static void output_both(const char* s) {
    terminal_writestring(s);
    serial_writestring(s);
}

static void output_both_nl(const char* s) {
    terminal_writestring_nl(s);
    serial_writestring(s);
    serial_writestring("\n");
}

// ============================================================
// Audit logging
//
// Records intent type, timestamp, and key parameters to both
// VGA terminal and serial port (QEMU -serial stdio).
// Future: swap VGA for framebuffer.
// ============================================================

void audit_intent(const intent_t* intent) {
    if (!intent) return;

    const intent_schema_t* schema = schema_for_type(intent->type);
    const char* name = intent_name(intent->type);
    const char* desc = schema ? schema->description : "unknown";
    const char* dom  = schema ? domain_name(schema->domain) : "?";
    const char* eff  = schema ? effect_name(schema->effect) : "?";

    output_both("[AUDIT] ");
    output_both(name);
    output_both(" (");
    output_both(desc);
    output_both_nl(")");

    output_both("[AUDIT]   pid=");
    terminal_put_dec(intent->source_process_id);
    serial_put_dec(intent->source_process_id);
    output_both("  seq=");
    terminal_put_dec(intent->sequence_number);
    serial_put_dec(intent->sequence_number);
    output_both("  ts=");
    terminal_put_dec(intent->timestamp);
    serial_put_dec(intent->timestamp);
    output_both_nl("");

    if (intent->param1[0]) {
        output_both("[AUDIT]   param1=\"");
        output_both(intent->param1);
        output_both_nl("\"");
    }
    if (intent->param2[0]) {
        output_both("[AUDIT]   param2=\"");
        output_both(intent->param2);
        output_both_nl("\"");
    }

    output_both("[AUDIT]   domain=");
    output_both(dom);
    output_both("  effect=");
    output_both(eff);
    output_both_nl("");
}

// ============================================================
// Help generation
//
// Prints a human-readable summary of an intent's contract
// to the terminal.
// ============================================================

void print_help_for(intent_type_t type) {
    const intent_schema_t* s = schema_for_type(type);
    if (!s) return;

    const char* name = intent_name(type);

    terminal_writestring("  ");
    terminal_writestring_nl(name);

    terminal_writestring("    ");
    terminal_writestring_nl(s->description);

    terminal_writestring("    domain: ");
    terminal_writestring(domain_name(s->domain));
    terminal_writestring("  |  effect: ");
    terminal_writestring(effect_name(s->effect));
    terminal_writestring_nl("");

    if (s->role_count > 0) {
        terminal_writestring_nl("    parameters:");
        for (size_t i = 0; i < s->role_count; i++) {
            const role_spec_t* r = &s->roles[i];
            terminal_writestring("      ");
            terminal_writestring(r->name);
            terminal_writestring(" \"");
            terminal_writestring(r->type_hint ? r->type_hint : "");
            terminal_writestring("\" (");
            terminal_writestring((r->attrs & ROLE_ATTR_REQUIRED) ? "required" : "optional");
            terminal_writestring_nl(")");
        }
    }

    if (intent_requires_confirmation(type)) {
        terminal_writestring_nl("    ! requires confirmation before execution");
    }
    if (intent_is_dry_run_safe(type)) {
        terminal_writestring_nl("    safe to dry-run");
    }
}
