#ifndef _DENTRY_H_
#define _DENTRY_H_ 1

#include <linux/dcache.h>
#include <linux/types.h>

#define DENTRY_MAX_DEPTH 16

struct bpf_map_def SEC("maps/pathnames") pathnames = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(__u32),
    .value_size = sizeof(struct path_leaf_t),
    .max_entries = 32000,
    .pinning = 0,
    .namespace = "",
};

static __attribute__((always_inline)) int resolve_dentry(struct dentry *dentry, u32 pathname_key) {
    struct path_leaf_t map_value = {};
    u32 id;
    u32 next_id = pathname_key;
    struct qstr qstr;

#pragma unroll
    for (int i = 0; i < DENTRY_MAX_DEPTH; i++)
    {
        bpf_probe_read(&qstr, sizeof(qstr), &dentry->d_name);

        struct dentry *d_parent;
        bpf_probe_read(&d_parent, sizeof(d_parent), &dentry->d_parent);

        id = next_id;
        next_id = (dentry == d_parent) ? 0 : bpf_get_prandom_u32();
        bpf_probe_read_str(map_value.name, NAME_MAX, (void*) qstr.name);

        if (map_value.name[0] == 47 || map_value.name[0] == 0)
            next_id = 0;

        map_value.parent = next_id;

        bpf_map_update_elem(&pathnames, &id, &map_value, BPF_ANY);

        dentry = d_parent;
        if (next_id == 0)
            return i + 1;
    }

    // If the last next_id isn't null, this means that there are still other parents to fetch.
    // TODO: use BPF_PROG_ARRAY to recursively fetch 32 more times. For now, add a fake parent to notify
    // that we couldn't fetch everything.

    if (next_id != 0) {
        map_value.name[0] = 0;
        map_value.parent = 0;
        bpf_map_update_elem(&pathnames, &next_id, &map_value, BPF_ANY);
    }

    return DENTRY_MAX_DEPTH;
}

int __attribute__((always_inline)) get_inode_mount_id(struct inode *dir) {
    // Mount ID
    int mount_id;
    struct super_block *spb;
    bpf_probe_read(&spb, sizeof(spb), &dir->i_sb);

    struct list_head s_mounts;
    bpf_probe_read(&s_mounts, sizeof(s_mounts), &spb->s_mounts);

    bpf_probe_read(&mount_id, sizeof(int), (void *) s_mounts.next + 172);
    // bpf_probe_read(&mount_id, sizeof(int), &((struct mount *) s_mounts.next)->mnt_id);

    return mount_id;
}

long __attribute__((always_inline)) get_dentry_inode(struct dentry *dentry) {
    long inode;
    struct inode *d_inode;
    bpf_probe_read(&d_inode, sizeof(d_inode), &dentry->d_inode);
    bpf_probe_read(&inode, sizeof(inode), &d_inode->i_ino);
    return inode;
}

#endif