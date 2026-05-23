/*
    VitaVault - FTP Client Header

*/

#ifndef FTP_H
#define FTP_H

#include <ftpvita.h>
#include "types.h"

extern int g_ftp_active;

int net_init();
void net_term();
void net_get_local_ip(char *ip, int ip_size);

int ftp_send_cmd(int sock, const char *cmd, char *response, int resp_size);
int ftp_connect(FTPConfig *cfg, int *ctrl_sock, int *data_sock);
int ftp_enter_pasv(int ctrl_sock, int *data_sock);
int ftp_upload_file(int ctrl_sock, int data_sock, const char *local_path, const char *remote_name);
int ftp_mkdir(int ctrl_sock, const char *remote_dir);
int ftp_cwd(int ctrl_sock, const char *remote_dir);
int ftp_upload_directory(int ctrl_sock, const char *local_dir, const char *remote_base,
                         int *total_files, int *done_files, SceOff *total_bytes, SceOff *done_bytes);

int ftp_upload_backup(FTPConfig *cfg, const char *backup_path,
                      void (*progress_cb)(int done_files, int total_files,
                                          SceOff done_bytes, SceOff total_bytes));

void ftp_server_run(void);

#endif
