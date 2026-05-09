#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

#define MY_PORT 50002 // mirror2 port
#define MX_DIRS 2000

char t_paths[MX_DIRS][512]; // saves dirs full paths
time_t t_mtimes[MX_DIRS];   // saves modification time

void handle_dirlist_a(int client_sd) { // for dirlist a
    const char *home = getenv("HOME"); // home dir path
    char shl_cmd[512];
    char line[512]; // buff to read each line of shell output
    int count_lines = 0; // count of lines streamed to client

    // shell cmnd to find all directories under home, sorted alphabetically
    snprintf(shl_cmd, sizeof(shl_cmd), "find %s -mindepth 1 -type d | sort", home);

    FILE *fp = popen(shl_cmd, "r"); // open a pipe to the shell cmnd
    if (fp == NULL) {
        write(client_sd, "Error running dirlist -a.", 25);
        return;
    }

    // stream each directory path directly to client instead of buffering
    while (fgets(line, sizeof(line), fp) != NULL) {
        int n = strlen(line);
        if (n == 0) continue;
        write(client_sd, line, n); // stream line directly to client
        count_lines++;
    }

    pclose(fp); // close pipe and wait for the shell to end

    if (count_lines == 0) {
        write(client_sd, "No directories/subdirectories found.", 36);
    }
}

void handle_dirlist_t(int client_sd) { // for dirlist t
    const char *home = getenv("HOME"); // home dir path
    char shl_cmd[512];
    char line[512]; // buff to read each line of shell output
    struct stat st;
    int count = 0;
    int i, j;

    // shell cmnd to find all directories under home
    snprintf(shl_cmd, sizeof(shl_cmd), "find %s -mindepth 1 -type d", home);

    FILE *fp = popen(shl_cmd, "r");
    if (fp == NULL) {
        write(client_sd, "Error running dirlist -t.", 25);
        return;
    }

    // read each directory path from find output
    while (fgets(line, sizeof(line), fp) != NULL && count < MX_DIRS) {
        line[strcspn(line, "\n")] = '\0'; // strip the trailing newline fgets includes
        if (strlen(line) == 0) continue;  // skip blank lines if any

        if (stat(line, &st) == 0) { // stat gives st with file metadata
            strncpy(t_paths[count], line, sizeof(t_paths[count]) - 1);
            t_paths[count][sizeof(t_paths[count]) - 1] = '\0';
            t_mtimes[count] = st.st_mtime; // store modification time
            count++;
        }
    }

    pclose(fp);

    if (count == 0) {
        write(client_sd, "No directories/subdirectories found.", 36);
        return;
    }

    // bubble sort on mod time
    for (i = 0; i < count - 1; i++) {
        for (j = 0; j < count - 1 - i; j++) {
            if (t_mtimes[j] > t_mtimes[j + 1]) {
                time_t tmp_t = t_mtimes[j]; // swap modification times
                t_mtimes[j] = t_mtimes[j + 1];
                t_mtimes[j + 1] = tmp_t;

                char tmp_p[512]; // swap corresponding paths
                strcpy(tmp_p, t_paths[j]);
                strcpy(t_paths[j], t_paths[j + 1]);
                strcpy(t_paths[j + 1], tmp_p);
            }
        }
    }

    // stream sorted paths directly to client
    for (i = 0; i < count; i++) {
        char line_buf[514]; // path up to 512 + newline + null
        int n = snprintf(line_buf, sizeof(line_buf), "%s\n", t_paths[i]); // format line
        write(client_sd, line_buf, n); // stream each path to client
    }
}

