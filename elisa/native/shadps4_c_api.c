#include "shadps4_c_api.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char shadps4_elisa_error[512];
static char shadps4_elisa_last_log[1024];
static char* shadps4_elisa_file_buffer;

static void shadps4_elisa_set_error(const char* message) {
    if (message == NULL) {
        shadps4_elisa_error[0] = 0;
        return;
    }
    snprintf(shadps4_elisa_error, sizeof(shadps4_elisa_error), "%s", message);
}

int64_t shadps4_elisa_probe_value(void) {
    return 4242;
}

const char* shadps4_elisa_probe_message(void) {
    return "shadPS4 C API probe reached from Elisa";
}

const char* shadps4_elisa_last_error(void) {
    return shadps4_elisa_error;
}

const char* shadps4_elisa_last_log_path(void) {
    return shadps4_elisa_last_log;
}

int shadps4_elisa_find_ufc1(char* out_path, uint64_t out_path_cap) {
    const char* path = "Games/CUSA00264";
    if (out_path == NULL || out_path_cap == 0) {
        shadps4_elisa_set_error("missing output path buffer");
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        shadps4_elisa_set_error("Games/CUSA00264 was not found from the current shadPS4 root");
        out_path[0] = 0;
        return 0;
    }
    snprintf(out_path, (size_t)out_path_cap, "%s", path);
    shadps4_elisa_set_error(NULL);
    return 1;
}

static void shadps4_elisa_apply_trace_env(int null_fmask_reads, int fmask_decompress_in_place,
                                          int compositor_null_layer, int videoout_unorm) {
    setenv("SHADPS4_TRACE_INPUT", "1", 1);
    setenv("SHADPS4_TRACE_RENDER", "1", 1);
    setenv("SHADPS4_TRACE_VIDEO_OUT_EVERY", "30", 1);
    unsetenv("SHADPS4_NULL_FMASK_TEXTURE_READS");
    unsetenv("SHADPS4_FMASK_DECOMPRESS_IN_PLACE");
    unsetenv("SHADPS4_COMPOSITOR_NULL_LAYER");
    unsetenv("SHADPS4_COMPOSITOR_ZERO_LAYER");
    unsetenv("SHADPS4_VIDEOOUT_UNORM");

    if (null_fmask_reads) {
        setenv("SHADPS4_NULL_FMASK_TEXTURE_READS", "1", 1);
    }
    if (fmask_decompress_in_place) {
        setenv("SHADPS4_FMASK_DECOMPRESS_IN_PLACE", "1", 1);
    }
    if (compositor_null_layer) {
        setenv("SHADPS4_COMPOSITOR_NULL_LAYER", "1", 1);
    }
    if (videoout_unorm) {
        setenv("SHADPS4_VIDEOOUT_UNORM", "1", 1);
    }
}

static void shadps4_elisa_profile_flags(const char* profile, int* null_fmask_reads,
                                        int* fmask_decompress_in_place,
                                        int* compositor_null_layer, int* videoout_unorm) {
    *null_fmask_reads = 0;
    *fmask_decompress_in_place = 0;
    *compositor_null_layer = 0;
    *videoout_unorm = 0;
    if (profile == NULL) {
        return;
    }
    if (strcmp(profile, "fmask-null-read") == 0) {
        *null_fmask_reads = 1;
    } else if (strcmp(profile, "fmask-in-place") == 0) {
        *fmask_decompress_in_place = 1;
    } else if (strcmp(profile, "compositor-null-layer") == 0) {
        *compositor_null_layer = 1;
    } else if (strcmp(profile, "videoout-unorm") == 0) {
        *videoout_unorm = 1;
    }
}

static uint64_t shadps4_elisa_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000u) + ((uint64_t)ts.tv_nsec / 1000000u);
}

