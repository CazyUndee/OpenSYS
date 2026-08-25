/*
 * intent_dispatcher.h - Plan 0 Intent Dispatcher (The "Brain")
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

#ifndef INTENT_DISPATCHER_H
#define INTENT_DISPATCHER_H

#include "state_graph.h"
#include <stdint.h>
#include <stdbool.h>

// Error codes
typedef enum {
    ERR_SUCCESS = 0,
    ERR_INVALID_INTENT = -1,
    ERR_UNAUTHORIZED = -2,
    ERR_NODE_NOT_FOUND = -3,
    ERR_INVALID_PARAMS = -4,
    ERR_SYSTEM_ERROR = -5
} error_t;

// Enhanced intent structure
typedef struct {
    // Basic intent data
    uint32_t type;
    node_id_t target_id;
    
    // Parameters
    char param1[256];
    char param2[256];
    int int_param1;
    int int_param2;
    
    // Security & tracking
    uint32_t source_process_id;
    uint64_t timestamp;
    uint32_t sequence_number;
    
    // Validation
    uint32_t checksum;
    uint8_t priority;  // 0=low, 1=normal, 2=high, 3=critical
    
    // Data payload
    void* data;
    size_t data_size;
} intent_t;

// Intent types (expanded)
typedef enum {
    // Window management
    INTENT_CREATE_WINDOW = 100,
    INTENT_DESTROY_WINDOW,
    INTENT_MOVE_WINDOW,
    INTENT_RESIZE_WINDOW,
    INTENT_SHOW_WINDOW,
    INTENT_HIDE_WINDOW,
    INTENT_FOCUS_WINDOW,
    
    // Node management
    INTENT_CREATE_NODE = 200,
    INTENT_DESTROY_NODE,
    INTENT_MOVE_NODE,
    INTENT_RESIZE_NODE,
    INTENT_SET_NODE_TEXT,
    INTENT_SET_NODE_VALUE,
    INTENT_SET_NODE_FLAGS,
    
    // Interaction
    INTENT_CLICK_NODE = 300,
    INTENT_DOUBLE_CLICK_NODE,
    INTENT_RIGHT_CLICK_NODE,
    INTENT_KEY_PRESS,
    INTENT_TEXT_INPUT,
    
    // System
    INTENT_SHUTDOWN = 400,
    INTENT_REBOOT,
    INTENT_SUSPEND,
    INTENT_DEBUG_INFO,
    
    // Process management
    INTENT_START_PROCESS = 600,
    INTENT_STOP_PROCESS,
    INTENT_PAUSE_PROCESS,
    INTENT_RESUME_PROCESS,

    // Filesystem operations (added v0.5.0, intent model completion)
    // These complete the filesystem intent category. They have no handlers
    // yet — dispatcher returns ERR_INVALID_INTENT until implemented.
    INTENT_FS_COPY_FILE = 700,
    INTENT_FS_MOVE_FILE,
    INTENT_FS_CREATE_DIR,
    INTENT_FS_REMOVE_DIR,
    INTENT_FS_SEARCH,
    INTENT_FS_CHDIR,
    INTENT_FS_PRINT_CWD,

    // Sentinel for array bounds
    INTENT_TYPE_COUNT
} intent_type_t;

// ACL (Access Control List) entry
typedef struct acl_entry {
    uint32_t process_id;
    uint32_t intent_mask;  // Bitmask of allowed intent types
    struct acl_entry* next;
} acl_entry_t;

// Intent queue for batching
typedef struct {
    intent_t intents[64];
    int count;
    int capacity;
    uint64_t last_flush;
} intent_queue_t;

// Dispatcher state
typedef struct {
    // ACL system
    acl_entry_t* acl_list;
    volatile uint32_t acl_lock;
    
    // Intent queue
    intent_queue_t queue;
    
    // Statistics
    uint64_t intents_processed;
    uint64_t intents_failed;
    uint64_t last_error_time;
    
    // Sequence numbers
    uint32_t next_sequence;
    
    // Performance monitoring
    uint64_t avg_process_time;
    uint64_t max_process_time;
} intent_dispatcher_t;

// Core API
void intent_dispatcher_init(void);
error_t intent_dispatch(intent_t* intent);

// Intent validation
int validate_intent(intent_t* intent);
int check_acl(uint32_t process_id, uint32_t intent_type);

// Intent builders
intent_t intent_create_window(node_id_t parent_id, const char* title);
intent_t intent_move_window(node_id_t window_id, int x, int y);
intent_t intent_click_node(node_id_t node_id);
intent_t intent_set_node_text(node_id_t node_id, const char* text);
intent_t intent_fs_create_dir(const char* path);
intent_t intent_fs_remove_dir(const char* path);
intent_t intent_fs_copy_file(const char* src, const char* dst);
intent_t intent_fs_move_file(const char* src, const char* dst);
intent_t intent_fs_chdir(const char* path);
intent_t intent_fs_chdir_parent(void);
intent_t intent_fs_chdir_home(void);
intent_t intent_fs_print_cwd(void);
intent_t intent_fs_search(const char* pattern);

// Queue management
int intent_queue_add(intent_t* intent);
int intent_queue_flush(void);

// ACL management
int acl_add_permission(uint32_t process_id, uint32_t intent_mask);
int acl_remove_permission(uint32_t process_id);
int acl_check_permission(uint32_t process_id, uint32_t intent_type);

// Statistics
void intent_get_stats(uint64_t* processed, uint64_t* failed, uint64_t* avg_time);

// System call interface
error_t sys_intent_dispatch(void* intent_ptr);

// ============================================================
// Intent Schema Contract (Phase 2)
// External table defining all intent semantics - single source of truth
// ============================================================

// Role identities (enum, not bitfield - enables iteration/introspection)
typedef enum {
    ROLE_SOURCE_FILE   = 0,
    ROLE_SOURCE_DIR    = 1,
    ROLE_DEST_FILE     = 2,
    ROLE_DEST_DIR      = 3,
    ROLE_PATTERN       = 4,
    ROLE_SCOPE         = 5,
    ROLE_MUTATION_DOMAIN = 6,
    ROLE_COUNT         = 7
} role_id_t;

// Role attributes (bitfield)
typedef enum {
    ROLE_ATTR_NONE       = 0,
    ROLE_ATTR_REQUIRED   = 1 << 0,
    ROLE_ATTR_DRY_RUN    = 1 << 1,
    ROLE_ATTR_DRY_RUN_OK = 1 << 2,
} role_attr_t;

// Mutation domain (policy class)
typedef enum {
    DOMAIN_SHELL  = 0,
    DOMAIN_VFS    = 1,
    DOMAIN_POLICY = 2,
    DOMAIN_COUNT  = 3
} mutation_domain_t;

// Effect (what actually happens)
typedef enum {
    EFFECT_NONE  = 0,
    EFFECT_READ  = 1,
    EFFECT_WRITE = 2,
    EFFECT_DELETE = 3,
} effect_t;

// Role specification
typedef struct {
    role_id_t role;
    role_attr_t attrs;
    const char* name;
    const char* type_hint;
} role_spec_t;

// Intent schema (external contract)
typedef struct {
    const role_spec_t* roles;
    size_t role_count;
    mutation_domain_t domain;
    effect_t effect;
    const char* description;
} intent_schema_t;

// External contract table - single source of truth
extern const intent_schema_t INTENT_SCHEMAS[];

// Schema-aware validation (replaces ad-hoc checks)
error_t validate_intent_against_schema(const intent_t* intent);

// Helper: extract role value from intent
const char* extract_role_value(const intent_t* intent, role_id_t role);

// Domain/Effect helpers
const char* domain_name(mutation_domain_t d);
const char* effect_name(effect_t e);
const char* intent_name(intent_type_t t);

// Audit logging
void audit_intent(const intent_t* intent);

// Dry-run / confirmation gates
bool intent_is_dry_run_safe(intent_type_t type);
bool intent_requires_confirmation(intent_type_t type);

// Help generation
void print_help_for(intent_type_t type);

#endif // INTENT_DISPATCHER_H
