// Mods: the player's own content for this game copy, in <data root>/mods - one folder per mod, applied in name
// order (later mods override earlier ones).
//
// Two kinds, both optional - a missing or empty mods/ folder changes nothing:
//
//   RomFS mods - files laid out like the RomFS (e.g. Textures/.../x.bflt). They replace the same-named game files
//                or add new ones into existing directories, exactly the way the development overlay
//                (SAVAGEWIND_ROMFS_OVERLAY, runtime/services/fs.c) already does: the mods are merged into one view
//                (<extracted>/.mods-view) and that view is what the overlay reads, so fs.c itself is unchanged.
//
//   ExeFS patches - a mod's <module>.ips files patch the copied NSO before code generation. Standard IPS/IPS32
//                supports literal zero bytes and RLE, requires an uncompressed module, and protects its 0x100-byte
//                header. Every record is validated before modification. The older private IPS\x1a dialect keeps
//                its legacy zero-means-keep convention. Build-id filenames and PCHTXT are not supported here.
//
// Because a patch can change code, with a non-empty mods/ the start screen always offers to rebuild the game code
// (mods_need_rebuild), and a successful rebuild marks the mods applied (mods_mark_built) - so a plain start
// afterwards goes straight into the game. Deleting a mod and re-running Set up restores the original module (the
// copy is re-made).
//
// mods.c is C; called from the start screen (runtime/ui/launcher.mm) and the boot (runtime/main.c).
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// <data root>/mods, or NULL while the data root isn't up yet.
const char *mods_dir(void);
bool mods_present(void);  // the mods/ folder exists and isn't empty

// Linux: community <mod>/romfs/ or legacy root contents are staged under the writable data root's
// native-mods/romfs-view. Code artifacts are excluded. Directory-FD traversal rejects symlinks/devices;
// verified owned views are atomically exchanged, without copying game data. A failure returns -1 and stops boot.
// Other platforms merge the RomFS mods into <extracted>/.mods-view. Point SAVAGEWIND_ROMFS_OVERLAY at the view; removes both
// when there is nothing to overlay (a development overlay set by the user alone is left untouched). The extracted
// root is "extracted" on the other platforms. Returns the number of files merged (0: no
// RomFS mods, -1: Linux preparation failure). Called at boot before the game files open; idempotent.
int mods_romfs_apply(void);

// Apply the mods' .ips to <extracted>/exefs (after the exefs copy, before the game code build). For each mod, the
// records of every <module>.ips in the folder - in name order - are applied to that module; the module's NSO
// header must still read with the same build-id afterwards. Returns the number of modules left patched; <why>
// (when non-NULL) a first problem, <record> (when non-NULL) where the applied-patches note went ("" when there is
// nothing to record).
int mods_exefs_apply(const char *extracted, char *why, size_t why_size, char *record, size_t record_size);

// true while the game code was not built with the current mods (see the top): the start screen keeps offering the
// rebuild. mods_mark_built clears that after a successful build.
bool mods_need_rebuild(const char *extracted);
void mods_mark_built(const char *extracted);

// What the start screen shows: the mod folders and what each one has (how many RomFS files, which modules its
// patches reached). The rows, terminated by an empty name.
typedef struct ModsInfo {
    const char *name;  // the mod folder's name ("" ends the list)
    const char *detail;  // a sentence about its contents
} ModsInfo;
int mods_list(ModsInfo *rows, int cap);

#ifdef __cplusplus
}
#endif
