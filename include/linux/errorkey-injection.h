/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020, Linutronix GmbH
 * Authors: Thomas Gleixner, Holger Dengler  */ #ifndef 
_LINUX_ERRORKEY_INJECTION_H #define _LINUX_ERRORKEY_INJECTION_H

#include <linux/types.h>
#include <asm-generic/errorkey-injection.h>

#ifdef CONFIG_ERRORKEY_INJECTION
extern bool errorkey_inject_filter_bool(struct static_key_false *key, 
bool *res); extern bool errorkey_inject_filter_ptr(struct 
static_key_false *key, void **res); #endif

#endif /* _LINUX_ERRORKEY_INJECTION_H */
