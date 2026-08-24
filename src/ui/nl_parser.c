/*
 * nl_parser.c - Natural-language phrase parser for the Plan 0 shell
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * The shell reads like natural language but stays structured underneath.
 * This module normalizes English-like phrases into the canonical
 * (command, arg1, arg2) triple that the token-based dispatcher in
 * shell.c already executes:
 *
 *   "list files in documents"          -> list, documents, ""
 *   "list current directory"           -> list, "", ""          (cwd = root)
 *   "create a file called notes.txt"   -> create, notes.txt, ""
 *   "write hello world to notes.txt"   -> write, notes.txt, "hello world"
 *   "delete the file notes.txt"        -> delete, notes.txt, ""
 *   "copy a.txt to b.txt"              -> copy, a.txt, b.txt
 *   "show me the memory"               -> show, memory, ""
 *
 * Approach:
 *   1. Tokenize the phrase. Tokens are stored as (start pointer, length)
 *      plus a lowercased copy for vocabulary matching; the input buffer is
 *      restored before returning so the caller can re-parse it.
 *   2. Find the verb: first match against the multi-word phrase table
 *      (filler/context words may appear between phrase words, e.g. "make
 *      a directory"), then against the single-word verb table.
 *   3. Extract arguments from the remaining tokens: filler words ("the",
 *      "please", ...) and context nouns ("file", "folder", ...) are
 *      ignored; location markers ("in", "from", ...) start a path; name
 *      markers ("called", "named", "to") start a name; special
 *      destinations ("current", "here", "home", "up") map to cwd/home/
 *      parent; anything else is a bare argument.
 *
 * Vocabulary matching is case-insensitive; extracted arguments keep their
 * original case (the filesystem is case-sensitive).
 *
 * If a phrase is not recognized or is ambiguous (for example "write
 * a.txt hello" with two bare words and no marker), nl_parse() returns -1
 * and the caller falls back to the plain token-based parser, so existing
 * syntax keeps working unchanged.
 */

#include <stdint.h>
#include <stddef.h>
#include "nl_parser.h"
#include "kstring.h"

#define NL_MAX_TOKENS 16
#define NL_WORD_LEN   32
#define NL_OUT_LEN    256

typedef struct {
    const char* orig;          /* start of the original-case token in the input */
    int olen;                  /* length of the original token */
    char lower[NL_WORD_LEN];   /* lowercased for vocabulary matching */
} nl_token_t;

/* ---- command ids produced by the parser ---- */
enum {
    NL_LIST = 1, NL_READ, NL_CREATE, NL_DELETE, NL_WRITE, NL_APPEND,
    NL_COPY, NL_MOVE, NL_RENAME, NL_MKDIR, NL_CD, NL_PWD,
    NL_MEMORY, NL_PROCESSES, NL_DATE, NL_UPTIME, NL_VERSION, NL_ECHO,
    NL_WHOAMI, NL_ENV, NL_HISTORY, NL_CLEAR, NL_HELP, NL_REBOOT,
    NL_SHUTDOWN, NL_FIND, NL_EDIT, NL_PING, NL_SYSINFO, NL_INFO,
    NL_OPEN_WINDOW, NL_CLOSE_WINDOW, NL_FOCUS_WINDOW, NL_MOVE_WINDOW,
    NL_LIST_WINDOWS
};

/* destination markers */
enum { NL_DEST_NONE = 0, NL_DEST_CWD, NL_DEST_HOME, NL_DEST_PARENT };

/* ---- small helpers ---- */

static int weq(const char* a, const char* b) {
    return k_strcmp(a, b) == 0;
}

static void tolower_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        i++;
    }
    dst[i] = 0;
}

static int in_list(const char* w, const char* const* list, int n) {
    for (int i = 0; i < n; i++) {
        if (weq(w, list[i])) return 1;
    }
    return 0;
}

static int tok_eq(const nl_token_t* t, const char* word) {
    return weq(t->lower, word);
}

/* Copy exactly the token's original bytes into dst. */
static void copy_token(char* dst, const nl_token_t* t, int max) {
    int i = 0;
    while (i < t->olen && i < max - 1) {
        dst[i] = t->orig[i];
        i++;
    }
    dst[i] = 0;
}

static void append_token(char* dst, const nl_token_t* t, int max) {
    int l = 0;
    while (dst[l]) l++;
    if (l && l < max - 1) dst[l++] = ' ';
    int i = 0;
    while (i < t->olen && l < max - 1) {
        dst[l++] = t->orig[i++];
    }
    dst[l] = 0;
}