static int shadps4_elisa_run_ufc_trace_impl(const char* root_dir, const char* profile,
                                            uint32_t timeout_ms, int null_fmask_reads,
                                            int fmask_decompress_in_place,
                                            int compositor_null_layer, int videoout_unorm,
                                            char* out_log_path, uint64_t out_log_path_cap,
                                            int* out_exit_code, int* out_timed_out) {
    const char* root = root_dir != NULL ? root_dir : ".";
    const char* chosen_profile = profile != NULL ? profile : "baseline-safe";
    if (out_log_path == NULL || out_log_path_cap == 0) {
        shadps4_elisa_set_error("missing log path buffer");
        return 0;
    }
    if (out_exit_code != NULL) {
        *out_exit_code = -1;
    }
    if (out_timed_out != NULL) {
        *out_timed_out = 0;
    }

    char log_dir[1024];
    snprintf(log_dir, sizeof(log_dir), "%s/elisa_trace_logs", root);
    if (mkdir(log_dir, 0755) != 0 && errno != EEXIST) {
        shadps4_elisa_set_error("failed to create elisa_trace_logs");
        return 0;
    }

    time_t now = time(NULL);
    snprintf(out_log_path, (size_t)out_log_path_cap, "%s/%s_%lld.log", log_dir, chosen_profile,
             (long long)now);
    snprintf(shadps4_elisa_last_log, sizeof(shadps4_elisa_last_log), "%s", out_log_path);

    pid_t pid = fork();
    if (pid < 0) {
        shadps4_elisa_set_error("fork failed");
        return 0;
    }

    if (pid == 0) {
        int fd = open(out_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        chdir(root);
        shadps4_elisa_apply_trace_env(null_fmask_reads, fmask_decompress_in_place,
                                      compositor_null_layer, videoout_unorm);
        execl("./build/shadps4", "shadps4", "--game", "Games/CUSA00264", (char*)NULL);
        _exit(127);
    }

    const uint32_t effective_timeout = timeout_ms == 0 ? 8000 : timeout_ms;
    const uint64_t deadline = shadps4_elisa_monotonic_ms() + effective_timeout;
    int status = 0;
    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            if (out_exit_code != NULL) {
                if (WIFEXITED(status)) {
                    *out_exit_code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    *out_exit_code = 128 + WTERMSIG(status);
                }
            }
            shadps4_elisa_set_error(NULL);
            return 1;
        }
        if (done < 0) {
            shadps4_elisa_set_error("waitpid failed");
            return 0;
        }
        if (shadps4_elisa_monotonic_ms() >= deadline) {
            kill(pid, SIGTERM);
            usleep(200000);
            if (waitpid(pid, &status, WNOHANG) == 0) {
                kill(pid, SIGKILL);
            }
            waitpid(pid, &status, 0);
            if (out_timed_out != NULL) {
                *out_timed_out = 1;
            }
            if (out_exit_code != NULL) {
                *out_exit_code = 124;
            }
            shadps4_elisa_set_error(NULL);
            return 1;
        }
        usleep(50000);
    }
}

int shadps4_elisa_run_ufc_trace(const char* root_dir, const char* profile, uint32_t timeout_ms,
                                char* out_log_path, uint64_t out_log_path_cap,
                                int* out_exit_code, int* out_timed_out) {
    int null_fmask_reads = 0;
    int fmask_decompress_in_place = 0;
    int compositor_null_layer = 0;
    int videoout_unorm = 0;
    shadps4_elisa_profile_flags(profile, &null_fmask_reads, &fmask_decompress_in_place,
                                &compositor_null_layer, &videoout_unorm);
    return shadps4_elisa_run_ufc_trace_impl(root_dir, profile, timeout_ms, null_fmask_reads,
                                            fmask_decompress_in_place, compositor_null_layer,
                                            videoout_unorm, out_log_path, out_log_path_cap,
                                            out_exit_code, out_timed_out);
}

int shadps4_elisa_run_ufc_trace_flags(const char* root_dir, const char* profile,
                                      uint32_t timeout_ms, int null_fmask_reads,
                                      int fmask_decompress_in_place, int compositor_null_layer,
                                      int videoout_unorm, char* out_log_path,
                                      uint64_t out_log_path_cap, int* out_exit_code,
                                      int* out_timed_out) {
    return shadps4_elisa_run_ufc_trace_impl(root_dir, profile, timeout_ms, null_fmask_reads,
                                            fmask_decompress_in_place, compositor_null_layer,
                                            videoout_unorm, out_log_path, out_log_path_cap,
                                            out_exit_code, out_timed_out);
}

const char* shadps4_elisa_read_file(const char* path) {
    const long max_trace_bytes = 2 * 1024 * 1024;
    if (path == NULL) {
        shadps4_elisa_set_error("missing file path");
        return "";
    }
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        shadps4_elisa_set_error("failed to open file");
        return "";
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        shadps4_elisa_set_error("failed to seek file");
        return "";
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        shadps4_elisa_set_error("failed to measure file");
        return "";
    }
    long start = 0;
    if (size > max_trace_bytes) {
        start = size - max_trace_bytes;
    }
    if (fseek(file, start, SEEK_SET) != 0) {
        fclose(file);
        shadps4_elisa_set_error("failed to seek trace tail");
        return "";
    }
    long read_size = size - start;
    free(shadps4_elisa_file_buffer);
    shadps4_elisa_file_buffer = (char*)malloc((size_t)read_size + 1);
    if (shadps4_elisa_file_buffer == NULL) {
        fclose(file);
        shadps4_elisa_set_error("failed to allocate file buffer");
        return "";
    }
    size_t read_count = fread(shadps4_elisa_file_buffer, 1, (size_t)read_size, file);
    fclose(file);
    shadps4_elisa_file_buffer[read_count] = 0;
    shadps4_elisa_set_error(NULL);
    return shadps4_elisa_file_buffer;
}
