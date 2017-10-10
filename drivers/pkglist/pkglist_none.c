/*
 * Copyright (C) 2017 Google Inc., Author: Daniel Rosenberg <drosen@google.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ctype.h>
#include <linux/dcache.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pkglist.h>

kuid_t get_appid(const char *key)
{
	return make_kuid(&init_user_ns, 0);
}
EXPORT_SYMBOL(get_appid);

kgid_t get_ext_gid(const char *key)
{
	return make_kgid(&init_user_ns, 0);
}
EXPORT_SYMBOL(get_ext_gid);

bool is_excluded(const char *key, uid_t user)
{
	return false;
}
EXPORT_SYMBOL(is_excluded);

kuid_t get_allowed_appid(const char *key, uid_t user)
{
	return make_kuid(&init_user_ns, 0);
}
EXPORT_SYMBOL(get_allowed_appid);

void pkglist_register_update_listener(struct pkg_list *pkg) { }
EXPORT_SYMBOL(pkglist_register_update_listener);

void pkglist_unregister_update_listener(struct pkg_list *pkg) { }
EXPORT_SYMBOL(pkglist_unregister_update_listener);

int __init pkglist_init(void)
{
	return 0;
}
module_init(pkglist_init);

void pkglist_exit(void) { }

module_exit(pkglist_exit);
