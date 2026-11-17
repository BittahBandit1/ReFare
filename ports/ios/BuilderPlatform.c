// DolRecomp's optional desktop setup/download commands cannot spawn processes
// inside iOS. GameCube C generation uses none of those commands.
#include "platform/process.h"
#include "app/database.h"
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

void sleep_ms(u32 milliseconds) { usleep((useconds_t)milliseconds * 1000u); }
char* shell_quote_arg(const char* argument) { (void)argument; return NULL; }
int run_shell_command(const char* command) {
    (void)command;
    fprintf(stderr, "External commands are unavailable in the iOS builder\n");
    return 0;
}
int command_exists_quiet(const char* command) { (void)command; return 0; }
int prompt_yes_no(const char* prompt) { (void)prompt; return 0; }
int download_file(const char* url, const char* path) {
    (void)url; (void)path;
    fprintf(stderr, "Use the Files importer to provide game data\n");
    return 0;
}

// Disc extraction is performed by DiscImporter.swift, and title lookup/setup
// is never requested by the builder's fixed --gamecube invocation.
int disc_extract_main(int argc, char** argv) {
    (void)argc; (void)argv;
    fprintf(stderr, "Use the Files importer to extract a disc\n");
    return 1;
}
int database_path(char* out, size_t size, const char* name) { (void)out; (void)size; (void)name; return 0; }
int database_titles_path(char* out, size_t size) { (void)out; (void)size; return 0; }
int database_titles_available(void) { return 0; }
void print_database_missing_notice(void) {}
int download_titles_database(const char* path) { (void)path; return 0; }
int resolve_game_name(const char* id, char* out, size_t size) { (void)id; (void)out; (void)size; return 0; }
void describe_game(char* out, size_t size, const char* id, int gamecube) {
    (void)id; (void)gamecube;
    snprintf(out, size, "GameCube DOL");
}