// search for filename recursively under base path
int search_file(int client_sd, const char *base_path, const char *filename) {
    DIR *dp = opendir(base_path);
    if (dp == NULL) return 0; // skip directories if can't open

    struct dirent *entry; // for iterating directory entries
    struct stat st; // for getting file meta data
    char full_path[1024]; 
    int found_f = 0; // if set to 1, file found and stop search

    // reads on item from dp until no more entries or file found
    while ((entry = readdir(dp)) != NULL && !found_f) { // check until found or no more entries
        // skip parent and current directory to avoid infinity loop
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name); // build full path of this entry

        if (stat(full_path, &st) != 0) continue; // skip if stat fails

        if (S_ISDIR(st.st_mode)) { // for directories to recurse
            // recurse into subdirectory, if file found thn stop searching
            found_f = search_file(client_sd, full_path, filename);
        } else if (S_ISREG(st.st_mode) && strcmp(entry->d_name, filename) == 0) { // for regular files

            char rspns[2048]; // respond to client with info

            char prmsn[10]; // get permission string
            prmsn[0] = (st.st_mode & S_IRUSR) ? 'r' : '-';
            prmsn[1] = (st.st_mode & S_IWUSR) ? 'w' : '-';
            prmsn[2] = (st.st_mode & S_IXUSR) ? 'x' : '-';
            prmsn[3] = (st.st_mode & S_IRGRP) ? 'r' : '-';
            prmsn[4] = (st.st_mode & S_IWGRP) ? 'w' : '-';
            prmsn[5] = (st.st_mode & S_IXGRP) ? 'x' : '-';
            prmsn[6] = (st.st_mode & S_IROTH) ? 'r' : '-';
            prmsn[7] = (st.st_mode & S_IWOTH) ? 'w' : '-';
            prmsn[8] = (st.st_mode & S_IXOTH) ? 'x' : '-';
            prmsn[9] = '\0';

            char *date_str = ctime(&st.st_mtime); // convert st_mtime to string

            snprintf(rspns, sizeof(rspns),
                "Filename: %s\n"
                "Size: %ld bytes\n"
                "Permissions: %s\n"
                "Date Modified: %s",
                filename, (long)st.st_size, prmsn, date_str);

            write(client_sd, rspns, strlen(rspns)); // respond to client
            found_f = 1; // to stop the loop
        }
    }
    closedir(dp);
    return found_f;
}

void handle_fn(int client_sd, const char *filename) { // for fn command
    const char *home = getenv("HOME"); // start search home dir
    int found_f = search_file(client_sd, home, filename);
    if (!found_f) {
        write(client_sd, "File not found", 14); // if found returns 0
    }
}

// check dir_path recursively and write matching file paths
void collect_fz_files(const char *dir_path, long sz1, long sz2, FILE *list_fp, int *count_f) {
    DIR *dp = opendir(dir_path); // start with home dir
    if (dp == NULL) return; // skip directories if cant be opened

    struct dirent *entry; // for iterating directory entries
    struct stat st; // for getting file meta data
    char full_path[1024]; // to build full path of each entry

    // read each entry in this directory
    while ((entry = readdir(dp)) != NULL) {
        // skip parent and current directory to avoid infinity loop
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name); // build full path of this entry

        if (stat(full_path, &st) != 0) continue; // skip if stat fails

        if (S_ISDIR(st.st_mode)) {
            collect_fz_files(full_path, sz1, sz2, list_fp, count_f); // recurse into subdirectory
        } else if (S_ISREG(st.st_mode)) { // for regular files
            if (st.st_size >= sz1 && st.st_size <= sz2) { // check if size in range
                fprintf(list_fp, "%s\n", full_path); // write path to the list file
                (*count_f)++;
            }
        }
    }

    closedir(dp);
}

void handle_fz(int client_sd, long sz1, long sz2) { // for fz command
    const char *home = getenv("HOME"); // start with home dir
    FILE *list_fp = fopen("/tmp/fz_list.txt", "w"); // open temp file for writing file path
    if (list_fp == NULL) {
        write(client_sd, "No files found", 14); // if fopen fails
        return;
    }

    int count_f = 0; // count of matched file
    collect_fz_files(home, sz1, sz2, list_fp, &count_f); // collect matched files recursively
    fclose(list_fp);

    if (count_f == 0) { // if no files matched
        unlink("/tmp/fz_list.txt"); // clean the temp file
        write(client_sd, "No files found", 14); 
        return;
    }

    char proj_dir[512]; // create project dirc if not there
    snprintf(proj_dir, sizeof(proj_dir), "%s/project", home);
    // mkdir(proj_dir, 0755); // returns -1 with EEXIST if already there and ignore

    char tar_path[1024]; // path for the tar archive
    snprintf(tar_path, sizeof(tar_path), "%s/fz_temp.tar.gz", proj_dir);


    pid_t tar_pid = fork(); // fork a child to run tar
    if (tar_pid < 0) {
        unlink("/tmp/fz_list.txt"); // if fork failed
        write(client_sd, "No files found", 14);
        return;
    }

    if (tar_pid == 0) { // inside child
        execlp("tar", "tar", "-czf", tar_path, "-T", "/tmp/fz_list.txt", NULL);
        perror("execlp tar"); // only return here if exec fails
        exit(EXIT_FAILURE);
    }

    // parent wait for tar child to finish
    int tar_status; // exit status of tar proc
    waitpid(tar_pid, &tar_status, 0); // get exit status
    unlink("/tmp/fz_list.txt"); // clean up temp file

    if (!WIFEXITED(tar_status) || WEXITSTATUS(tar_status) != 0) {
        write(client_sd, "No files found", 14); // if tar failed
        return;
    }

    char msg_clnt[1024]; // to send response to client
    snprintf(msg_clnt, sizeof(msg_clnt), "fz_temp.tar.gz saved to %s", proj_dir);
    write(client_sd, msg_clnt, strlen(msg_clnt)); // send response to client
}

