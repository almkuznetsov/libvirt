/*
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include "testutils.h"

#ifdef __linux__

# include <fcntl.h>
# include <sys/stat.h>
# include "virstring.h"
# include "virutil.h"
# include "virerror.h"
# include "virlog.h"

# define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("tests.scsihosttest");

char *scsihost_class_path;
# define TEST_SCSIHOST_CLASS_PATH scsihost_class_path

/*
 * Initialized/create a mock sysfs environment with 4 scsi_host devices
 * located on "0000:00:1f.1" and "0000:00:1f.2".  Each directory will
 * contain 4 unique_id files having the same value.
 *
 * The environment is:
 *
 *  4 files:
 *
 *     sys/devices/pci0000:00/0000:00:1f.1/ata1/host0/scsi_host/host0/unique_id
 *     sys/devices/pci0000:00/0000:00:1f.1/ata2/host1/scsi_host/host1/unique_id
 *     sys/devices/pci0000:00/0000:00:1f.2/ata1/host0/scsi_host/host0/unique_id
 *     sys/devices/pci0000:00/0000:00:1f.2/ata2/host1/scsi_host/host1/unique_id
 *
 *  4 symlinks:
 *
 *     sys/class/scsi_host/host0 -> link to 1f.1 host 0
 *     sys/class/scsi_host/host1 -> link to 1f.1 host 1
 *     sys/class/scsi_host/host2 -> link to 1f.2 host 0
 *     sys/class/scsi_host/host3 -> link to 1f.2 host 1
 *
 *  The unique_id's for host0 and host2 are set to "1"
 *  The unique_id's for host1 and host3 are set to "2"
 */

static int
create_scsihost(const char *fakesysfsdir, const char *devicepath,
                const char *unique_id, const char *hostname)
{
    char *unique_id_path = NULL;
    char *link_path = NULL;
    char *spot;
    int ret = -1;
    int fd = -1;

    if (virAsprintfQuiet(&unique_id_path, "%s/devices/pci0000:00/%s/unique_id",
                         fakesysfsdir, devicepath) < 0 ||
        virAsprintfQuiet(&link_path, "%s/class/scsi_host/%s",
                         fakesysfsdir, hostname) < 0) {
        fprintf(stderr, "Out of memory\n");
        goto cleanup;
    }

    /* Rather than create path & file, temporarily snip off the file to
     * create the path
     */
    if (!(spot = strstr(unique_id_path, "unique_id"))) {
        fprintf(stderr, "Did not find unique_id in path\n");
        goto cleanup;
    }
    spot--;
    *spot = '\0';
    if (virFileMakePathWithMode(unique_id_path, 0755) < 0) {
        fprintf(stderr, "Unable to make path to '%s'\n", unique_id_path);
        goto cleanup;
    }
    *spot = '/';

    /* Rather than create path & file, temporarily snip off the file to
     * create the path
     */
    if (!(spot = strstr(link_path, hostname))) {
        fprintf(stderr, "Did not find hostname in path\n");
        goto cleanup;
    }
    spot--;
    *spot = '\0';
    if (virFileMakePathWithMode(link_path, 0755) < 0) {
        fprintf(stderr, "Unable to make path to '%s'\n", link_path);
        goto cleanup;
    }
    *spot = '/';

    if ((fd = open(unique_id_path, O_CREAT|O_WRONLY, 0444)) < 0) {
        fprintf(stderr, "Unable to create '%s'\n", unique_id_path);
        goto cleanup;
    }

    if (safewrite(fd, unique_id, 1) != 1) {
        fprintf(stderr, "Unable to write '%s'\n", unique_id);
        goto cleanup;
    }
    VIR_DEBUG("Created unique_id '%s'", unique_id_path);

    /* The link is to the path not the file - so remove the file */
    if (!(spot = strstr(unique_id_path, "unique_id"))) {
        fprintf(stderr, "Did not find unique_id in path\n");
        goto cleanup;
    }
    spot--;
    *spot = '\0';
    if (symlink(unique_id_path, link_path) < 0) {
        fprintf(stderr, "Unable to create symlink '%s' to '%s'\n",
                link_path, unique_id_path);
        goto cleanup;
    }
    VIR_DEBUG("Created symlink '%s'", link_path);

    ret = 0;

 cleanup:
    VIR_FORCE_CLOSE(fd);
    VIR_FREE(unique_id_path);
    VIR_FREE(link_path);
    return ret;
}

