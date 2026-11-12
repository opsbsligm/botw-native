// SPDX-License-Identifier: MIT
// Linux portable packages: bounded directory-FD traversal and atomic RomFS views.
// Included by mods.c after mods_dir(); no game files are copied or modified.
#include <fcntl.h>
#include <signal.h>
#include <strings.h>
#include <sys/syscall.h>

#define MODS_VIEW_MARKER ".native-romfs-view"
#define MODS_VIEW_MAGIC "native-romfs-view-v1\n"
#define MODS_VIEW_PARENT "native-mods"
#define MODS_VIEW_NAME "romfs-view"
#define MODS_VIEW_PATH MODS_VIEW_PARENT "/" MODS_VIEW_NAME
#define MODS_DIR_FLAGS (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#define MODS_RENAME_NOREPLACE 1
#define MODS_RENAME_EXCHANGE 2

static bool mods_view_owned(int fd) {
    char marker[sizeof MODS_VIEW_MAGIC] = {0};
    struct stat directory, file;
    int input = openat(fd, MODS_VIEW_MARKER, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (input < 0) return false;
    bool ok = !fstat(fd, &directory) && directory.st_uid == geteuid() && !(directory.st_mode & 077) &&
              !fstat(input, &file) && S_ISREG(file.st_mode) && file.st_uid == geteuid() &&
              file.st_size == (off_t)(sizeof MODS_VIEW_MAGIC - 1) &&
              read(input, marker, sizeof marker) == (ssize_t)(sizeof MODS_VIEW_MAGIC - 1) &&
              !memcmp(marker, MODS_VIEW_MAGIC, sizeof MODS_VIEW_MAGIC - 1);
    close(input);
    return ok;
}

// Only task-owned view directories are passed here. Never traverse a symlink,
// including one introduced while a previous directory is being cleaned up.
static bool mods_view_remove(int parent, const char *name, unsigned depth) {
    if (depth > 40) { errno = ELOOP; return false; }
    struct stat info;
    if (fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
    if (!S_ISDIR(info.st_mode)) return !unlinkat(parent, name, 0);
    int child = openat(parent, name, MODS_DIR_FLAGS);
    if (child < 0) return false;
    DIR *directory = fdopendir(child);
    if (!directory) { close(child); return false; }
    bool ok = true;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!mods_view_remove(child, entry->d_name, depth + 1)) { ok = false; break; }
        errno = 0;
    }
    if (errno) ok = false;
    closedir(directory);
    return ok && !unlinkat(parent, name, AT_REMOVEDIR);
}

static bool mods_view_collect_stale(int parent) {
    DIR *directory = fdopendir(dup(parent));
    if (!directory) return false;
    struct dirent *entry;
    bool ok = true;
    unsigned visited = 0;
    errno = 0;
    while ((entry = readdir(directory))) {
        const char *name = entry->d_name;
        if (strncmp(name, ".romfs-new-", 11)) continue;
        if (++visited > 128) { errno = EFBIG; ok = false; break; }
        char *end;
        errno = 0;
        long owner = strtol(name + 11, &end, 10);
        if (errno || owner <= 1 || owner > INT_MAX || *end++ != '-') { errno = 0; continue; }
        char *finish;
        unsigned long serial = strtoul(end, &finish, 10);
        if (errno || !serial || finish == end || *finish) { errno = 0; continue; }
        if (!kill((pid_t)owner, 0) || errno != ESRCH) { errno = 0; continue; }
        int child = openat(parent, name, MODS_DIR_FLAGS);
        if (child < 0) { errno = 0; continue; }
        bool owned = mods_view_owned(child);
        close(child);
        if (owned && !mods_view_remove(parent, name, 0)) { ok = false; break; }
        errno = 0;
    }
    if (errno) ok = false;
    closedir(directory);
    return ok;
}

static bool mods_code_entry(const char *name) {
    const char *suffix = strrchr(name, '.');
    return !strcmp(name, "exefs") || !strcmp(name, "README") || !strcmp(name, "README.md") ||
           (suffix && (!strcasecmp(suffix, ".ips") || !strcasecmp(suffix, ".ips32") ||
                       !strcasecmp(suffix, ".pchtxt")));
}

static bool mods_view_stage(int source, int target, unsigned depth, bool legacy_root, int *files) {
    if (depth > 32) { errno = ELOOP; return false; }
    int scan = dup(source);
    if (scan < 0) return false;
    DIR *directory = fdopendir(scan);
    if (!directory) { close(scan); return false; }
    bool ok = true;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        const char *name = entry->d_name;
        if (name[0] == '.' || (legacy_root && mods_code_entry(name))) continue;
        struct stat before;
        if (fstatat(source, name, &before, AT_SYMLINK_NOFOLLOW)) { ok = false; break; }
        if (S_ISDIR(before.st_mode)) {
            int input = openat(source, name, MODS_DIR_FLAGS);
            if (input < 0) { ok = false; break; }
            if (mkdirat(target, name, 0700) && errno != EEXIST) { close(input); ok = false; break; }
            int output = openat(target, name, MODS_DIR_FLAGS);
            ok = output >= 0 && mods_view_stage(input, output, depth + 1, false, files);
            if (output >= 0) close(output);
            close(input);
        } else if (S_ISREG(before.st_mode)) {
            if (*files >= 65536) { errno = EFBIG; ok = false; break; }
            int input = openat(source, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
            struct stat opened, linked, after;
            ok = input >= 0 && !fstat(input, &opened) && S_ISREG(opened.st_mode) &&
                 opened.st_dev == before.st_dev && opened.st_ino == before.st_ino;
            if (ok && unlinkat(target, name, 0) && errno != ENOENT) ok = false;
            if (ok && linkat(source, name, target, name, 0)) ok = false;
            if (ok) ok = !fstatat(target, name, &linked, AT_SYMLINK_NOFOLLOW) && S_ISREG(linked.st_mode) &&
                         !fstat(input, &after) && linked.st_dev == opened.st_dev && linked.st_ino == opened.st_ino &&
                         after.st_size == opened.st_size && after.st_mtim.tv_sec == opened.st_mtim.tv_sec &&
                         after.st_mtim.tv_nsec == opened.st_mtim.tv_nsec;
            if (input >= 0) close(input);
            if (ok) (*files)++;
        } else { errno = EINVAL; ok = false; }
        if (!ok) break;
        errno = 0;
    }
    if (errno) ok = false;
    closedir(directory);
    return ok && !fsync(target);
}

