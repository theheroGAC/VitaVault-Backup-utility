#ifndef GAME_BACKUP_H
#define GAME_BACKUP_H

#include "types.h"

void init_games_array();
void cleanup_games_array();
int expand_games_array();

int scan_games(void);
int backup_game(int game_index, const char *backup_root, ProgressCallback cb);

#endif
