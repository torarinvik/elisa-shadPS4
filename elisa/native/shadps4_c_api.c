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
static char shadps4_elisa_log_path_ring[8][1024];
static int shadps4_elisa_log_path_ring_index;
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
    shadps4_elisa_log_path_ring_index = (shadps4_elisa_log_path_ring_index + 1) % 8;
    snprintf(shadps4_elisa_log_path_ring[shadps4_elisa_log_path_ring_index],
             sizeof(shadps4_elisa_log_path_ring[shadps4_elisa_log_path_ring_index]), "%s",
             shadps4_elisa_last_log);
    return shadps4_elisa_log_path_ring[shadps4_elisa_log_path_ring_index];
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
    setenv("SHADPS4_GPU_WAIT_TIMEOUT_MS", "2000", 1);
    setenv("SHADPS4_SKIP_IMGUI_TEXTURE_UPLOADS", "1", 1);
    setenv("SHADPS4_MOLTENVK_SAFE_MODE", "1", 1);
    setenv("SHADPS4_FORCE_FIFO_PRESENT", "1", 1);
    setenv("SHADPS4_TRACE_GPU_COMMANDS", "1", 1);
    setenv("SHADPS4_TRACE_SCREENSHOT_INTERVAL_MS", "3000", 1);
    setenv("SHADPS4_TRACE_SCREENSHOT_GAME_ONLY", "1", 1);
    setenv("SHADPS4_TRACE_SCREENSHOT_STATS_ONLY", "1", 1);
    setenv("SHADPS4_TRACE_SCREENSHOT_DIR", "./elisa_trace_screenshots", 1);
    setenv("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS", "0", 0);
    setenv("MVK_CONFIG_METAL_COMPILE_TIMEOUT", "2000000000", 0);
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

static const char* shadps4_elisa_select_binary(void) {
    const char* override = getenv("SHADPS4_ELISA_BINARY");
    if (override != NULL && override[0] != 0) {
        return override;
    }
    if (access("./build-codex-make/shadps4", X_OK) == 0) {
        return "./build-codex-make/shadps4";
    }
    return "./build/shadps4";
}

