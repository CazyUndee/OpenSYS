#ifndef NL_PARSER_H
#define NL_PARSER_H

/*
 * nl_parser.h — Natural-language phrase parser for the Plan 0 shell.
 *
 * The shell reads like natural language but stays structured underneath.
 * This module translates English-like phrases into the canonical
 * (command, arg1, arg2) triple the structured dispatcher in shell.c
 * already knows how to execute:
 *
 *   "list files in documents"          -> list, documents, ""
 *   "list current directory"           -> list, "", ""          (cwd = root)
 *   "create a file called notes.txt"   -> create, notes.txt, ""
 *   "write hello world to notes.txt"   -> write, notes.txt, "hello world"
 *   "delete the file notes.txt"        -> delete, notes.txt, ""
 *   "copy a.txt to b.txt"              -> copy, a.txt, b.txt
 *   "show me the memory"               -> show, memory, ""
 *   "go up"                            -> up, "", ""
 *
 * Vocabulary matching is case-insensitive; extracted arguments keep their
 * original case (the filesystem is case-sensitive).
 *
 * Returns 0 on success (cmd/arg1/arg2 are filled) or -1 when the phrase is
 * not recognized or is ambiguous — the caller then falls back to the plain
 * token-based parser, so existing syntax like "write a.txt hello" keeps
 * working unchanged.  The input buffer may be modified temporarily but is
 * restored before returning.
 */

int nl_parse(char* input, char* cmd, char* arg1, char* arg2);

#endif
