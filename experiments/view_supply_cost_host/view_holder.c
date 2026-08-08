// Holder process for the host-baseline per-view supply cost experiment.
//
// One holder represents one live "view" supplied by the mount route. It
// optionally unshares its mount namespace and bind-mounts a per-view tree onto
// a common pathname, then stays alive so that the resident cost of that view
// can be measured. The control mode (no unshare, no mount) exists so that the
// per-process cost can be subtracted from the per-view cost.
//
// This runs on a stock host kernel. It does not exercise namei_ext and is not
// Phase 1 validation.

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

enum holder_mode {
	HOLDER_MODE_CONTROL = 0,
	HOLDER_MODE_MOUNTNS_BIND = 1,
};

static void die(const char *what)
{
	fprintf(stderr, "view_holder: %s: %s\n", what, strerror(errno));
	_exit(1);
}

int main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr,
			"usage: view_holder <control|mountns_bind> <source_dir> <view_path>\n");
		return 2;
	}

	enum holder_mode mode;
	if (strcmp(argv[1], "control") == 0) {
		mode = HOLDER_MODE_CONTROL;
	} else if (strcmp(argv[1], "mountns_bind") == 0) {
		mode = HOLDER_MODE_MOUNTNS_BIND;
	} else {
		fprintf(stderr, "view_holder: unknown mode %s\n", argv[1]);
		return 2;
	}

	const char *source_dir = argv[2];
	const char *view_path = argv[3];

	if (mode == HOLDER_MODE_MOUNTNS_BIND) {
		if (unshare(CLONE_NEWNS) != 0)
			die("unshare(CLONE_NEWNS)");
		// Keep this namespace's mounts from propagating back to the
		// parent; otherwise every per-view bind mount would also land in
		// the host mount table and the measurement would not represent
		// per-task views.
		if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
			die("mount(/, MS_REC|MS_PRIVATE)");
		if (mount(source_dir, view_path, NULL, MS_BIND | MS_REC, NULL) != 0)
			die("mount(bind)");
	}

	// Report readiness. The parent waits for this line before timing the
	// next view, so the measured interval covers the whole supply action.
	if (write(STDOUT_FILENO, "ready\n", 6) != 6)
		die("write(ready)");

	// Stay resident until the parent tears the ladder down.
	for (;;)
		pause();

	return 0;
}