static void join_tokens(char* out, const nl_token_t* const* toks, int count, int max) {
    out[0] = 0;
    for (int i = 0; i < count; i++) {
        append_token(out, toks[i], max);
    }
}

/* Join the original-case tokens in [start, end) verbatim (fillers and
 * all) into out. Used for write/append content, where words like "more"
 * are legitimately part of the payload and must not be dropped. */
static void join_span_verbatim(char* out, const nl_token_t* toks,
                               int start, int end, int max) {
    out[0] = 0;
    for (int i = start; i < end; i++) {
        append_token(out, &toks[i], max);
    }
}

/* ---- vocabulary ---- */

static const char* const fillers[] = {
    "the", "a", "an", "please", "kindly", "would", "like", "want", "can",
    "could", "you", "i", "me", "my", "this", "that", "these", "those",
    "all", "some", "of", "with", "for", "do", "does", "did", "yes", "just",
    "may", "might", "shall", "should", "will", "we", "our", "us", "it",
    "its", "them", "their", "any", "each", "every", "other", "another",
    "ok", "okay", "alright", "sure", "now", "then", "so", "well", "also",
    "actually", "really", "is", "are", "am", "what", "which", "who",
    "about", "if", "even", "only", "much", "many", "more", "most",
    "little", "few", "please"
};
#define FILLER_COUNT ((int)(sizeof(fillers) / sizeof(fillers[0])))

static const char* const contexts[] = {
    "file", "files", "folder", "folders", "directory", "directories",
    "dir", "dirs", "item", "items", "entry", "entries", "content",
    "contents", "screen", "things", "stuff", "everything", "data"
};
#define CONTEXT_COUNT ((int)(sizeof(contexts) / sizeof(contexts[0])))

static const char* const loc_markers[] = {
    "in", "inside", "from", "under", "within", "at", "on"
};
#define LOC_COUNT ((int)(sizeof(loc_markers) / sizeof(loc_markers[0])))

static const char* const name_markers[] = {
    "called", "named", "entitled", "to"
};
#define NAME_COUNT ((int)(sizeof(name_markers) / sizeof(name_markers[0])))

static int special_dest(const char* w) {
    if (weq(w, "current") || weq(w, "here") || weq(w, "same")) return NL_DEST_CWD;
    if (weq(w, "home") || weq(w, "root")) return NL_DEST_HOME;
    if (weq(w, "parent") || weq(w, "up") || weq(w, "back")) return NL_DEST_PARENT;
    return NL_DEST_NONE;
}

/* multi-word verb phrases — checked before single-word verbs */
typedef struct {
    const char* w1;
    const char* w2;
    const char* w3;
    int words;
    int id;
} nl_phrase_t;