static void shadps4_elisa_warn_stale_build_cache(void) {
    FILE* file = fopen("./build/CMakeCache.txt", "rb");
    if (file == NULL) {
        return;
    }

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fclose(file);
        return;
    }

    char line[2048];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "CMAKE_HOME_DIRECTORY:INTERNAL=", 30) == 0) {
            line[strcspn(line, "\r\n")] = 0;
            if (strstr(line, cwd) == NULL) {
                fprintf(stderr,
                        "ELISA_TRACE build_cache_warning path=./build/CMakeCache.txt "
                        "expected_root=\"%s\" cache_line=\"%s\"\n",
                        cwd, line);
            }
            break;
        }
    }
    fclose(file);
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
    } else if (strcmp(profile, "fmask-in-place") == 0 || strcmp(profile, "fmask-in-place-20s") == 0) {
        *fmask_decompress_in_place = 1;
    } else if (strcmp(profile, "fmask-in-place-videoout-unorm") == 0 ||
               strcmp(profile, "fmask-in-place-videoout-unorm-20s") == 0) {
        *fmask_decompress_in_place = 1;
        *videoout_unorm = 1;
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
        shadps4_elisa_warn_stale_build_cache();
        const char* binary_path = shadps4_elisa_select_binary();
        fprintf(stderr, "ELISA_TRACE binary_path=%s\n", binary_path);
        execl(binary_path, "shadps4", "--game", "Games/CUSA00264", (char*)NULL);
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

static int shadps4_elisa_line_contains(const char* line, size_t line_len, const char* needle) {
    const size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return 1;
    }
    if (line_len < needle_len) {
        return 0;
    }
    for (size_t index = 0; index + needle_len <= line_len; ++index) {
        if (memcmp(line + index, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int shadps4_elisa_keep_trace_line(const char* line, size_t line_len) {
    const int trace_render_evidence =
        shadps4_elisa_line_contains(line, line_len, "TRACE_RENDER render_target") ||
        shadps4_elisa_line_contains(line, line_len, "TRACE_RENDER depth_target") ||
        shadps4_elisa_line_contains(line, line_len, "TRACE_RENDER metadata_register") ||
        shadps4_elisa_line_contains(line, line_len, "TRACE_RENDER metadata_texture_read") ||
        shadps4_elisa_line_contains(line, line_len, "TRACE_RENDER fmask_decompress") ||
        shadps4_elisa_line_contains(line, line_len, "TRACE_RENDER fmask_decompress_missing_mrt1") ||
        shadps4_elisa_line_contains(line, line_len, "is_videoout_storage=true") ||
        shadps4_elisa_line_contains(line, line_len, "TRACE_RENDER image_binding_null") ||
        shadps4_elisa_line_contains(line, line_len, "TRACE_RENDER compositor_null_layer");
    return shadps4_elisa_line_contains(line, line_len, "ELISA_TRACE") || trace_render_evidence ||
           shadps4_elisa_line_contains(line, line_len, "TRACE_VIDEO_OUT") ||
           shadps4_elisa_line_contains(line, line_len, "TRACE_IMAGE_VIEW") ||
           shadps4_elisa_line_contains(line, line_len, "TRACE_BLACK_WATCHDOG") ||
           shadps4_elisa_line_contains(line, line_len, "Strict black-screen watchdog") ||
           shadps4_elisa_line_contains(line, line_len, "TRACE_SCREENSHOT") ||
           shadps4_elisa_line_contains(line, line_len, "GPU command diagnostics:") ||
           shadps4_elisa_line_contains(line, line_len, "GPU command [") ||
           shadps4_elisa_line_contains(line, line_len, "Compiling cs shader") ||
           shadps4_elisa_line_contains(line, line_len, "Compiling vs shader") ||
           shadps4_elisa_line_contains(line, line_len, "Compiling graphics pipeline") ||
           shadps4_elisa_line_contains(line, line_len, "Compiling compute pipeline");
}

static char* shadps4_elisa_filter_trace_log(char* input, size_t input_size, size_t* out_size) {
    const char* header = "ELISA_TRACE log_filter mode=render-evidence\n";
    const size_t header_len = strlen(header);
    const size_t footer_cap = 96;
    char* output = (char*)malloc(input_size + header_len + footer_cap + 1);
    if (output == NULL) {
        return NULL;
    }

    size_t output_size = 0;
    memcpy(output + output_size, header, header_len);
    output_size += header_len;

    size_t line_start = 0;
    size_t omitted_lines = 0;
    for (size_t index = 0; index <= input_size; ++index) {
        if (index == input_size || input[index] == '\n') {
            size_t line_len = index - line_start;
            if (shadps4_elisa_keep_trace_line(input + line_start, line_len)) {
                memcpy(output + output_size, input + line_start, line_len);
                output_size += line_len;
                output[output_size++] = '\n';
            } else if (line_len > 0) {
                omitted_lines += 1;
            }
            line_start = index + 1;
        }
    }

    char footer[96];
    int footer_len = snprintf(footer, sizeof(footer),
                              "ELISA_TRACE log_filter omitted_lines=%zu\n", omitted_lines);
    if (footer_len > 0 && (size_t)footer_len < sizeof(footer)) {
        memcpy(output + output_size, footer, (size_t)footer_len);
        output_size += (size_t)footer_len;
    }
    output[output_size] = 0;
    *out_size = output_size;
    return output;
}

const char* shadps4_elisa_read_file(const char* path) {
    const long filter_threshold_bytes = 128 * 1024;
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
    free(shadps4_elisa_file_buffer);
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        shadps4_elisa_set_error("failed to seek trace");
        return "";
    }
    char* raw_buffer = (char*)malloc((size_t)size + 1);
    if (raw_buffer == NULL) {
        fclose(file);
        shadps4_elisa_set_error("failed to allocate file buffer");
        return "";
    }
    size_t read_count = fread(raw_buffer, 1, (size_t)size, file);
    fclose(file);
    raw_buffer[read_count] = 0;

    if (size <= filter_threshold_bytes) {
        shadps4_elisa_file_buffer = raw_buffer;
        shadps4_elisa_set_error(NULL);
        return shadps4_elisa_file_buffer;
    }

    size_t filtered_size = 0;
    shadps4_elisa_file_buffer = shadps4_elisa_filter_trace_log(raw_buffer, read_count, &filtered_size);
    free(raw_buffer);
    if (shadps4_elisa_file_buffer == NULL) {
        shadps4_elisa_set_error("failed to allocate filtered trace buffer");
        return "";
    }
    (void)filtered_size;
    shadps4_elisa_set_error(NULL);
    return shadps4_elisa_file_buffer;
}
