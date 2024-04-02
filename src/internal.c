#include "internal.h"

void log_message(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);

    time_t now = time(NULL);
    char* time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';

    const char* color_red = "\033[1;31m";
    const char* color_blue = "\033[1;34m";
    const char* color_yellow = "\033[1;33m";
    const char* color_green = "\033[1;32m";
    const char* color_reset = "\033[0m";

    const char* level_strs[] = {"INFO", "END", "DEBUG", "UPDATE"};
    const char* level_colors[] = {color_blue, color_red, color_yellow, color_green};

    printf("%s[%s] [%s]%s ", level_colors[level], time_str, level_strs[level], color_reset);
    vprintf(format, args);
    printf("\n");

    va_end(args);
}


bool launch_processes() {
    // sem_t *sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    // if (sem == SEM_FAILED) {
    //     //log_message(LOG_ERROR, "sem_open failed: %s", strerror(errno));
    //     return false;
    // }
    ptedit_init();
    int pids[NUMBER_OF_PROCESSES];
    for(int i=0; i < NUMBER_OF_PROCESSES; i++) {
        pids[i] = fork();
        if(pids[i] < 0) {
            // sem_close(sem);
            // sem_unlink(SEM_NAME);
            //log_message(LOG_ERROR, "fork failed: %s", strerror(errno));
            return false;
        } else if (pids[i] == 0) {
            log_message(LOG_UPDATE, "Process %d created", getpid());
            // sem_wait(sem);
            log_message(LOG_UPDATE, "Process %d acquired pteditor lock", getpid());
            // if (ptedit_init()) {
            //     //log_message(LOG_ERROR, "error ptedit init");
            //     // sem_post(sem);
            //     return 1;
            // }
            //log_message(LOG_DEBUG, "Starting writing tests...\n");
            if(!demo_read_write()){
                //log_message(LOG_ERROR, "write failed: %s", strerror(errno)); 
                // sem_post(sem);
                exit(EXIT_FAILURE);

            }
            // sem_post(sem);
            log_message(LOG_UPDATE, "Process %d released pteditor lock", getpid());
            exit(EXIT_SUCCESS);
        }
    }

    int status;
    for(int i=0; i < NUMBER_OF_PROCESSES; i++) {
        waitpid(pids[i], &status, 0);
        if(WIFEXITED(status)) {
            //log_message(LOG_INFO, "Child process %d returned with status %d\n", pids[i], WEXITSTATUS(status));
        } else if(WIFSIGNALED(status)) {
            //log_message(LOG_INFO,"Child process %d was terminated by a signal %d - ", pids[i], WTERMSIG(status));
            if(WTERMSIG(status) == SIGSEGV) {
                //log_message(LOG_INFO, "That was due a segmentation fault\n");
            }
        }
    }

    // sem_close(sem);
    // sem_unlink(SEM_NAME);
    //log_message(LOG_INFO, "All child processes have finished\n");
    ptedit_cleanup();
    return true;
}

bool ensure_directory_exists(const char* dir_path) {
    struct stat st = {0};
    if (stat(dir_path, &st) == -1) {
        if (mkdir(dir_path, 0755) == -1) {
            perror("Failed to create directory");
            return false;
        }
    }
    return true;
}