static const nl_phrase_t phrases[] = {
    {"current", "date", "time", 3, NL_DATE},
    {"who", "am", "i", 3, NL_WHOAMI},
    {"make", "directory", 0, 2, NL_MKDIR},
    {"make", "folder", 0, 2, NL_MKDIR},
    {"make", "dir", 0, 2, NL_MKDIR},
    {"create", "directory", 0, 2, NL_MKDIR},
    {"create", "folder", 0, 2, NL_MKDIR},
    {"new", "directory", 0, 2, NL_MKDIR},
    {"new", "folder", 0, 2, NL_MKDIR},
    {"go", "to", 0, 2, NL_CD},
    {"go", "into", 0, 2, NL_CD},
    {"change", "directory", 0, 2, NL_CD},
    {"open", "window", 0, 2, NL_OPEN_WINDOW},
    {"open", "application", 0, 2, NL_OPEN_WINDOW},
    {"launch", "window", 0, 2, NL_OPEN_WINDOW},
    {"launch", "application", 0, 2, NL_OPEN_WINDOW},
    {"close", "window", 0, 2, NL_CLOSE_WINDOW},
    {"close", "application", 0, 2, NL_CLOSE_WINDOW},
    {"focus", "window", 0, 2, NL_FOCUS_WINDOW},
    {"move", "window", 0, 2, NL_MOVE_WINDOW},
    {"list", "windows", 0, 2, NL_LIST_WINDOWS},
    {"show", "windows", 0, 2, NL_LIST_WINDOWS},
    {"display", "windows", 0, 2, NL_LIST_WINDOWS},
    {"show", "memory", 0, 2, NL_MEMORY},
    {"display", "memory", 0, 2, NL_MEMORY},
    {"view", "memory", 0, 2, NL_MEMORY},
    {"show", "processes", 0, 2, NL_PROCESSES},
    {"list", "processes", 0, 2, NL_PROCESSES},
    {"display", "processes", 0, 2, NL_PROCESSES},
    {"show", "tasks", 0, 2, NL_PROCESSES},
    {"list", "tasks", 0, 2, NL_PROCESSES},
    {"system", "information", 0, 2, NL_SYSINFO},
    {"system", "info", 0, 2, NL_SYSINFO},
    {"date", "time", 0, 2, NL_DATE},
    {"current", "time", 0, 2, NL_DATE},
    {"current", "date", 0, 2, NL_DATE},
    {"clear", "screen", 0, 2, NL_CLEAR},
    {"clean", "screen", 0, 2, NL_CLEAR},
    {"information", "about", 0, 2, NL_INFO},
    {"info", "about", 0, 2, NL_INFO},
    {"details", "about", 0, 2, NL_INFO},
    {"show", "files", 0, 2, NL_LIST},
    {"display", "files", 0, 2, NL_LIST},
    {"view", "files", 0, 2, NL_LIST},
    {"list", "files", 0, 2, NL_LIST},
    {"see", "files", 0, 2, NL_LIST},
    {"show", "contents", 0, 2, NL_LIST},
    {"list", "contents", 0, 2, NL_LIST},
    {"show", "file", 0, 2, NL_READ},
    {"display", "file", 0, 2, NL_READ},
    {"view", "file", 0, 2, NL_READ},
    {"see", "file", 0, 2, NL_READ},
    {"power", "off", 0, 2, NL_SHUTDOWN},
};
#define PHRASE_COUNT ((int)(sizeof(phrases) / sizeof(phrases[0])))

/* single-word verbs */
typedef struct {
    const char* word;
    int id;
} nl_verb_t;

static const nl_verb_t verbs[] = {
    {"list", NL_LIST}, {"show", NL_LIST}, {"display", NL_LIST},
    {"view", NL_LIST}, {"see", NL_LIST}, {"ls", NL_LIST}, {"enumerate", NL_LIST},
    {"create", NL_CREATE}, {"make", NL_CREATE}, {"new", NL_CREATE},
    {"touch", NL_CREATE}, {"start", NL_CREATE}, {"generate", NL_CREATE},
    {"delete", NL_DELETE}, {"remove", NL_DELETE}, {"erase", NL_DELETE},
    {"destroy", NL_DELETE}, {"drop", NL_DELETE}, {"rm", NL_DELETE},
    {"discard", NL_DELETE},
    {"read", NL_READ}, {"cat", NL_READ}, {"open", NL_READ}, {"dump", NL_READ},
    {"write", NL_WRITE}, {"save", NL_WRITE}, {"store", NL_WRITE},
    {"put", NL_WRITE}, {"set", NL_WRITE}, {"record", NL_WRITE},
    {"append", NL_APPEND},
    {"copy", NL_COPY}, {"duplicate", NL_COPY}, {"clone", NL_COPY}, {"cp", NL_COPY},
    {"move", NL_MOVE}, {"relocate", NL_MOVE}, {"transfer", NL_MOVE},
    {"rename", NL_RENAME},
    {"mkdir", NL_MKDIR}, {"md", NL_MKDIR},
    {"cd", NL_CD}, {"go", NL_CD}, {"goto", NL_CD},
    {"pwd", NL_PWD}, {"where", NL_PWD}, {"location", NL_PWD},
    {"memory", NL_MEMORY}, {"ram", NL_MEMORY},
    {"processes", NL_PROCESSES}, {"process", NL_PROCESSES},
    {"tasks", NL_PROCESSES}, {"task", NL_PROCESSES},
    {"date", NL_DATE}, {"time", NL_DATE}, {"clock", NL_DATE}, {"today", NL_DATE},
    {"uptime", NL_UPTIME},
    {"version", NL_VERSION},
    {"echo", NL_ECHO}, {"say", NL_ECHO}, {"repeat", NL_ECHO}, {"print", NL_ECHO},
    {"whoami", NL_WHOAMI}, {"user", NL_WHOAMI},
    {"env", NL_ENV}, {"environment", NL_ENV},
    {"history", NL_HISTORY},
    {"clear", NL_CLEAR}, {"cls", NL_CLEAR}, {"clean", NL_CLEAR}, {"wipe", NL_CLEAR},
    {"help", NL_HELP}, {"commands", NL_HELP}, {"usage", NL_HELP},
    {"options", NL_HELP}, {"assistance", NL_HELP},
    {"reboot", NL_REBOOT}, {"restart", NL_REBOOT},
    {"shutdown", NL_SHUTDOWN}, {"poweroff", NL_SHUTDOWN},
    {"find", NL_FIND}, {"search", NL_FIND}, {"locate", NL_FIND},
    {"edit", NL_EDIT},
    {"ping", NL_PING},
    {"here", NL_PWD},
};
#define VERB_COUNT ((int)(sizeof(verbs) / sizeof(verbs[0])))

