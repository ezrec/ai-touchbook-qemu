#include "qemu-common.h"
#include "qemu-option.h"
#include "qemu-config.h"

QemuOptsList qemu_drive_opts = {
    .name = "drive",
    .head = TAILQ_HEAD_INITIALIZER(qemu_drive_opts.head),
    .desc = {
        {
            .name = "bus",
            .type = QEMU_OPT_NUMBER,
            .help = "bus number",
        },{
            .name = "unit",
            .type = QEMU_OPT_NUMBER,
            .help = "unit number (i.e. lun for scsi)",
        },{
            .name = "if",
            .type = QEMU_OPT_STRING,
            .help = "interface (ide, scsi, sd, mtd, floppy, pflash, virtio)",
        },{
            .name = "index",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "cyls",
            .type = QEMU_OPT_NUMBER,
            .help = "number of cylinders (ide disk geometry)",
        },{
            .name = "heads",
            .type = QEMU_OPT_NUMBER,
            .help = "number of heads (ide disk geometry)",
        },{
            .name = "secs",
            .type = QEMU_OPT_NUMBER,
            .help = "number of sectors (ide disk geometry)",
        },{
            .name = "trans",
            .type = QEMU_OPT_STRING,
            .help = "chs translation (auto, lba. none)",
        },{
            .name = "media",
            .type = QEMU_OPT_STRING,
            .help = "media type (disk, cdrom)",
        },{
            .name = "snapshot",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "file",
            .type = QEMU_OPT_STRING,
            .help = "disk image",
        },{
            .name = "cache",
            .type = QEMU_OPT_STRING,
            .help = "host cache usage (none, writeback, writethrough)",
        },{
            .name = "aio",
            .type = QEMU_OPT_STRING,
            .help = "host AIO implementation (threads, native)",
        },{
            .name = "format",
            .type = QEMU_OPT_STRING,
            .help = "disk format (raw, qcow2, ...)",
        },{
            .name = "serial",
            .type = QEMU_OPT_STRING,
        },{
            .name = "werror",
            .type = QEMU_OPT_STRING,
        },{
            .name = "addr",
            .type = QEMU_OPT_STRING,
            .help = "pci address (virtio only)",
        },
        { /* end if list */ }
    },
};

QemuOptsList qemu_device_opts = {
    .name = "device",
    .head = TAILQ_HEAD_INITIALIZER(qemu_device_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting device properties
         */
        { /* end if list */ }
    },
};

static QemuOptsList *lists[] = {
    &qemu_drive_opts,
    &qemu_device_opts,
    NULL,
};

int qemu_set_option(const char *str)
{
    char group[64], id[64], arg[64];
    QemuOpts *opts;
    int i, rc, offset;

    rc = sscanf(str, "%63[^.].%63[^.].%63[^=]%n", group, id, arg, &offset);
    if (rc < 3 || str[offset] != '=') {
        fprintf(stderr, "can't parse: \"%s\"\n", str);
        return -1;
    }

    for (i = 0; lists[i] != NULL; i++) {
        if (strcmp(lists[i]->name, group) == 0)
            break;
    }
    if (lists[i] == NULL) {
        fprintf(stderr, "there is no option group \"%s\"\n", group);
        return -1;
    }

    opts = qemu_opts_find(lists[i], id);
    if (!opts) {
        fprintf(stderr, "there is no %s \"%s\" defined\n",
                lists[i]->name, id);
        return -1;
    }

    if (-1 == qemu_opt_set(opts, arg, str+offset+1)) {
        fprintf(stderr, "failed to set \"%s\" for %s \"%s\"\n",
                arg, lists[i]->name, id);
        return -1;
    }
    return 0;
}

