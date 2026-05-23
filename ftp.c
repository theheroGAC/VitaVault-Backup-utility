#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <vita2d.h>

#include "ftp.h"
#include "backup.h"
#include "ui.h"

static int g_net_init = 0;
int g_ftp_active = 0;
static char g_ftp_status[128] = "Waiting for connection...";

void ftp_info_log_cb(const char *msg) {
    strncpy(g_ftp_status, msg, sizeof(g_ftp_status) - 1);
    g_ftp_status[sizeof(g_ftp_status) - 1] = '\0';

    if (strstr(g_ftp_status, "STOR")) {
        char notify_buf[160];
        const char *file_part = strrchr(g_ftp_status, '/');
        if (!file_part) file_part = strrchr(g_ftp_status, ' ');
        
        snprintf(notify_buf, sizeof(notify_buf), "Ricevuto: %s", (file_part) ? file_part + 1 : "nuovo file");
        ui_set_notification(notify_buf);
    }
}

int net_init() {
    if (g_net_init) return 1;

    SceNetInitParam param;
    memset(&param, 0, sizeof(param));
    param.memory = malloc(128 * 1024);
    param.size = 128 * 1024;
    param.flags = 0;

    int ret = sceNetInit(&param);
    if (ret < 0) { free(param.memory); return 0; }

    ret = sceNetCtlInit();
    if (ret < 0) { sceNetTerm(); free(param.memory); return 0; }

    g_net_init = 1;
    return 1;
}

void net_term() {
    if (g_net_init) {
        sceNetCtlTerm();
        sceNetTerm();
        g_net_init = 0;
    }
}

void net_get_local_ip(char *ip, int ip_size) {
    SceNetCtlInfo info;
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0) {
        strncpy(ip, info.ip_address, ip_size);
    } else {
        snprintf(ip, ip_size, "0.0.0.0");
    }
}

int ftp_send_cmd(int sock, const char *cmd, char *response, int resp_size) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    if (n > 0) sceNetSend(sock, buf, n, 0);

    if (response && resp_size > 0) {
        int pos = 0;
        while (pos < resp_size - 1) {
            int r = sceNetRecv(sock, response + pos, resp_size - 1 - pos, 0);
            if (r <= 0) break;
            pos += r;
            if (pos >= 5 && response[pos - 1] == '\n') {
                if (isdigit((unsigned char)response[pos - 5]) &&
                    isdigit((unsigned char)response[pos - 4]) &&
                    isdigit((unsigned char)response[pos - 3]) &&
                    response[pos - 2] == ' ') {
                    break;
                }
                if (pos >= 4 && response[pos - 2] == ' ' &&
                    isdigit((unsigned char)response[pos - 4]) &&
                    isdigit((unsigned char)response[pos - 3])) {
                    break;
                }
            }
        }
        response[pos] = '\0';
    }
    return 1;
}

int ftp_connect(FTPConfig *cfg, int *ctrl_sock, int *data_sock) {
    *ctrl_sock = -1;
    *data_sock = -1;

    SceNetInAddr addr;
    int ret = sceNetInetPton(SCE_NET_AF_INET, cfg->host, &addr);
    if (ret <= 0) {
        
        return 0;
    }

    *ctrl_sock = sceNetSocket("ftp-ctrl", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (*ctrl_sock < 0) return 0;

    SceNetSockaddrIn sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = SCE_NET_AF_INET;
    sin.sin_addr = addr;
    sin.sin_port = sceNetHtons(cfg->port);

    if (sceNetConnect(*ctrl_sock, (SceNetSockaddr *)&sin, sizeof(sin)) < 0) {
        sceNetSocketClose(*ctrl_sock);
        *ctrl_sock = -1;
        return 0;
    }

    char resp[1024];
    ftp_send_cmd(*ctrl_sock, "", resp, sizeof(resp));

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "USER %s", cfg->user);
    ftp_send_cmd(*ctrl_sock, cmd, resp, sizeof(resp));

    snprintf(cmd, sizeof(cmd), "PASS %s", cfg->pass);
    ftp_send_cmd(*ctrl_sock, cmd, resp, sizeof(resp));

    ftp_send_cmd(*ctrl_sock, "TYPE I", resp, sizeof(resp));
    return 1;
}