// check dir_path recursively to write paths of files whose ext matches
void collect_ft_files(const char *dir_path, char ext_list[][32], int ext_cnt, FILE *list_fp, int *count_f) {
    DIR *dp = opendir(dir_path); // start with home dir
    if (dp == NULL) return; // skip directories if cant access

    struct dirent *entry; // for iterating directory entries
    struct stat st;
    char full_path[1024];
    // read each entry in this directory
    while ((entry = readdir(dp)) != NULL) {
        // skip parent and current directory to avoid infinity loop
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name); // build full path of this entry

        if (stat(full_path, &st) != 0) continue; // skip if stat fails

        if (S_ISDIR(st.st_mode)) { // for directories to recurse
            collect_ft_files(full_path, ext_list, ext_cnt, list_fp, count_f);
        } else if (S_ISREG(st.st_mode)) { // for regular files
            // find the last dot in the fname to get ext
            char *dot = strrchr(entry->d_name, '.');
            if (dot == NULL) continue; // no ext then skip

            int i; // check if this ext matches any in the list
            for (i = 0; i < ext_cnt; i++) {
                if (strcmp(dot + 1, ext_list[i]) == 0) {
                    fprintf(list_fp, "%s\n", full_path); // write this path to list file if ext matches
                    (*count_f)++; // incr count of matched files
                    break; // no need to check remaining ext for this file
                }
            }
        }
    }

    closedir(dp);
}

void handle_ft(int client_sd, const char *ext_arg) { // for ft command
    const char *home = getenv("HOME"); // start with home dir

    char ext_list[3][32]; // at most 3 extensions, each up to 31 chars
    int ext_cnt = 0; // count of valid extensions parsed
    char arg_copy[256]; // copy of ext_arg to tokenize since strtok modifies the string
    strncpy(arg_copy, ext_arg, sizeof(arg_copy) - 1); // copy and ensure null termination
    arg_copy[sizeof(arg_copy) - 1] = '\0'; // tokenize by space to get each extension

    char *token = strtok(arg_copy, " "); // get first token
    while (token != NULL && ext_cnt < 3) {
        strncpy(ext_list[ext_cnt], token, sizeof(ext_list[ext_cnt]) - 1);
        ext_list[ext_cnt][sizeof(ext_list[ext_cnt]) - 1] = '\0';
        ext_cnt++;
        token = strtok(NULL, " "); // get next token
    }

    if (ext_cnt == 0) { // if no valid ext parsed
        write(client_sd, "No files found", 14);
        return;
    }

    FILE *list_fp = fopen("/tmp/ft_list.txt", "w"); // open tmp file for listing
    if (list_fp == NULL) {
        write(client_sd, "No files found", 14); // if fopen failss
        return;
    }

    int count_f = 0;
    collect_ft_files(home, ext_list, ext_cnt, list_fp, &count_f);
    fclose(list_fp);

    if (count_f == 0) {
        unlink("/tmp/ft_list.txt"); // remove the temp list file
        write(client_sd, "No files found", 14);
        return;
    }

    char proj_dir[512];
    snprintf(proj_dir, sizeof(proj_dir), "%s/project", home); //
    // mkdir(proj_dir, 0755); // ensure project folder exists

    char tar_path[1024];
    snprintf(tar_path, sizeof(tar_path), "%s/ft_temp.tar.gz", proj_dir);

    pid_t tar_pid = fork(); // fork a child to run tar
    if (tar_pid < 0) {
        unlink("/tmp/ft_list.txt"); // remove the temp list file
        write(client_sd, "No files found", 14); // if fork fails
        return;
    }

    if (tar_pid == 0) { // inside child to run tar
        execlp("tar", "tar", "-czf", tar_path, "-T", "/tmp/ft_list.txt", NULL);
        perror("execlp tar"); // only returns if exec fails
        exit(EXIT_FAILURE);
    }

    int tar_status; // parent waits for tar child to finish
    waitpid(tar_pid, &tar_status, 0); // get tar exit status

    unlink("/tmp/ft_list.txt"); // delete temp list file

    if (!WIFEXITED(tar_status) || WEXITSTATUS(tar_status) != 0) { // if tar failed
        write(client_sd, "No files found", 14);
        return;
    }

    char msg_clnt[1024];
    snprintf(msg_clnt, sizeof(msg_clnt), "ft_temp.tar.gz saved to %s", proj_dir); // respond to client
    write(client_sd, msg_clnt, strlen(msg_clnt)); // send response to client
}

