// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020, Linutronix GmbH
 * Authors: Thomas Gleixner, Holger Dengler  */ #include 
<linux/errorkey-injection.h>

#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/slab.h>

static LIST_HEAD(eki_list);
static DEFINE_MUTEX(eki_mutex);

struct ekey_entry {
       struct list_head                list;
       struct static_key_false         *key;
       int                             type;
       struct dentry                   *dbgfs;
};

static char *eki_name = "eki";
static struct dentry *eki_debugfs_root;

bool errorkey_inject_filter_bool(struct static_key_false *key, bool 
*res) {
	int keytype = EKI_KEYTYPE_NONE;

	/* lookup and check filter */
	/* return false, if filter doesn't match */

	/* lookup key and keytype*/
	keytype = EKI_KEYTYPE_TRUE;

	/* set injected result */
	switch (keytype) {
	case EKI_KEYTYPE_TRUE:
		*res = true;
		break;
	case EKI_KEYTYPE_FALSE:
		*res = false;
		break;
	default:
		pr_err("eki: unknown type %d", keytype);
		return false;
	}

	return true;
}

bool errorkey_inject_filter_ptr(struct static_key_false *key, void 
**res) {
	int keytype = EKI_KEYTYPE_NONE;

	/* lookup and check filter */
	/* return false, if filter doesn't match */

	/* lookup key and keytype*/
	keytype = EKI_KEYTYPE_NULL;

	/* set injected result */
	switch (keytype) {
	case EKI_KEYTYPE_NULL:
		*res = NULL;
		break;
	default:
		pr_err("eki: unknown type %d", keytype);
		return false;
	}

	return true;
}


static int static_key_set(void *data, u64 val) {
	struct static_key *key = (struct static_key *)data;

	if (val)
		static_key_enable(key);
	else
		static_key_disable(key);
	return 0;
}

static int static_key_get(void *data, u64 *val) {
	struct static_key *key = (struct static_key *)data;

	*val = !!(static_key_count(key));
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_static_key, static_key_get,
static_key_set, "%lld\n");

static struct dentry *eki_debugfs(unsigned long entry, struct
ekey_entry *ee) {
	struct dentry *dir;
	char name[KSYM_NAME_LEN], *cat;

	/* truncate size from symbol string */
	sprint_symbol(name, entry);
	if ((cat = strnchr(name, KSYM_NAME_LEN, '/')))
	    *cat = '\0';

	dir = debugfs_create_dir(name, eki_debugfs_root);
	if (!dir) {
		pr_err("eki: debugfs: unable to create parent\n");
		return NULL;
	}

	debugfs_create_file_unsafe("enable", 0660, dir, &ee->key->key,
&fops_static_key);

	return dir;
}

static void populate_ekey_injection_list(struct errorkey_inject_entry *start,
					 struct errorkey_inject_entry *end,
					 void *priv)
{
	struct errorkey_inject_entry *iter;
	struct ekey_entry *ee;
	unsigned long entry, size, offset;

	pr_info("eki: %s\n", __FUNCTION__);

	mutex_lock(&eki_mutex);
	for (iter = start; iter < end; iter++) {
		entry = arch_deref_entry_point((void *)iter->addr);
		pr_info("eki: entry 0x%lx\n", (unsigned long)iter->addr);

		if (!kernel_text_address(entry)) {
			pr_err("eki.addr 0x%lx: not in kernel space\n", entry);
			continue;
		}
		if (!kallsyms_lookup_size_offset(entry, &size, &offset)) {
			pr_err("eki.addr 0x%lx: unable to lookup symbol\n", entry);
			continue;
		}

		ee = (struct ekey_entry *)kzalloc(sizeof(struct ekey_entry), GFP_KERNEL);
		if (!ee) {
			pr_err("eki: unable to create ekey_entry\n");
			continue;
		}

		ee->key = iter->key;
		ee->type = iter->type;
		ee->dbgfs = eki_debugfs(entry, ee);

		list_add(&ee->list, &eki_list);
	}
	mutex_unlock(&eki_mutex);
}

/* Markers of the _error_inject_whitelist section */ extern struct
errorkey_inject_entry __start_errorkey_injection_list[];
extern struct errorkey_inject_entry __stop_errorkey_injection_list[];

static void __init populate_kernel_errorkey_injections(void)
{
	populate_ekey_injection_list(__start_errorkey_injection_list,
				     __stop_errorkey_injection_list,
				     NULL);
}

static int __init init_errorkey_injection(void) {
	eki_debugfs_root = debugfs_create_dir(eki_name, NULL);
	populate_kernel_errorkey_injections();
	return 0;
}
late_initcall(init_errorkey_injection);
