// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020, Linutronix GmbH
 * Authors: Thomas Gleixner, Holger Dengler  */ #include 
<linux/errorkey-injection.h>

#include <linux/printk.h>

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