bool psar_write(char *mapped_region, off_t offset, const char *data, size_t len, size_t region_size, char * file_name) {
    if (offset + len > region_size) {
        //log_message(LOG_ERROR, "Write operation exceeds mapped region bounds.");
        return false;
    }

    char log_dir_path[256];
    snprintf(log_dir_path, sizeof(log_dir_path), "logs/logs_%d", getpid());

    
    if (!ensure_directory_exists(log_dir_path)) {
        return false;
    }

    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
    char log_file_path[512];
    snprintf(log_file_path, sizeof(log_file_path), "%s/log_%s_%s.log", log_dir_path, file_name, timestamp);

    
    int log_fd = open(log_file_path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (log_fd == -1) {
        //log_message(LOG_ERROR, "Failed to open log file: %s", strerror(errno));
        return false;
    }

    dprintf(log_fd, "Offset: %ld, Length: %zu, Data: ", (long)offset, len);
    write(log_fd, data, len);
    write(log_fd, "\n", 1);

    close(log_fd);

    memcpy(mapped_region + offset, data, len);
    log_message(LOG_UPDATE, "Process %d logged %s", getpid(), log_file_path);
    return true;
}

bool merge(const char* original_file_path, const char* log_file_path) {
    int original_fd = open(original_file_path, O_RDONLY);
    if (original_fd == -1) {
        perror("Failed to open original file");
        return false;
    }

    int log_fd = open(log_file_path, O_RDONLY);
    if (log_fd == -1) {
        perror("Failed to open log file");
        close(original_fd);
        return false;
    }

    char merged_file_path[512];
    const char* original_file_name = strrchr(original_file_path, '/');
    if (!original_file_name) original_file_name = original_file_path;
    else original_file_name++;
    const char* log_file_name = strrchr(log_file_path, '/');
    if (!log_file_name) log_file_name = log_file_path;
    else log_file_name++;

    char merge_dir_path[256];
    snprintf(merge_dir_path, sizeof(merge_dir_path), "merge/merge_%s", original_file_name);
    if (!ensure_directory_exists(merge_dir_path)) {
        close(original_fd);
        close(log_fd);
        return false;
    }
    
    snprintf(merged_file_path, sizeof(merged_file_path), "%s/%s_%s", merge_dir_path, original_file_name, log_file_name);

    int merged_fd = open(merged_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (merged_fd == -1) {
        perror("Failed to create merged file");
        close(original_fd);
        close(log_fd);
        return false;
    }

    char buffer[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(original_fd, buffer, sizeof(buffer))) > 0) {
        write(merged_fd, buffer, bytes_read);
    }

    // Apply merge between log file and target file.
    char log_line[1024];
    ssize_t read_size;
    lseek(log_fd, 0, SEEK_SET);
    while ((read_size = read(log_fd, log_line, sizeof(log_line) - 1)) > 0) {
        log_line[read_size] = '\0';
        char* ptr = log_line;
        while (ptr < log_line + read_size) {
            char* end_ptr = strchr(ptr, '\n');
            if (!end_ptr) break;
            *end_ptr = '\0';
            off_t offset;
            size_t len;
            char data[512];
            if (sscanf(ptr, "Offset: %ld, Length: %zu, Data: %[^\n]", &offset, &len, data) == 3) {
                lseek(merged_fd, offset, SEEK_SET);
                write(merged_fd, data, strlen(data));
            }
            ptr = end_ptr + 1;
        }
    }

    close(original_fd);
    close(log_fd);
    close(merged_fd);
    log_message(LOG_UPDATE, "merge created for file %s", original_file_name);
    return true;
}


bool merge_all(const char* original_file_path) {
    int original_fd = open(original_file_path, O_RDONLY);
    if (original_fd == -1) {
        perror("Failed to open original file");
        return false;
    }

    const char* original_file_name = strrchr(original_file_path, '/');
    if (!original_file_name) original_file_name = original_file_path;
    else original_file_name++;

    char merge_all_path[256];
    snprintf(merge_all_path, sizeof(merge_all_path), "merge/merge_all_%s", original_file_name);

    int merged_fd = open(merge_all_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (merged_fd == -1) {
        perror("Failed to create merge_all file");
        close(original_fd);
        return false;
    }

    char buffer[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(original_fd, buffer, sizeof(buffer))) > 0) {
        if (write(merged_fd, buffer, bytes_read) != bytes_read) {
            perror("Failed to write to merge_all file");
            close(original_fd);
            close(merged_fd);
            return false;
        }
    }

    close(original_fd); // Close the original file as its contents are fully copied

    DIR *dir;
    struct dirent *ent;
    char log_folder_path[] = "logs/";
    if ((dir = opendir(log_folder_path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            char* log_file_name = ent->d_name;
            // Check if the log file name contains the original file name
            if (strstr(log_file_name, original_file_name) != NULL) {
                char log_file_path[256];
                snprintf(log_file_path, sizeof(log_file_path), "%s%s", log_folder_path, log_file_name);

                int log_fd = open(log_file_path, O_RDONLY);
                if (log_fd == -1) {
                    perror("Failed to open log file");
                    continue; // Skip to next log file
                }

                // Apply merge between log file and merge_all file
                char log_line[1024];
                ssize_t read_size;
                lseek(log_fd, 0, SEEK_SET);
                while ((read_size = read(log_fd, log_line, sizeof(log_line) - 1)) > 0) {
                    log_line[read_size] = '\0';
                    char* ptr = log_line;
                    while (ptr < log_line + read_size) {
                        char* end_ptr = strchr(ptr, '\n');
                        if (!end_ptr) break;
                        *end_ptr = '\0';
                        off_t offset;
                        size_t len;
                        char data[512];
                        if (sscanf(ptr, "Offset: %ld, Length: %zu, Data: %[^\n]", &offset, &len, data) == 3) {
                            lseek(merged_fd, offset, SEEK_SET);
                            write(merged_fd, data, strlen(data));
                        }
                        ptr = end_ptr + 1;
                    }
                }
                close(log_fd);
            }
        }
        closedir(dir);
    } else {
        perror("Failed to open logs directory");
    }

    close(merged_fd); // Ensure the merged file is closed properly
    return true; // Indicate successful completion
}

bool demo_read_write() {
    log_message(LOG_UPDATE, "Process %d started reading and write routine", getpid());
    int fd[NUMBER_OF_FILES];
    char file_name[FILE_NAME_SIZE];
    int i = 0;
    for(; i < NUMBER_OF_FILES; i++) {
        snprintf(file_name, FILE_NAME_SIZE, "files/file_%d", i);
        fd[i] = open(file_name, O_RDONLY);
        
        if(fd[i] == -1) {
            //log_message(LOG_ERROR, "file descriptor failed: %s", strerror(errno));
            return false;
        }

        struct stat st;
        if(fstat(fd[i], &st) == -1) {
            //log_message(LOG_ERROR, "fstat failed: %s", strerror(errno));
            close(fd[i]);
            return false;
        }
        char * mapped_region = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd[i], 0);
        if(mapped_region == MAP_FAILED) {
            //log_message(LOG_ERROR, "mmap failed: %s", strerror(errno));
            close(fd[i]);
            return false;
        }
        
        //log_message(LOG_INFO, "Process %d reads %s before write: [%s]\n", getpid(), file_name, mapped_region);
        // const char * testing = "WOW";
        psar_write(mapped_region, WRITE_OFFSET, WRITE_DEMO, strlen(WRITE_DEMO), st.st_size, file_name);
        //log_message(LOG_INFO, "Process %d reading new mapped region [%s]\n", getpid(), mapped_region);
        munmap(mapped_region, st.st_size);
        close(fd[i]);
    }
    log_message(LOG_UPDATE, "Process %d modified %d files", getpid(), i);
    return true;
}

void setup() {
    ensure_directories_exist();
    create_files();
    add_content_files();
    setupSignalHandler();
}

void ensure_directories_exist() {
    const char* directories[] = {"logs", "merge", "files"};
    size_t num_directories = sizeof(directories) / sizeof(directories[0]);
    for (size_t i = 0; i < num_directories; ++i) {
        struct stat st = {0};
        if (stat(directories[i], &st) == -1) {
            if (mkdir(directories[i], 0755) == -1) {
                perror("Failed to create directory");
            }
        }
    }
    log_message(LOG_UPDATE, "Folders log, merge and files created");
}

bool create_files() {
    char file_name[FILE_NAME_SIZE];
    int fd;

    // if(!check_directory()) return false;
    int i = 0;
    for(; i < NUMBER_OF_FILES; i++) {
        snprintf(file_name, FILE_NAME_SIZE, "files/file_%d", i);
        fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMISSIONS);
        if(fd == -1) {
            //log_message(LOG_ERROR, "open failed: %s", strerror(errno));
            return false;
        }
        close(fd);
    }
    log_message(LOG_UPDATE, "%d files were created", i);
    return true;
}