/* ---- tokenizer ----
 * NUL-separates tokens in place (for lowercase conversion); the caller
 * must call untokenize() to restore the spaces before the buffer is used
 * again. */
static int tokenize(char* buf, nl_token_t* toks, int max) {
    int n = 0;
    char* p = buf;
    while (*p && n < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        char* start = p;
        while (*p && *p != ' ') p++;
        toks[n].orig = start;
        toks[n].olen = (int)(p - start);
        if (*p) {
            *p = 0;   /* terminate the token for lowercase conversion */
            p++;
        }
        tolower_copy(toks[n].lower, start, NL_WORD_LEN);
        n++;
    }
    return n;
}

static void untokenize(nl_token_t* toks, int n) {
    for (int i = 0; i < n; i++) {
        if (i < n - 1) {
            /* the final token's olen position is the original string end */
            ((char*)toks[i].orig)[toks[i].olen] = ' ';
        }
    }
}

/* Match a phrase starting at token i, allowing filler words to appear
 * between the phrase's words ("make a directory"). Context words are NOT
 * skipped here: many phrase words are themselves context nouns
 * ("directory", "files", "folder", ...), so skipping them would consume
 * the phrase's own words. On success sets *end_out to the index after
 * the last phrase word. */
static int match_phrase_at(nl_token_t* toks, int n, int i,
                           const nl_phrase_t* ph, int* end_out) {
    if (!tok_eq(&toks[i], ph->w1)) return 0;

    int j = i + 1;
    while (j < n && in_list(toks[j].lower, fillers, FILLER_COUNT)) {
        j++;
    }
    if (j >= n || !tok_eq(&toks[j], ph->w2)) return 0;
    j++;

    if (ph->words == 3) {
        while (j < n && in_list(toks[j].lower, fillers, FILLER_COUNT)) {
            j++;
        }
        if (j >= n || !tok_eq(&toks[j], ph->w3)) return 0;
        j++;
    }

    *end_out = j;
    return 1;
}

/* ---- public API ---- */