static int mods_linux_romfs_apply(void) {
    int mods = -1, parent = -1, staging = -1, files = 0, result = -1;
    bool pending = false;
    char temporary[64] = {0};
    // rt_paths_init() pins the writable data root as cwd. The package's game
    // data argument may be elsewhere and is always left untouched.
    mods = open("mods", MODS_DIR_FLAGS);
    if (mods < 0 && errno != ENOENT) goto done;
    if (mkdir(MODS_VIEW_PARENT, 0700) && errno != EEXIST) goto done;
    parent = open(MODS_VIEW_PARENT, MODS_DIR_FLAGS);
    struct stat info;
    if (parent < 0 || fstat(parent, &info)) goto done;
    if (info.st_uid != geteuid() || (info.st_mode & 077)) { errno = EACCES; goto done; }
    if (!mods_view_collect_stale(parent)) goto done;
    static unsigned serial;
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        snprintf(temporary, sizeof temporary, ".romfs-new-%ld-%u", (long)getpid(), ++serial);
        if (!mkdirat(parent, temporary, 0700)) { pending = true; break; }
        if (errno != EEXIST) goto done;
    }
    if (!pending) goto done;
    staging = openat(parent, temporary, MODS_DIR_FLAGS);
    if (staging < 0) goto done;
    int marker = openat(staging, MODS_VIEW_MARKER, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (marker < 0) goto done;
    bool marked = write(marker, MODS_VIEW_MAGIC, sizeof MODS_VIEW_MAGIC - 1) == (ssize_t)(sizeof MODS_VIEW_MAGIC - 1) && !fsync(marker);
    close(marker);
    if (!marked) goto done;
    if (mods >= 0) {
        DIR *directory = fdopendir(dup(mods));
        if (!directory) goto done;
        DirEntry names[64];
        int count = 0;
        bool listed = true;
        struct dirent *entry;
        errno = 0;
        while ((entry = readdir(directory))) {
            if (entry->d_name[0] == '.') continue;
            if (fstatat(mods, entry->d_name, &info, AT_SYMLINK_NOFOLLOW)) { listed = false; break; }
            if (S_ISREG(info.st_mode)) continue;
            if (!S_ISDIR(info.st_mode) || count == 64) { errno = EINVAL; listed = false; break; }
            snprintf(names[count++].name, sizeof names[0].name, "%s", entry->d_name);
            errno = 0;
        }
        if (errno) listed = false;
        closedir(directory);
        if (!listed) goto done;
        for (int i = 0; i < count; i++) for (int j = i + 1; j < count; j++)
            if (strcmp(names[j].name, names[i].name) < 0) { DirEntry swap = names[i]; names[i] = names[j]; names[j] = swap; }
        for (int i = 0; i < count; i++) {
            int mod = openat(mods, names[i].name, MODS_DIR_FLAGS);
            if (mod < 0) goto done;
            int romfs = openat(mod, "romfs", MODS_DIR_FLAGS);
            bool legacy = romfs < 0 && errno == ENOENT;
            bool ok = (romfs >= 0 || legacy) && mods_view_stage(legacy ? mod : romfs, staging, 0, legacy, &files);
            if (romfs >= 0) close(romfs);
            close(mod);
            if (!ok) goto done;
        }
    }
    int previous = openat(parent, MODS_VIEW_NAME, MODS_DIR_FLAGS);
    if (previous < 0 && errno != ENOENT) goto done;
    if (previous >= 0) {
        bool owned = mods_view_owned(previous);
        close(previous);
        if (!owned) { errno = EACCES; goto done; }
    }
    if (files) {
        if (syscall(SYS_renameat2, parent, temporary, parent, MODS_VIEW_NAME,
                    previous >= 0 ? MODS_RENAME_EXCHANGE : MODS_RENAME_NOREPLACE)) goto done;
        // EXCHANGE leaves the previous owned view at our private staging name.
        pending = previous >= 0;
        if (fsync(parent) || setenv("SAVAGEWIND_ROMFS_OVERLAY", MODS_VIEW_PATH, 1)) goto done;
        g_view = MODS_VIEW_PATH;
    } else {
        if (previous >= 0 && !mods_view_remove(parent, MODS_VIEW_NAME, 0)) goto done;
        const char *value = getenv("SAVAGEWIND_ROMFS_OVERLAY");
        if (value && !strcmp(value, MODS_VIEW_PATH)) unsetenv("SAVAGEWIND_ROMFS_OVERLAY");
        g_view = NULL;
    }
    result = files;
done:
    if (result < 0) LOG("[mods] RomFS view failed: %s\n", strerror(errno));
    if (staging >= 0) close(staging);
    if (pending && parent >= 0 && !mods_view_remove(parent, temporary, 0)) {
        LOG("[mods] RomFS staging cleanup failed: %s\n", strerror(errno));
        result = -1;
    }
    if (parent >= 0) close(parent);
    if (mods >= 0) close(mods);
    return result;
}
