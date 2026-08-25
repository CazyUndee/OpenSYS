/*
 * test_nl_parser.c - Unit Tests for the Natural-Language Shell Parser
 *
 * Copyright (C) 2026 CazyUndee
 *
 * These tests exercise nl_parse() (the real implementation, not a mock)
 * on the host to lock in the natural-language phrase mapping used by the
 * shell: phrases such as "list files in documents" or "write hello world
 * to notes.txt" must normalize to the canonical (cmd, arg1, arg2) triple,
 * and ambiguous phrases must fall back (-1) to the token parser.
 */

#include "../test_framework.h"
#include "../../include/nl_parser.h"
#include <string.h>

/* Run nl_parse on a copy of the phrase (nl_parse may mutate its input). */
static int parse_phrase(const char* phrase, char* cmd, char* a1, char* a2) {
    char buf[256];
    strncpy(buf, phrase, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    return nl_parse(buf, cmd, a1, a2);
}

static void expect_parse(const char* phrase, int rc,
                         const char* c, const char* a1, const char* a2) {
    char cmd[32], arg1[256], arg2[256];
    cmd[0] = 0; arg1[0] = 0; arg2[0] = 0;
    int r = parse_phrase(phrase, cmd, arg1, arg2);
    if (r != rc) {
        printf("FAIL: phrase '%s' returned %d (expected %d)\n", phrase, r, rc);
        longjmp(__test_jmp_buf, 1);
    }
    if (rc == 0) {
        if (strcmp(cmd, c) != 0 || strcmp(arg1, a1) != 0 || strcmp(arg2, a2) != 0) {
            printf("FAIL: phrase '%s' -> cmd='%s' arg1='%s' arg2='%s' "
                   "(expected cmd='%s' arg1='%s' arg2='%s')\n",
                   phrase, cmd, arg1, arg2, c, a1, a2);
            longjmp(__test_jmp_buf, 1);
        }
    }
}

/* ---- list ---- */

void test_nl_list_phrases(void) {
    expect_parse("list", 0, "list", "", "");
    expect_parse("ls", 0, "list", "", "");
    expect_parse("list current directory", 0, "list", "", "");
    expect_parse("list the files in the current directory", 0, "list", "", "");
    expect_parse("list files in documents", 0, "list", "documents", "");
    expect_parse("list files in my documents folder", 0, "list", "documents", "");
    expect_parse("list documents", 0, "list", "documents", "");
    expect_parse("please show me the files", 0, "list", "", "");
    expect_parse("show the file notes.txt", 0, "read", "notes.txt", "");
    TEST_PASS();
}

/* ---- show-family ---- */

void test_nl_show_phrases(void) {
    expect_parse("show memory", 0, "show", "memory", "");
    expect_parse("show me the memory", 0, "show", "memory", "");
    expect_parse("display memory", 0, "show", "memory", "");
    expect_parse("show processes", 0, "show", "processes", "");
    expect_parse("show me the processes", 0, "show", "processes", "");
    expect_parse("show windows", 0, "list", "windows", "");
    expect_parse("system information", 0, "system", "information", "");
    expect_parse("current date time", 0, "date", "time", "");
    TEST_PASS();
}

/* ---- file operations ---- */

void test_nl_file_phrases(void) {
    expect_parse("create a file called notes.txt", 0, "create", "notes.txt", "");
    expect_parse("make a directory called documents", 0, "mkdir", "documents", "");
    expect_parse("delete the file notes.txt", 0, "delete", "notes.txt", "");
    expect_parse("remove notes.txt", 0, "delete", "notes.txt", "");
    expect_parse("read notes.txt", 0, "read", "notes.txt", "");
    expect_parse("cat notes.txt", 0, "read", "notes.txt", "");
    expect_parse("mkdir docs", 0, "mkdir", "docs", "");
    TEST_PASS();
}

/* ---- write / copy / move / rename ---- */

void test_nl_write_phrases(void) {
    expect_parse("write hello world to notes.txt", 0, "write", "notes.txt", "hello world");
    expect_parse("append more text to notes.txt", 0, "append", "notes.txt", "more text");
    expect_parse("write notes.txt", 0, "write", "notes.txt", "");
    /* ambiguous (two bare words, no marker) -> fall back to token parser */
    expect_parse("write a.txt hello", -1, "", "", "");
    TEST_PASS();
}

void test_nl_copy_move_rename(void) {
    expect_parse("copy a.txt to b.txt", 0, "copy", "a.txt", "b.txt");
    expect_parse("copy a.txt b.txt", 0, "copy", "a.txt", "b.txt");
    expect_parse("move a.txt to b.txt", 0, "move", "a.txt", "b.txt");
    expect_parse("rename a.txt to b.txt", 0, "rename", "a.txt", "b.txt");
    expect_parse("move window 1 10 20", 0, "move", "window", "1 10 20");
    TEST_PASS();
}

/* ---- navigation ---- */

void test_nl_navigation(void) {
    expect_parse("cd /home/user", 0, "cd", "/home/user", "");
    expect_parse("cd to /home", 0, "cd", "/home", "");
    expect_parse("go to /home", 0, "cd", "/home", "");
    expect_parse("go up", 0, "up", "", "");
    expect_parse("go home", 0, "home", "", "");
    expect_parse("where am i", 0, "pwd", "", "");
    TEST_PASS();
}

/* ---- misc ---- */

void test_nl_misc(void) {
    expect_parse("echo hello there", 0, "echo", "hello there", "");
    expect_parse("say hi", 0, "echo", "hi", "");
    expect_parse("open window calculator", 0, "open", "window", "calculator");
    expect_parse("close window 2", 0, "close", "window", "2");
    expect_parse("clear the screen", 0, "clear", "", "");
    expect_parse("uptime", 0, "uptime", "", "");
    expect_parse("version", 0, "version", "", "");
    expect_parse("search for notes", 0, "find", "notes", "");
    expect_parse("edit file.txt", 0, "edit", "file.txt", "");
    expect_parse("ping 192.168.1.1", 0, "ping", "192.168.1.1", "");
    /* A verb token buried mid-command must NOT claim the phrase: these
     * are plain token-based commands ("grep X 0/system/runtime/uptime", "grep beta
     * file"), not NL phrases — the parser must decline and fall through. */
    expect_parse("grep Uptime 0/system/runtime/uptime", -1, "", "", "");
    expect_parse("grep beta grepfile.txt", -1, "", "", "");
    /* Leading fillers before the verb still work ("please show me..."). */
    expect_parse("please show me the files", 0, "list", "", "");
    TEST_PASS();
}

/* Create test suite */
test_suite_t* create_nl_parser_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "Natural Language Parser");

    test_suite_add_test(&suite, "nl_list_phrases", test_nl_list_phrases);
    test_suite_add_test(&suite, "nl_show_phrases", test_nl_show_phrases);
    test_suite_add_test(&suite, "nl_file_phrases", test_nl_file_phrases);
    test_suite_add_test(&suite, "nl_write_phrases", test_nl_write_phrases);
    test_suite_add_test(&suite, "nl_copy_move_rename", test_nl_copy_move_rename);
    test_suite_add_test(&suite, "nl_navigation", test_nl_navigation);
    test_suite_add_test(&suite, "nl_misc", test_nl_misc);

    return &suite;
}