int ftp_enter_pasv(int ctrl_sock, int *data_sock) {
    char resp[1024];
    ftp_send_cmd(ctrl_sock, "PASV", resp, sizeof(resp));

    char *p = strchr(resp, '(');
    if (!p) return 0;
    p++;

    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return 0;

    *data_sock = sceNetSocket("ftp-data", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (*data_sock < 0) return 0;

    SceNetSockaddrIn sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = SCE_NET_AF_INET;
    sin.sin_addr.s_addr = sceNetHtonl((h1 << 24) | (h2 << 16) | (h3 << 8) | h4);
    sin.sin_port = sceNetHtons(p1 * 256 + p2);

    if (sceNetConnect(*data_sock, (SceNetSockaddr *)&sin, sizeof(sin)) < 0) {
        sceNetSocketClose(*data_sock);
        *data_sock = -1;
        return 0;
    }
    return 1;
}

int ftp_upload_file(int ctrl_sock, int data_sock,
                    const char *local_path, const char *remote_name) {
    SceUID fd = sceIoOpen(local_path, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "STOR %s", remote_name);
    char resp[1024];
    ftp_send_cmd(ctrl_sock, cmd, resp, sizeof(resp));

    if (resp[0] != '1') { sceIoClose(fd); return 0; }

    char buf[16384];
    while (1) {
        int r = sceIoRead(fd, buf, sizeof(buf));
        if (r <= 0) break;
        sceNetSend(data_sock, buf, r, 0);
    }

    sceIoClose(fd);
    sceNetSocketClose(data_sock);

    ftp_send_cmd(ctrl_sock, "", resp, sizeof(resp));
    return 1;
}

int ftp_mkdir(int ctrl_sock, const char *remote_dir) {
    char cmd[512], resp[1024];
    snprintf(cmd, sizeof(cmd), "MKD %s", remote_dir);
    ftp_send_cmd(ctrl_sock, cmd, resp, sizeof(resp));
    return (resp[0] == '2' || resp[0] == '5');
}

int ftp_cwd(int ctrl_sock, const char *remote_dir) {
    char cmd[512], resp[1024];
    snprintf(cmd, sizeof(cmd), "CWD %s", remote_dir);
    ftp_send_cmd(ctrl_sock, cmd, resp, sizeof(resp));
    return (resp[0] == '2');
}

int ftp_upload_directory(int ctrl_sock, const char *local_dir, const char *remote_base,
                         int *total_files, int *done_files,
                         SceOff *total_bytes, SceOff *done_bytes) {

    SceUID dir = sceIoDopen(local_dir);
    if (dir < 0) return -1;

    ftp_mkdir(ctrl_sock, remote_base);
    ftp_cwd(ctrl_sock, remote_base);

    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        char lp[PATH_MAX_SIZE], rp[PATH_MAX_SIZE];
        snprintf(lp, sizeof(lp), "%s/%s", local_dir, ent.d_name);
        snprintf(rp, sizeof(rp), "%s/%s", remote_base, ent.d_name);

        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            ftp_upload_directory(ctrl_sock, lp, rp,
                                 total_files, done_files, total_bytes, done_bytes);
        } else {
            if (total_files) (*total_files)++;
            if (total_bytes) (*total_bytes) += ent.d_stat.st_size;
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);

    dir = sceIoDopen(local_dir);
    if (dir < 0) return 0;

    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        char lp[PATH_MAX_SIZE];
        snprintf(lp, sizeof(lp), "%s/%s", local_dir, ent.d_name);

        if (!SCE_S_ISDIR(ent.d_stat.st_mode)) {
            int data_sock = -1;
            if (ftp_enter_pasv(ctrl_sock, &data_sock) && data_sock >= 0) {
                ftp_upload_file(ctrl_sock, data_sock, lp, ent.d_name);
                if (done_files) (*done_files)++;
                if (done_bytes) (*done_bytes) += ent.d_stat.st_size;
            }
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
    return 0;
}

int ftp_upload_backup(FTPConfig *cfg, const char *backup_path,
                      void (*progress_cb)(int, int, SceOff, SceOff)) {

    if (!net_init()) return 0;

    int ctrl_sock, data_sock;
    if (!ftp_connect(cfg, &ctrl_sock, &data_sock))
        return 0;

    ftp_cwd(ctrl_sock, "/");
    ftp_mkdir(ctrl_sock, cfg->remote_dir);
    ftp_cwd(ctrl_sock, cfg->remote_dir);

    const char *backup_name = strrchr(backup_path, '/');
    if (backup_name) backup_name++; else backup_name = backup_path;

    int total_files = 0;
    SceOff total_bytes = 0;
    SceUID dir = sceIoDopen(backup_path);
    if (dir >= 0) {
        SceIoDirent ent;
        memset(&ent, 0, sizeof(ent));
        while (sceIoDread(dir, &ent) > 0) {
            if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
                memset(&ent, 0, sizeof(ent));
                continue;
            }
            if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
                char sub[PATH_MAX_SIZE];
                snprintf(sub, sizeof(sub), "%s/%s", backup_path, ent.d_name);
                int fc = 0;
                SceOff fs = 0;
                count_files_recursive(sub, &fc, &fs);
                total_files += fc;
                total_bytes += fs;
            } else {
                total_files++;
                total_bytes += ent.d_stat.st_size;
            }
            memset(&ent, 0, sizeof(ent));
        }
        sceIoDclose(dir);
    }

    int done_files = 0;
    SceOff done_bytes = 0;
    ftp_upload_directory(ctrl_sock, backup_path, backup_name,
                         &total_files, &done_files, &total_bytes, &done_bytes);

    ftp_send_cmd(ctrl_sock, "QUIT", NULL, 0);
    if (ctrl_sock >= 0) sceNetSocketClose(ctrl_sock);

    return 1;
}