// helper for fdb and fda for tar logic
static int run_tar_from_list(const char *tar_path, const char *list_file) {
    pid_t tar_pid = fork(); // fork a child to run tar
    if (tar_pid < 0) {
        unlink(list_file); // if tar fails clean temp file
        return 0;
    }

    if (tar_pid == 0) { // child to run tar
        execlp("tar", "tar", "-czf", tar_path, "-T", list_file, NULL);
        perror("execlp tar"); // returns if exec fails
        exit(EXIT_FAILURE);
    }

    int tar_status; // exit status of tar proc
    waitpid(tar_pid, &tar_status, 0); // get tar exit status
    unlink(list_file); // clean tmp file

    return WIFEXITED(tar_status) && WEXITSTATUS(tar_status) == 0; // return 1 if tar succeeded else 0
}

void handle_fdb(int client_sd, const char *date) { // handle fdb command
    const char *home = getenv("HOME"); // home dir path
    char proj_dir[512]; // project dir to exclude from search
    snprintf(proj_dir, sizeof(proj_dir), "%s/project", home);

    char find_cmd[1024]; // list regular files modified on or before given date
    // to get <=date used -not -newermt date 
    snprintf(find_cmd, sizeof(find_cmd), "find %s -path '%s' -prune -o -type f ! -newermt %s -print", home, proj_dir, date);

    FILE *fp = popen(find_cmd, "r"); // open a pipe to run the find command
    if (fp == NULL) {
        write(client_sd, "No files found", 14); // if popen fail
        return;
    }

    FILE *list_fp = fopen("/tmp/fdb_list.txt", "w"); // open tmp file to write matched files
    if (list_fp == NULL) {
        pclose(fp);
        write(client_sd, "No files found", 14); // if fopen fail
        return;
    }

    char line[1024]; // buff to read each line of find output
    int count_m = 0; // count of matched files
    while (fgets(line, sizeof(line), fp) != NULL) {
        // fgets includes the newline, so strip it before writing to list file
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;
        fprintf(list_fp, "%s\n", line); // write this matched file path to list file
        count_m++;
    }
    pclose(fp);
    fclose(list_fp);

    if (count_m == 0) {
        unlink("/tmp/fdb_list.txt"); // if no files matched, clean tmp file
        write(client_sd, "No files found", 14); // respond to client
        return;
    }

    // mkdir(proj_dir, 0755); // ensure project dir exists

    char tar_path[1024]; // build tar archive path
    snprintf(tar_path, sizeof(tar_path), "%s/fdb_temp.tar.gz", proj_dir); // create tar archive from the list of matched files

    if (!run_tar_from_list(tar_path, "/tmp/fdb_list.txt")) { // if tar failed
        write(client_sd, "No files found", 14);
        return;
    }

    char msg_clnt[1024];
    snprintf(msg_clnt, sizeof(msg_clnt), "fdb_temp.tar.gz saved to %s", proj_dir);
    write(client_sd, msg_clnt, strlen(msg_clnt)); // respond to client with success msg
}

void handle_fda(int client_sd, const char *date) { // handle fda command
    const char *home = getenv("HOME"); // home dir path
    char proj_dir[512]; // project dir to exclude from search
    snprintf(proj_dir, sizeof(proj_dir), "%s/project", home);

    char find_cmd[1024]; // list regular files modified on or before given date
    // to get >date used -newermt date
    snprintf(find_cmd, sizeof(find_cmd), "find %s -path '%s' -prune -o -type f -newermt %s -print", home, proj_dir, date);

    FILE *fp = popen(find_cmd, "r"); // open a pipe to run the find command
    if (fp == NULL) {
        write(client_sd, "No files found", 14); // if popen fails
        return;
    }

    FILE *list_fp = fopen("/tmp/fda_list.txt", "w"); // open tmp file to write matched files
    if (list_fp == NULL) {
        pclose(fp);
        write(client_sd, "No files found", 14); // if fopen fail
        return;
    }

    char line[1024]; // buff to read each line of find output
    int count_m = 0; // count of matched files
    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;
        fprintf(list_fp, "%s\n", line); // write this matched file path to list file
        count_m++;
    }
    pclose(fp);
    fclose(list_fp);

    if (count_m == 0) {
        unlink("/tmp/fda_list.txt"); // if no files matched, clean tmp file
        write(client_sd, "No files found", 14); // respond to client
        return;
    }

    // mkdir(proj_dir, 0755); // ensure project dir exists

    char tar_path[1024]; // build tar archive path
    snprintf(tar_path, sizeof(tar_path), "%s/fda_temp.tar.gz", proj_dir);

    if (!run_tar_from_list(tar_path, "/tmp/fda_list.txt")) { // if tar failed
        write(client_sd, "No files found", 14);
        return;
    }

    char msg_clnt[1024];
    snprintf(msg_clnt, sizeof(msg_clnt), "fda_temp.tar.gz saved to %s", proj_dir);
    write(client_sd, msg_clnt, strlen(msg_clnt)); // respond to client with success msg
}

