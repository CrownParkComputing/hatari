#ifndef RETRO_ATARIST_IOS_SYS_DISK_H
#define RETRO_ATARIST_IOS_SYS_DISK_H

/*
 * Hatari's macOS-only raw-device size probe is guarded by __APPLE__, which is
 * also defined by the iPhone SDK. iOS exposes neither <sys/disk.h> nor raw
 * disk devices to applications. Keep that unreachable probe compilable and
 * force it to fall back to Hatari's ordinary fseeko()/ftello() path.
 */
#define DKIOCGETBLOCKSIZE 0UL
#define DKIOCGETBLOCKCOUNT 0UL
#define ioctl(...) (-1)

#endif