int nl_parse(char* input, char* cmd, char* arg1, char* arg2) {
    nl_token_t toks[NL_MAX_TOKENS];
    int n = tokenize(input, toks, NL_MAX_TOKENS);
    int result = 0;

    if (n == 0) {
        untokenize(toks, n);
        return -1;
    }

    /* Find the verb: multi-word phrases first, then single verbs.
     * The verb must sit at the START of the phrase (after at most some
     * filler words): a verb token buried mid-command — e.g. "grep
     * Uptime /proc/uptime" — belongs to a plain token-based command,
     * not an NL phrase, so once a non-filler precedes the position the
     * scan stops and the parser declines the claim. */
    int verb_id = 0;
    int verb_idx = 0;
    int verb_end = 0;
    int found = 0;
    for (int i = 0; i < n && !found; i++) {
        if (i > 0 && !in_list(toks[i - 1].lower, fillers, FILLER_COUNT)) break;
        for (int pi = 0; pi < PHRASE_COUNT; pi++) {
            int end = 0;
            if (match_phrase_at(toks, n, i, &phrases[pi], &end)) {
                verb_id = phrases[pi].id;
                verb_idx = i;
                verb_end = end;
                found = 1;
                break;
            }
        }
        if (found) break;
        for (int vi = 0; vi < VERB_COUNT; vi++) {
            if (tok_eq(&toks[i], verbs[vi].word)) {
                verb_id = verbs[vi].id;
                verb_idx = i;
                verb_end = i + 1;
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        untokenize(toks, n);
        return -1;
    }

    /* show/display/view/see family: an object word may follow the verb,
     * possibly after filler words ("show me the memory", "show the file x") */
    if (verb_id == NL_LIST) {
        if (tok_eq(&toks[verb_idx], "show") || tok_eq(&toks[verb_idx], "display") ||
            tok_eq(&toks[verb_idx], "view") || tok_eq(&toks[verb_idx], "see")) {
            for (int j = verb_end; j < n; j++) {
                if (in_list(toks[j].lower, fillers, FILLER_COUNT)) continue;
                if (tok_eq(&toks[j], "memory") || tok_eq(&toks[j], "ram")) {
                    verb_id = NL_MEMORY;
                } else if (tok_eq(&toks[j], "processes") ||
                           tok_eq(&toks[j], "process") ||
                           tok_eq(&toks[j], "tasks") ||
                           tok_eq(&toks[j], "task")) {
                    verb_id = NL_PROCESSES;
                } else if (tok_eq(&toks[j], "windows")) {
                    verb_id = NL_LIST_WINDOWS;
                } else if (tok_eq(&toks[j], "file")) {
                    verb_id = NL_READ;
                } else if (tok_eq(&toks[j], "files")) {
                    verb_id = NL_LIST;
                } else {
                    break;
                }
                verb_end = j + 1;
                break;
            }
        }
    }

    /* Extract arguments from the remaining tokens. */
    int mode = 0;                              /* 0=bare, 1=location, 2=name */
    int dest = NL_DEST_NONE;
    int name_marker_at = -1;                   /* index of first name marker */
    char path_buf[NL_OUT_LEN];
    char name_buf[NL_OUT_LEN];
    const nl_token_t* bare_toks[NL_MAX_TOKENS];
    int bare_count = 0;
    path_buf[0] = 0;
    name_buf[0] = 0;

    for (int j = verb_end; j < n; j++) {
        if (in_list(toks[j].lower, fillers, FILLER_COUNT)) continue;
        if (in_list(toks[j].lower, contexts, CONTEXT_COUNT)) continue;
        int sd = special_dest(toks[j].lower);
        if (sd) {
            dest = sd;
            continue;
        }
        if (in_list(toks[j].lower, loc_markers, LOC_COUNT)) {
            mode = 1;
            continue;
        }
        if (in_list(toks[j].lower, name_markers, NAME_COUNT)) {
            if (name_marker_at < 0) name_marker_at = j;
            mode = 2;
            continue;
        }
        if (mode == 1) {
            append_token(path_buf, &toks[j], NL_OUT_LEN);
            continue;
        }
        if (mode == 2) {
            append_token(name_buf, &toks[j], NL_OUT_LEN);
            continue;
        }
        if (bare_count < NL_MAX_TOKENS) {
            bare_toks[bare_count++] = &toks[j];
        }
    }

    char bare_joined[NL_OUT_LEN];
    join_tokens(bare_joined, bare_toks, bare_count, NL_OUT_LEN);

    cmd[0] = 0;
    arg1[0] = 0;
    arg2[0] = 0;

    switch (verb_id) {
    case NL_LIST:
        k_strcpy(cmd, "list");
        if (path_buf[0]) {
            k_strcpy(arg1, path_buf);
        } else if (bare_count == 1) {
            copy_token(arg1, bare_toks[0], NL_OUT_LEN);
        } else if (bare_count > 1) {
            result = -1;  /* ambiguous — fall back to the token parser */
        }
        /* otherwise arg1 stays "" = current directory */
        break;

    case NL_READ:
    case NL_CREATE:
    case NL_DELETE:
    case NL_MKDIR:
    case NL_FIND:
    case NL_EDIT:
    case NL_PING:
        k_strcpy(cmd, verb_id == NL_READ ? "read" :
                     verb_id == NL_CREATE ? "create" :
                     verb_id == NL_DELETE ? "delete" :
                     verb_id == NL_MKDIR ? "mkdir" :
                     verb_id == NL_FIND ? "find" :
                     verb_id == NL_EDIT ? "edit" : "ping");
        if (name_buf[0]) {
            k_strcpy(arg1, name_buf);
        } else if (path_buf[0]) {
            k_strcpy(arg1, path_buf);
        } else if (bare_count == 1) {
            copy_token(arg1, bare_toks[0], NL_OUT_LEN);
        } else if (bare_count > 1) {
            result = -1;
        }
        break;

    case NL_WRITE:
    case NL_APPEND:
        k_strcpy(cmd, verb_id == NL_WRITE ? "write" : "append");
        if (name_buf[0]) {
            /* "write hello world to notes.txt": name after the marker,
             * content is everything before it, kept verbatim ("more
             * text" must not lose "more" just because it is a filler) */
            k_strcpy(arg1, name_buf);
            if (name_marker_at > verb_end) {
                join_span_verbatim(arg2, toks, verb_end, name_marker_at,
                                   NL_OUT_LEN);
            }
        } else if (bare_count == 0) {
            /* "write" alone — no target */
        } else if (bare_count == 1) {
            /* "write notes.txt" (no content) */
            copy_token(arg1, bare_toks[0], NL_OUT_LEN);
        } else {
            result = -1;  /* "write a.txt hello" — old parser handles it */
        }
        break;

    case NL_COPY:
    case NL_MOVE:
    case NL_RENAME:
        k_strcpy(cmd, verb_id == NL_COPY ? "copy" :
                     verb_id == NL_MOVE ? "move" : "rename");
        if (name_buf[0]) {
            k_strcpy(arg1, bare_joined);   /* source */
            k_strcpy(arg2, name_buf);      /* destination */
        } else if (bare_count == 2) {
            copy_token(arg1, bare_toks[0], NL_OUT_LEN);
            copy_token(arg2, bare_toks[1], NL_OUT_LEN);
        } else {
            result = -1;
        }
        break;

    case NL_CD:
        if (dest == NL_DEST_PARENT) {
            k_strcpy(cmd, "up");
            break;
        }
        if (dest == NL_DEST_HOME) {
            k_strcpy(cmd, "home");
            break;
        }
        k_strcpy(cmd, "cd");
        if (path_buf[0]) {
            k_strcpy(arg1, path_buf);
        } else if (name_buf[0]) {
            k_strcpy(arg1, name_buf);      /* "cd to /home" */
        } else if (bare_count == 1) {
            copy_token(arg1, bare_toks[0], NL_OUT_LEN);
        } else if (bare_count > 1) {
            result = -1;
        } else if (dest == NL_DEST_CWD) {
            k_strcpy(arg1, "/");
        }
        break;

    case NL_ECHO:
        k_strcpy(cmd, "echo");
        k_strcpy(arg1, bare_joined);
        break;

    case NL_MEMORY:
        k_strcpy(cmd, "show");
        k_strcpy(arg1, "memory");
        break;

    case NL_PROCESSES:
        k_strcpy(cmd, "show");
        k_strcpy(arg1, "processes");
        break;

    case NL_SYSINFO:
        k_strcpy(cmd, "system");
        k_strcpy(arg1, "information");
        break;

    case NL_INFO:
        k_strcpy(cmd, "information");
        k_strcpy(arg1, "about");
        if (name_buf[0]) {
            k_strcpy(arg2, name_buf);
        } else if (bare_count == 1) {
            copy_token(arg2, bare_toks[0], NL_OUT_LEN);
        }
        break;

    case NL_LIST_WINDOWS:
        k_strcpy(cmd, "list");
        k_strcpy(arg1, "windows");
        break;

    case NL_OPEN_WINDOW:
        k_strcpy(cmd, "open");
        k_strcpy(arg1, "window");
        k_strcpy(arg2, bare_joined);
        break;

    case NL_CLOSE_WINDOW:
        k_strcpy(cmd, "close");
        k_strcpy(arg1, "window");
        k_strcpy(arg2, bare_joined);
        break;

    case NL_FOCUS_WINDOW:
        k_strcpy(cmd, "focus");
        k_strcpy(arg1, "window");
        k_strcpy(arg2, bare_joined);
        break;

    case NL_MOVE_WINDOW:
        k_strcpy(cmd, "move");
        k_strcpy(arg1, "window");
        k_strcpy(arg2, bare_joined);
        break;

    case NL_DATE:
        k_strcpy(cmd, "date");
        k_strcpy(arg1, "time");
        break;

    case NL_PWD:
        k_strcpy(cmd, "pwd");
        break;

    case NL_CLEAR:
        k_strcpy(cmd, "clear");
        break;

    case NL_HELP:
        k_strcpy(cmd, "help");
        break;

    case NL_UPTIME:
        k_strcpy(cmd, "uptime");
        break;

    case NL_VERSION:
        k_strcpy(cmd, "version");
        break;

    case NL_WHOAMI:
        k_strcpy(cmd, "whoami");
        break;

    case NL_ENV:
        k_strcpy(cmd, "env");
        break;

    case NL_HISTORY:
        k_strcpy(cmd, "history");
        break;

    case NL_REBOOT:
        k_strcpy(cmd, "reboot");
        break;

    case NL_SHUTDOWN:
        k_strcpy(cmd, "shutdown");
        break;

    default:
        result = -1;
        break;
    }

    untokenize(toks, n);
    return result;
}
