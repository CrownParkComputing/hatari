#import <Foundation/Foundation.h>

#include "paths.h"
#include "str.h"

/* iOS replacement for Hatari's macOS screenshot-preference helper. */
char *Paths_GetMacScreenShotDir(void)
{
	NSArray<NSString *> *directories =
		NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
								 NSUserDomainMask, YES);
	NSString *directory = directories.firstObject ?: NSHomeDirectory();
	return Str_Dup(directory.fileSystemRepresentation);
}