bool add_content_files() {
    char file_name[FILE_NAME_SIZE];
    int fd;
    // if(!check_directory()) return false;
    for(int i=0; i < NUMBER_OF_FILES; i++) {
        snprintf(file_name, FILE_NAME_SIZE, "files/file_%d", i);
        fd = open(file_name, O_WRONLY, FILE_PERMISSIONS);
        if(fd == -1) {
            //log_message(LOG_ERROR, "open failed: %s", strerror(errno));
            return false;
        }
        ssize_t bytes = write(fd, DATA_DEMO, strlen(DATA_DEMO));        
        if(bytes == -1) {
            //log_message(LOG_ERROR, "write failed: %s", strerror(errno));
            return false;
        }
        close(fd);
    }
    log_message(LOG_UPDATE, "Data written to files");
    return true;
}

bool check_directory() {
    if(mkdir(TEST_FILE_FOLDER, 0755)) {
        if(errno != EEXIST) {
            //log_message(LOG_ERROR, "mkdir failed: %s", strerror(errno));
            return false;
        }
    }
    return true;
}

void log_virtual_to_physical(void* address) {
    ptedit_entry_t entry = ptedit_resolve(address, 0);
    size_t pfn = ptedit_get_pfn(entry.pte);
    //log_message(LOG_INFO, "Virtual address %p -> Physical Frame Number (PFN): %zu\n", address, pfn);
}

