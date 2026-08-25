/*
 * intent_dispatcher.c - Plan 0 Intent Dispatcher Implementation
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

#include "intent_dispatcher.h"
#include "state_graph.h"
#include "kheap.h"
#include "vga.h"
#include "rtc.h"
#include "vfs.h"
#include "ramfs.h"
#include <stddef.h>
#include <stdint.h>
#include "kstring.h"

// Global dispatcher instance
static intent_dispatcher_t g_dispatcher;

// Simple checksum for intent validation
static uint32_t calculate_checksum(intent_t* intent) {
    uint32_t sum = 0;
    uint8_t* bytes = (uint8_t*)intent;
    
    for (size_t i = 0; i < sizeof(intent_t) - sizeof(intent->checksum); i++) {
        sum += bytes[i];
    }
    
    return sum;
}

// Get current timestamp
static uint64_t get_timestamp(void) {
    rtc_time_t time;
    rtc_read_time(&time);
    
    // Simple timestamp (could be improved with proper timer)
    return (uint64_t)time.second + 
           (uint64_t)time.minute * 60 + 
           (uint64_t)time.hour * 3600;
}

// Initialize the intent dispatcher
void intent_dispatcher_init(void) {
    g_dispatcher.acl_list = NULL;
    g_dispatcher.acl_lock = 0;
    g_dispatcher.queue.count = 0;
    g_dispatcher.queue.capacity = 64;
    g_dispatcher.queue.last_flush = get_timestamp();
    g_dispatcher.intents_processed = 0;
    g_dispatcher.intents_failed = 0;
    g_dispatcher.next_sequence = 1;
    g_dispatcher.avg_process_time = 0;
    g_dispatcher.max_process_time = 0;
    
    // Add default permissions for system processes
    acl_add_permission(0, 0xFFFFFFFF);  // Kernel can do everything
    acl_add_permission(1, 0x0000FFFF);  // GUI process limited
    acl_add_permission(2, 0x0000FFFF);  // Shell process limited
}

// Validate an intent
int validate_intent(intent_t* intent) {
    if (!intent) return 0;
    
    // Check checksum
    uint32_t expected = calculate_checksum(intent);
    if (intent->checksum != expected) {
        return 0;
    }
    
    // Check intent type range
    if (intent->type < 100 || intent->type > 799) {
        return 0;
    }
    
    // Check target node exists (if specified)
    if (intent->target_id != NODE_ID_INVALID) {
        state_node_t* node = state_graph_get_node(intent->target_id);
        if (!node) {
            return 0;
        }
    }
    
    return 1;
}

// Check ACL permissions
// 
// intent_mask is a 32-bit bitfield covering intent types 100-131
// (bit 0 = type 100, bit 1 = type 101, ..., bit 31 = type 131).
// Intent types outside this range are not per-maskable; they are
// allowed only if bit 0 (the wildcard, covering type 100) is set.
// The kernel ACL entry sets 0xFFFFFFFF so bit 0 is always set,
// meaning all current callers can use any intent type.
int check_acl(uint32_t process_id, uint32_t intent_type) {
    acl_entry_t* current = g_dispatcher.acl_list;

    while (current) {
        if (current->process_id == process_id) {
            if (intent_type < 100 || intent_type > 131) {
                return (current->intent_mask & 1U) != 0;
            }
            uint32_t bit = intent_type - 100;
            return (current->intent_mask & (1U << bit)) != 0;
        }
        current = current->next;
    }

    return 0;  // Default deny
}

// Global current working directory (process-shared for v0.5.0)
static char g_cwd[256] = "/";

// Search state for callback
static const char* search_pattern = 0;
static int search_match_count = 0;

static void search_callback_impl(const char* name, int is_dir, uint32_t size) {
    (void)is_dir; (void)size;
    if (search_pattern && k_strstr(name, search_pattern)) {
        search_match_count++;
    }
}

// Handler: INTENT_FS_CREATE_DIR
// params: param1 = directory path
static error_t handle_create_dir(intent_t* intent) {
    if (intent->param1[0] == 0) return ERR_INVALID_PARAMS;
    int r = vfs_mkdir(intent->param1);
    return (r == 0) ? ERR_SUCCESS : ERR_SYSTEM_ERROR;
}

// Handler: INTENT_FS_REMOVE_DIR
// params: param1 = directory path
static error_t handle_remove_dir(intent_t* intent) {
    if (intent->param1[0] == 0) return ERR_INVALID_PARAMS;
    int r = vfs_unlink(intent->param1);
    return (r == 0) ? ERR_SUCCESS : ERR_SYSTEM_ERROR;
}

// Handler: INTENT_FS_COPY_FILE
// params: param1 = source path, param2 = destination path
static error_t handle_copy_file(intent_t* intent) {
    if (intent->param1[0] == 0 || intent->param2[0] == 0) return ERR_INVALID_PARAMS;
    int src_fd = vfs_open(intent->param1, VFS_O_RDONLY);
    if (src_fd < 0) return ERR_NODE_NOT_FOUND;
    int dst_fd = vfs_open(intent->param2, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (dst_fd < 0) { vfs_close(src_fd); return ERR_SYSTEM_ERROR; }
    uint8_t buf[512];
    int n;
    while ((n = vfs_read(src_fd, buf, sizeof(buf))) > 0) {
        if (vfs_write(dst_fd, buf, n) != n) {
            vfs_close(src_fd); vfs_close(dst_fd);
            return ERR_SYSTEM_ERROR;
        }
    }
    vfs_close(src_fd);
    vfs_close(dst_fd);
    return ERR_SUCCESS;
}

// Handler: INTENT_FS_MOVE_FILE
// params: param1 = source path, param2 = destination path
// NOTE: VFS has no rename, so we copy+delete. Not atomic, not efficient.
// Future: add vfs_rename() to VFS layer.
static error_t handle_move_file(intent_t* intent) {
    if (intent->param1[0] == 0 || intent->param2[0] == 0) return ERR_INVALID_PARAMS;
    error_t r = handle_copy_file(intent);
    if (r != ERR_SUCCESS) return r;
    int d = vfs_unlink(intent->param1);
    return (d == 0) ? ERR_SUCCESS : ERR_SYSTEM_ERROR;
}

// Handler: INTENT_FS_CHDIR
// params: param1 = directory path (".." = parent, "/" = root, or name)
// Special: param1[0]==0 with int_param1=1 means "go to parent"
//          param1[0]==0 with int_param1=2 means "go to home" (== "/" for now)
static error_t handle_chdir(intent_t* intent) {
    const char* target = intent->param1;
    if (target[0] == 0) {
        if (intent->int_param1 == 1) {
            // parent
            int len = k_strlen(g_cwd);
            if (len > 1) {
                // strip trailing slash
                if (g_cwd[len-1] == '/') { g_cwd[len-1] = 0; len--; }
                // walk back to last /
                while (len > 0 && g_cwd[len-1] != '/') len--;
                if (len == 0) g_cwd[0] = '/', g_cwd[1] = 0;
                else g_cwd[len] = 0;
            }
            return ERR_SUCCESS;
        }
        if (intent->int_param1 == 2) {
            k_strcpy(g_cwd, "/");
            return ERR_SUCCESS;
        }
        return ERR_INVALID_PARAMS;
    }
    // For now, accept absolute paths only. Relative path resolution
    // requires prefixing g_cwd — left as future work.
    if (target[0] != '/') return ERR_INVALID_PARAMS;
    k_strcpy(g_cwd, target);
    return ERR_SUCCESS;
}

// Handler: INTENT_FS_PRINT_CWD
// Writes the CWD into intent->param1 so the caller can read it back.
static error_t handle_print_cwd(intent_t* intent) {
    k_strcpy(intent->param1, g_cwd);
    return ERR_SUCCESS;
}

// Handler: INTENT_FS_SEARCH
// params: param1 = pattern (substring), int_param1 = 0 for name match
// Scans the current VFS mount's list and counts matches.
// Real implementation would walk subdirectories recursively.
static error_t handle_search(intent_t* intent) {
    if (intent->param1[0] == 0) return ERR_INVALID_PARAMS;
    search_pattern = intent->param1;
    search_match_count = 0;
    vfs_list(search_callback_impl);
    intent->int_param2 = search_match_count;
    return ERR_SUCCESS;
}

// Apply intent to state graph
static error_t apply_intent_to_graph(intent_t* intent) {
    state_node_t* node = NULL;
    
    if (intent->target_id != NODE_ID_INVALID) {
        node = state_graph_get_node(intent->target_id);
        if (!node) return ERR_NODE_NOT_FOUND;
    }
    
    switch (intent->type) {
        case INTENT_CREATE_WINDOW: {
            node_id_t new_id = state_graph_create_node(NODE_TYPE_WINDOW, intent->target_id);
            if (new_id != NODE_ID_INVALID) {
                state_graph_set_text(new_id, intent->param1);
                state_graph_set_geometry(new_id, intent->int_param1, intent->int_param2, 400, 300);
                return ERR_SUCCESS;
            }
            return ERR_SYSTEM_ERROR;
        }
        
        case INTENT_MOVE_WINDOW: {
            if (node && node->type == NODE_TYPE_WINDOW) {
                state_graph_set_geometry(intent->target_id, 
                                     intent->int_param1, intent->int_param2,
                                     node->geometry.width, node->geometry.height);
                return ERR_SUCCESS;
            }
            return ERR_INVALID_PARAMS;
        }
        
        case INTENT_CLICK_NODE: {
            if (node) {
                // Handle click - could trigger callbacks
                state_graph_set_flags(intent->target_id, node->flags | NODE_FLAG_FOCUSED);
                return ERR_SUCCESS;
            }
            return ERR_NODE_NOT_FOUND;
        }
        
        case INTENT_SET_NODE_TEXT: {
            if (node) {
                state_graph_set_text(intent->target_id, intent->param1);
                return ERR_SUCCESS;
            }
            return ERR_NODE_NOT_FOUND;
        }
        
        case INTENT_DESTROY_WINDOW:
        case INTENT_DESTROY_NODE: {
            state_graph_delete_node(intent->target_id);
            return ERR_SUCCESS;
        }

        case INTENT_FS_CREATE_DIR: {
            return handle_create_dir(intent);
        }
        case INTENT_FS_REMOVE_DIR: {
            return handle_remove_dir(intent);
        }
        case INTENT_FS_COPY_FILE: {
            return handle_copy_file(intent);
        }
        case INTENT_FS_MOVE_FILE: {
            return handle_move_file(intent);
        }
        case INTENT_FS_CHDIR: {
            return handle_chdir(intent);
        }
        case INTENT_FS_PRINT_CWD: {
            return handle_print_cwd(intent);
        }
        case INTENT_FS_SEARCH: {
            return handle_search(intent);
        }

        default:
            return ERR_INVALID_INTENT;
    }
}

// Core dispatch function
error_t intent_dispatch(intent_t* intent) {
    uint64_t start_time = get_timestamp();
    
    // 1. Validation (legacy checks)
    if (!validate_intent(intent)) {
        g_dispatcher.intents_failed++;
        return ERR_INVALID_INTENT;
    }
    
    // 2. Schema-aware validation (Phase 2 contract)
    error_t schema_err = validate_intent_against_schema(intent);
    if (schema_err != ERR_SUCCESS) {
        g_dispatcher.intents_failed++;
        return schema_err;
    }
    
    // 3. ACL check
    if (!check_acl(intent->source_process_id, intent->type)) {
        g_dispatcher.intents_failed++;
        return ERR_UNAUTHORIZED;
    }
    
// 4. Journaling (write-ahead log) — deferred; intent is audited at step 6
// TODO: Implement MFT journaling for pre-execution crash recovery.
//   See also: include/input.h (unimplemented input.h API layer).
//   Current behavior: intents are applied immediately, then audit-logged.
//   Caller must be aware that a crash will lose intents since step 5.
    
    // 5. Apply to state graph
    error_t result = apply_intent_to_graph(intent);
    
    // 6. Audit log (after execution for result context)
    audit_intent(intent);
    
    // 7. Notify observers (handled by state graph)
    
    // 8. Update statistics
    uint64_t end_time = get_timestamp();
    uint64_t process_time = end_time - start_time;
    
    g_dispatcher.intents_processed++;
    if (process_time > g_dispatcher.max_process_time) {
        g_dispatcher.max_process_time = process_time;
    }
    
    // Update average (simple moving average)
    g_dispatcher.avg_process_time = 
        (g_dispatcher.avg_process_time * 9 + process_time) / 10;
    
    return result;
}

// Intent builders
intent_t intent_create_window(node_id_t parent_id, const char* title) {
    intent_t intent = {0};
    intent.type = INTENT_CREATE_WINDOW;
    intent.target_id = parent_id;
    k_strcpy(intent.param1, title);
    intent.int_param1 = 100;  // Default x
    intent.int_param2 = 100;  // Default y
    intent.source_process_id = 0;  // Current process
    intent.timestamp = get_timestamp();
    intent.sequence_number = g_dispatcher.next_sequence++;
    intent.priority = 1;  // Normal priority
    intent.checksum = calculate_checksum(&intent);
    
    return intent;
}

intent_t intent_move_window(node_id_t window_id, int x, int y) {
    intent_t intent = {0};
    intent.type = INTENT_MOVE_WINDOW;
    intent.target_id = window_id;
    intent.int_param1 = x;
    intent.int_param2 = y;
    intent.source_process_id = 0;
    intent.timestamp = get_timestamp();
    intent.sequence_number = g_dispatcher.next_sequence++;
    intent.priority = 1;
    intent.checksum = calculate_checksum(&intent);
    
    return intent;
}

intent_t intent_click_node(node_id_t node_id) {
    intent_t intent = {0};
    intent.type = INTENT_CLICK_NODE;
    intent.target_id = node_id;
    intent.source_process_id = 0;
    intent.timestamp = get_timestamp();
    intent.sequence_number = g_dispatcher.next_sequence++;
    intent.priority = 2;  // High priority for interactions
    intent.checksum = calculate_checksum(&intent);
    
    return intent;
}

intent_t intent_set_node_text(node_id_t node_id, const char* text) {
    intent_t intent = {0};
    intent.type = INTENT_SET_NODE_TEXT;
    intent.target_id = node_id;
    k_strcpy(intent.param1, text);
    intent.source_process_id = 0;
    intent.timestamp = get_timestamp();
    intent.sequence_number = g_dispatcher.next_sequence++;
    intent.priority = 1;
    intent.checksum = calculate_checksum(&intent);

    return intent;
}

// Filesystem intent builders
static intent_t fs_intent_build(uint32_t type, const char* p1, const char* p2, int ip1) {
    intent_t intent = {0};
    intent.type = type;
    if (p1) k_strcpy(intent.param1, p1);
    if (p2) k_strcpy(intent.param2, p2);
    intent.int_param1 = ip1;
    intent.source_process_id = 0;
    intent.timestamp = get_timestamp();
    intent.sequence_number = g_dispatcher.next_sequence++;
    intent.priority = 1;
    intent.checksum = calculate_checksum(&intent);
    return intent;
}

intent_t intent_fs_create_dir(const char* path) {
    return fs_intent_build(INTENT_FS_CREATE_DIR, path, 0, 0);
}

intent_t intent_fs_remove_dir(const char* path) {
    return fs_intent_build(INTENT_FS_REMOVE_DIR, path, 0, 0);
}

intent_t intent_fs_copy_file(const char* src, const char* dst) {
    return fs_intent_build(INTENT_FS_COPY_FILE, src, dst, 0);
}

intent_t intent_fs_move_file(const char* src, const char* dst) {
    return fs_intent_build(INTENT_FS_MOVE_FILE, src, dst, 0);
}

intent_t intent_fs_chdir(const char* path) {
    return fs_intent_build(INTENT_FS_CHDIR, path, 0, 0);
}

intent_t intent_fs_chdir_parent(void) {
    return fs_intent_build(INTENT_FS_CHDIR, 0, 0, 1);
}

intent_t intent_fs_chdir_home(void) {
    return fs_intent_build(INTENT_FS_CHDIR, 0, 0, 2);
}

intent_t intent_fs_print_cwd(void) {
    return fs_intent_build(INTENT_FS_PRINT_CWD, 0, 0, 0);
}

intent_t intent_fs_search(const char* pattern) {
    return fs_intent_build(INTENT_FS_SEARCH, pattern, 0, 0);
}

// ACL management
int acl_add_permission(uint32_t process_id, uint32_t intent_mask) {
    acl_entry_t* entry = (acl_entry_t*)kmalloc(sizeof(acl_entry_t));
    if (!entry) return -1;
    
    entry->process_id = process_id;
    entry->intent_mask = intent_mask;
    entry->next = g_dispatcher.acl_list;
    g_dispatcher.acl_list = entry;
    
    return 0;
}

int acl_remove_permission(uint32_t process_id) {
    acl_entry_t** current = &g_dispatcher.acl_list;
    
    while (*current) {
        if ((*current)->process_id == process_id) {
            acl_entry_t* to_remove = *current;
            *current = (*current)->next;
            kfree(to_remove);
            return 0;
        }
        current = &(*current)->next;
    }
    
    return -1;
}

// System call interface
error_t sys_intent_dispatch(void* intent_ptr) {
    if (!intent_ptr) return ERR_INVALID_PARAMS;

    // Copy intent from user space (simplified)
    intent_t intent;
    k_memcpy(&intent, intent_ptr, sizeof(intent_t));
    
    // Set source process ID (will use scheduler's current process when available)
    intent.source_process_id = 1;
    
    return intent_dispatch(&intent);
}

// Get statistics
void intent_get_stats(uint64_t* processed, uint64_t* failed, uint64_t* avg_time) {
    if (processed) *processed = g_dispatcher.intents_processed;
    if (failed) *failed = g_dispatcher.intents_failed;
    if (avg_time) *avg_time = g_dispatcher.avg_process_time;
}