void ftp_server_run() {
    char ip[32];
    unsigned short int port = 1337;

    if (!g_ftp_active) {
        sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

        ftpvita_set_info_log_cb(ftp_info_log_cb);

        if (ftpvita_init(ip, &port) < 0) return;

        ftpvita_add_device("ux0:");
        ftpvita_add_device("ur0:");
        ftpvita_add_device("uma0:");
        g_ftp_active = 1;
    } else {
        net_get_local_ip(ip, sizeof(ip));
    }

    SceCtrlData pad;
    int running = 1;

    while (running) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        draw_panel(0, 0, 960, 55, COLOR_BG_HEADER);
        draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "FTP Server");
        
        char buf[128];
        snprintf(buf, sizeof(buf), "IP Address: %s", ip);
        draw_text(30, 100, COLOR_TEXT_MAIN, 1.2f, buf);
        snprintf(buf, sizeof(buf), "Port: %d", port);
        draw_text(30, 140, COLOR_TEXT_MAIN, 1.2f, buf);
        
        draw_text(30, 250, COLOR_ACCENT, 1.0f, "Connect with FileZilla using these credentials.");
        
        draw_text(30, 320, COLOR_TEXT_DIM, 0.9f, "Current status:");
        draw_text(30, 350, COLOR_YELLOW, 1.0f, g_ftp_status);

        draw_text(30, 460, COLOR_TEXT_DIM, 0.8f, "Press START to return to menu (FTP stays active).");
        draw_text(30, 490, COLOR_TEXT_DIM, 0.8f, "Press O to stop the server and return to menu.");
        vita2d_end_drawing();
        vita2d_swap_buffers();

        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) {
            running = 0;
        }
        if (pad.buttons & SCE_CTRL_CIRCLE) {
            ftpvita_fini();
            g_ftp_active = 0;
            strcpy(g_ftp_status, "Server stopped.");
            running = 0;
        }
        sceKernelDelayThread(16000);
    }

    sceKernelDelayThread(300000);
}