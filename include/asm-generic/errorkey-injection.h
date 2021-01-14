/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020, Linutronix GmbH
 * Authors: Thomas Gleixner, Holger Dengler  */ #ifndef 
_ASM_GENERIC_ERRORKEY_INJECTION_H
#define _ASM_GENERIC_ERRORKEY_INJECTION_H

enum {
	EKI_KEYTYPE_NONE,
	EKI_KEYTYPE_NULL,
	EKI_KEYTYPE_TRUE,
	EKI_KEYTYPE_FALSE,
};

struct errorkey_inject_entry {
	struct static_key_false	*key;
	void			*addr;
	int			type;
} __aligned(32);

#ifdef CONFIG_ERRORKEY_INJECTION
#define EKEYINJECT_ENTRY(_key, _type, _addr)				\
static struct errorkey_inject_entry __used				\
	__attribute__((__section__("_errorkey_injection_list")))	\
	entry = {							\
		.key = _key,						\
		.addr = _addr,						\
		.type = _type,						\
	}

#define __ERROR_BOOL(expr, keytype, _where)				\
({									\
	static DEFINE_STATIC_KEY_FALSE(einject_key);			\
	EKEYINJECT_ENTRY(&einject_key, keytype, _where);		\
	bool __res;							\
									\
	if (!static_branch_unlikely(&einject_key) ||			\
	    !errorkey_inject_filter_bool(&einject_key, &__res))		\
		__res = expr;						\
	__res;								\
})

#define __ERROR_PTR(expr, keytype, _where)				\
({									\
	static DEFINE_STATIC_KEY_FALSE(einject_key);			\
	EKEYINJECT_ENTRY(&einject_key, keytype, _where);		\
	void *__res;							\
									\
	if (!static_branch_unlikely(&einject_key) ||			\
	    !errorkey_inject_filter_ptr(&einject_key, &__res))		\
		__res = expr;						\
	__res;								\
})

#define ERROR_TRUE(expr)						\
({									\
	__label__ _where;						\
_where:									\
	__ERROR_BOOL(expr, EKI_KEYTYPE_TRUE, &&_where);			\
})

#define ERROR_FALSE(expr)						\
({									\
	__label__ _where;						\
_where:									\
	__ERROR_BOOL(expr, EKI_KEYTYPE_FALSE, &&_where);		\
})

#define ERROR_NULL(expr)						\
({									\
	__label__ _where;						\
_where:									\
	__ERROR_PTR(expr, EKI_KEYTYPE_NULL, &&_where);			\
})
#else
#define ERROR_TRUE(expr)	expr
#define ERROR_FALSE(expr)	expr
#define ERROR_NULL(expr)	expr
#endif

#endif /* _ASM_GENERIC_ERRORKEY_INJECTION_H */