static int
init_scsihost_sysfs(const char *fakesysfsdir)
{
    int ret = 0;

    if (create_scsihost(fakesysfsdir,
                        "0000:00:1f.1/ata1/host0/scsi_host/host0",
                        "1", "host0") < 0 ||
        create_scsihost(fakesysfsdir,
                        "0000:00:1f.1/ata2/host1/scsi_host/host1",
                        "2", "host1") < 0 ||
        create_scsihost(fakesysfsdir,
                        "0000:00:1f.2/ata1/host0/scsi_host/host0",
                        "1", "host2") < 0 ||
        create_scsihost(fakesysfsdir,
                        "0000:00:1f.2/ata2/host1/scsi_host/host1",
                        "2", "host3") < 0)
        ret = -1;

    return ret;
}

/* Test virReadSCSIUniqueId */
static int
testVirReadSCSIUniqueId(const void *data ATTRIBUTE_UNUSED)
{
    int hostnum, unique_id;

    for (hostnum = 0; hostnum < 4; hostnum++) {
        if (virReadSCSIUniqueId(TEST_SCSIHOST_CLASS_PATH,
                                hostnum, &unique_id) < 0) {
            fprintf(stderr, "Failed to read hostnum=%d unique_id\n", hostnum);
            return -1;
        }

        /* host0 and host2 have unique_id == 1
         * host1 and host3 have unique_id == 2
         */
        if ((hostnum == 0 || hostnum == 2) && unique_id != 1) {
            fprintf(stderr, "The unique_id='%d' for hostnum=%d is wrong\n",
                    unique_id, hostnum);
            return -1;
        } else if ((hostnum == 1 || hostnum == 3) && unique_id != 2) {
            fprintf(stderr, "The unique_id='%d' for hostnum=%d is wrong\n",
                    unique_id, hostnum);
            return -1;
        }
    }

    return 0;
}

# define FAKESYSFSDIRTEMPLATE abs_builddir "/fakesysfsdir-XXXXXX"

static int
mymain(void)
{
    int ret = -1;
    char *fakesysfsdir = NULL;

    if (VIR_STRDUP_QUIET(fakesysfsdir, FAKESYSFSDIRTEMPLATE) < 0) {
        fprintf(stderr, "Out of memory\n");
        goto cleanup;
    }

    if (!mkdtemp(fakesysfsdir)) {
        fprintf(stderr, "Cannot create fakesysfsdir");
        goto cleanup;
    }

    setenv("LIBVIRT_FAKE_SYSFS_DIR", fakesysfsdir, 1);

    if (init_scsihost_sysfs(fakesysfsdir) < 0) {
        fprintf(stderr, "Failed to create fakesysfs='%s'\n", fakesysfsdir);
        goto cleanup;
    }

    if (virAsprintfQuiet(&scsihost_class_path, "%s/class/scsi_host",
                         fakesysfsdir) < 0) {
        fprintf(stderr, "Out of memory\n");
        goto cleanup;
    }
    VIR_DEBUG("Reading from '%s'", scsihost_class_path);

    if (virtTestRun("testVirReadSCSIUniqueId",
                    testVirReadSCSIUniqueId, NULL) < 0) {
        ret = -1;
        goto cleanup;
    }

    ret = 0;

 cleanup:
    if (getenv("LIBVIRT_SKIP_CLEANUP") == NULL)
        virFileDeleteTree(fakesysfsdir);
    VIR_FREE(fakesysfsdir);
    VIR_FREE(scsihost_class_path);
    return ret;
}

VIRT_TEST_MAIN(mymain)
#else
int
main(void)
{
    return EXIT_AM_SKIP;
}
#endif