void* align_to_page_boundary(void* address) {
    return (void*)((size_t)address & ~(PAGE_SIZE - 1));
}

void signalHandler(int sig, siginfo_t * si, void * unused) {
    //log_message(LOG_INFO, "Handler caught SIGSEGV - write attempt by process %d\n", getpid());
    void * fault_addr = si->si_addr;
    fault_addr = align_to_page_boundary(fault_addr);
    //log_message(LOG_INFO, "Faulting address (aligned): %p\n", fault_addr);

    // log_virtual_to_physical(fault_addr);

    void *new_page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (new_page == MAP_FAILED) {
        //log_message(LOG_ERROR, "mmap failed: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }
    //log_message(LOG_INFO, "New page mapped at: %p\n", new_page);

    memcpy(new_page, fault_addr, PAGE_SIZE);
    //log_message(LOG_INFO, "Content copied to new page by process %d\n", getpid());

    if (mprotect(new_page, PAGE_SIZE, PROT_READ | PROT_WRITE) == -1) {
        //log_message(LOG_ERROR, "mprotect failed: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }

    ptedit_entry_t fault_entry = ptedit_resolve(fault_addr, 0);
    ptedit_entry_t new_page_entry = ptedit_resolve(new_page, 0);

    size_t pt_pfn = ptedit_cast(fault_entry.pmd, ptedit_pmd_t).pfn;
    char* pt = ptedit_pmap(pt_pfn * ptedit_get_pagesize(), ptedit_get_pagesize());

    size_t entry_index = (((size_t)fault_addr) >> 12) & 0x1FF;
    size_t *mapped_entry = ((size_t *)pt) + entry_index;

    *mapped_entry = ptedit_set_pfn(*mapped_entry, ptedit_get_pfn(new_page_entry.pte));

    ptedit_update(fault_addr, 0, &new_page_entry);

    ptedit_invalidate_tlb(fault_addr);

    // log_virtual_to_physical(fault_addr);
    log_message(LOG_UPDATE, "Process %d updated virtual address %p to new physical address %zu", getpid(), fault_addr, (new_page_entry.pte));
}

bool setupSignalHandler() {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask); 
    sa.sa_sigaction = signalHandler;
    sa.sa_flags = SA_SIGINFO;

    if(sigaction(SIGSEGV, &sa, NULL) == -1) {
        //log_message(LOG_ERROR, "sigaction setup for SIGSEGV failed: %s", strerror(errno));
        return false;
    }
    log_message(LOG_UPDATE, "SIGSEGV Signal Handler updated");
    return true;
}

void show_diff(const char *file1, const char *file2) {
    char command[256];
    snprintf(command, sizeof(command), "diff %s %s", file1, file2);
    system(command);
}