// crequest handles all requests from a single connected client
void crequest(int client_sd) {
    char cmnds[1024]; // buffer to read client command into
    int bytes_read; // num of bytes read from client

    signal(SIGCHLD, SIG_DFL); // to prevent zombie processses
    while (1) {
        memset(cmnds, 0, sizeof(cmnds));

        bytes_read = read(client_sd, cmnds, sizeof(cmnds) - 1); // read client command
        if (bytes_read <= 0) {
            break;
        }
        cmnds[bytes_read] = '\0'; // null terminate the command string

        // for calling handlers based on client reqs
        if (strcmp(cmnds, "quitc") == 0) {
            printf("Client sent quitc. Closing connection.\n");
            break;
        } else if (strcmp(cmnds, "dirlist -a") == 0) {
            handle_dirlist_a(client_sd);
        } else if (strcmp(cmnds, "dirlist -t") == 0) {
            handle_dirlist_t(client_sd);
        } else if (strncmp(cmnds, "fn ", 3) == 0) {
            handle_fn(client_sd, cmnds + 3);
        } else if (strncmp(cmnds, "fz ", 3) == 0) {
            long sz1, sz2;
            if (sscanf(cmnds + 3, "%ld %ld", &sz1, &sz2) == 2) {
                handle_fz(client_sd, sz1, sz2);
            } else {
                write(client_sd, "No files found", 14);
            }
        } else if (strncmp(cmnds, "ft ", 3) == 0) {
            handle_ft(client_sd, cmnds + 3);
        } else if (strncmp(cmnds, "fdb ", 4) == 0) {
            handle_fdb(client_sd, cmnds + 4);
        } else if (strncmp(cmnds, "fda ", 4) == 0) {
            handle_fda(client_sd, cmnds + 4);
        } else {
            char *reply = "Unknown command.";
            write(client_sd, reply, strlen(reply));
        }
        write(client_sd, "\0", 1); // client can read until null byte to get full response
    }
    close(client_sd);
    exit(0);
}

int main(void) {
    int lis_sd, con_sd;
    int opt = 1; // for setsockopt to reuse address
    struct sockaddr_in servAdd;

    signal(SIGCHLD, SIG_IGN); // to prevent zombie processes by ignoring SIGCHLD

    if ((lis_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Could not create socket\n");
        exit(1);
    }

    setsockopt(lis_sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // allow reuse of port

    memset(&servAdd, 0, sizeof(servAdd));
    servAdd.sin_family = AF_INET;
    servAdd.sin_addr.s_addr = htonl(INADDR_ANY);
    servAdd.sin_port = htons((uint16_t)MY_PORT); // set port for mirror2

    if (bind(lis_sd, (struct sockaddr *)&servAdd, sizeof(servAdd)) < 0) {
        fprintf(stderr, "bind() failed\n");
        exit(1);
    }

    listen(lis_sd, 10);
    printf("mirror2 started. Listening on port %d...\n", MY_PORT);

    while (1) {
        con_sd = accept(lis_sd, (struct sockaddr *)NULL, NULL);
        if (con_sd < 0) {
            perror("accept");
            continue;
        }

        printf("Client connected to mirror2.\n");

        int pid = fork(); // fork child proc to handle this client
        if (pid < 0) {
            perror("fork");
            close(con_sd);
            continue;
        }

        if (pid == 0) { // child proc to handle this client
            close(lis_sd); // close listening socket in child proc
            crequest(con_sd); // handle client requests in this child proc
        }

        close(con_sd);
    }

    close(lis_sd);
    return 0;
}
