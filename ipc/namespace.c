/*
 * linux/ipc/namespace.c
 * Copyright (C) 2006 Pavel Emelyanov <xemul@openvz.org> OpenVZ, SWsoft Inc.
 */

#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/ipc_namespace.h>
#include <linux/rcupdate.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/user_namespace.h>
#include <linux/proc_ns.h>

#include "util.h"

static DEFINE_MUTEX(per_ipc_ops_mutex);
static HLIST_HEAD(per_ipc_ops_list);
static int ipc_priv_token_count;

static struct ucounts *inc_ipc_namespaces(struct user_namespace *ns)
{
	return inc_ucount(ns, current_euid(), UCOUNT_IPC_NAMESPACES);
}

static void dec_ipc_namespaces(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_IPC_NAMESPACES);
}

/*
 * Call each registered ipc mechanism's function to initialize its
 * private data.
 */
static int init_per_ipc_data(struct ipc_namespace *ns)
{
	int ret = 0;
	struct ipc_priv_ops_entry *entry;
	struct ipc_priv_data *priv_data;

	mutex_lock(&per_ipc_ops_mutex);
	INIT_HLIST_HEAD(&ns->ipc_priv_data_list);
	hlist_for_each_entry(entry, &per_ipc_ops_list, hlist) {
		priv_data = kzalloc(sizeof(*priv_data), GFP_KERNEL);
		if (!priv_data) {
			ret = -ENOMEM;
			break;
		}
		priv_data->token = entry->token;
		hlist_add_head(&priv_data->hlist, &ns->ipc_priv_data_list);
		ret = entry->ops->init(ns, entry->token);
		if (ret != 0)
			break;
	}
	mutex_unlock(&per_ipc_ops_mutex);
	return ret;
}


/*
 * Call each registered ipc mechanism's function to destroy
 * its private data.
 */
static void exit_per_ipc_data(struct ipc_namespace *ns)
{
	struct ipc_priv_ops_entry *entry;
	struct ipc_priv_data *data;
	struct hlist_node *tmp;

	mutex_lock(&per_ipc_ops_mutex);
	hlist_for_each_entry(entry, &per_ipc_ops_list, hlist) {
		hlist_for_each_entry_safe(data, tmp,
				&ns->ipc_priv_data_list, hlist) {
			if (entry->token == data->token) {
				entry->ops->exit(ns, entry->token);
				hlist_del(&data->hlist);
				kfree(data);
			}
		}
	}
	mutex_unlock(&per_ipc_ops_mutex);
}

static struct ipc_namespace *create_ipc_ns(struct user_namespace *user_ns,
					   struct ipc_namespace *old_ns)
{
	struct ipc_namespace *ns;
	struct ucounts *ucounts;
	int err;

	err = -ENOSPC;
	ucounts = inc_ipc_namespaces(user_ns);
	if (!ucounts)
		goto fail;

	err = -ENOMEM;
	ns = kmalloc(sizeof(struct ipc_namespace), GFP_KERNEL);
	if (ns == NULL)
		goto fail_dec;

	err = ns_alloc_inum(&ns->ns);
	if (err)
		goto fail_free;
	ns->ns.ops = &ipcns_operations;

	atomic_set(&ns->count, 1);

	err = init_per_ipc_data(ns);
	if (err)
		goto fail_per_ipc;

	ns->user_ns = get_user_ns(user_ns);
	ns->ucounts = ucounts;

	err = mq_init_ns(ns);
	if (err)
		goto fail_put;

	sem_init_ns(ns);
	msg_init_ns(ns);
	shm_init_ns(ns);

	return ns;

fail_put:
	put_user_ns(ns->user_ns);
	ns_free_inum(&ns->ns);
fail_per_ipc:
	exit_per_ipc_data(ns);
fail_free:
	kfree(ns);
fail_dec:
	dec_ipc_namespaces(ucounts);
fail:
	return ERR_PTR(err);
}

struct ipc_namespace *copy_ipcs(unsigned long flags,
	struct user_namespace *user_ns, struct ipc_namespace *ns)
{
	if (!(flags & CLONE_NEWIPC))
		return get_ipc_ns(ns);
	return create_ipc_ns(user_ns, ns);
}

/*
 * free_ipcs - free all ipcs of one type
 * @ns:   the namespace to remove the ipcs from
 * @ids:  the table of ipcs to free
 * @free: the function called to free each individual ipc
 *
 * Called for each kind of ipc when an ipc_namespace exits.
 */
void free_ipcs(struct ipc_namespace *ns, struct ipc_ids *ids,
	       void (*free)(struct ipc_namespace *, struct kern_ipc_perm *))
{
	struct kern_ipc_perm *perm;
	int next_id;
	int total, in_use;

	down_write(&ids->rwsem);

	in_use = ids->in_use;

	for (total = 0, next_id = 0; total < in_use; next_id++) {
		perm = idr_find(&ids->ipcs_idr, next_id);
		if (perm == NULL)
			continue;
		rcu_read_lock();
		ipc_lock_object(perm);
		free(ns, perm);
		total++;
	}
	up_write(&ids->rwsem);
}

static void free_ipc_ns(struct ipc_namespace *ns)
{
	sem_exit_ns(ns);
	msg_exit_ns(ns);
	shm_exit_ns(ns);
	exit_per_ipc_data(ns);

	dec_ipc_namespaces(ns->ucounts);
	put_user_ns(ns->user_ns);
	ns_free_inum(&ns->ns);
	kfree(ns);
}

/*
 * put_ipc_ns - drop a reference to an ipc namespace.
 * @ns: the namespace to put
 *
 * If this is the last task in the namespace exiting, and
 * it is dropping the refcount to 0, then it can race with
 * a task in another ipc namespace but in a mounts namespace
 * which has this ipcns's mqueuefs mounted, doing some action
 * with one of the mqueuefs files.  That can raise the refcount.
 * So dropping the refcount, and raising the refcount when
 * accessing it through the VFS, are protected with mq_lock.
 *
 * (Clearly, a task raising the refcount on its own ipc_ns
 * needn't take mq_lock since it can't race with the last task
 * in the ipcns exiting).
 */
void put_ipc_ns(struct ipc_namespace *ns)
{
	if (atomic_dec_and_lock(&ns->count, &mq_lock)) {
		mq_clear_sbinfo(ns);
		spin_unlock(&mq_lock);
		mq_put_mnt(ns);
		free_ipc_ns(ns);
	}
}

static inline struct ipc_namespace *to_ipc_ns(struct ns_common *ns)
{
	return container_of(ns, struct ipc_namespace, ns);
}

static struct ns_common *ipcns_get(struct task_struct *task)
{
	struct ipc_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy)
		ns = get_ipc_ns(nsproxy->ipc_ns);
	task_unlock(task);

	return ns ? &ns->ns : NULL;
}

static void ipcns_put(struct ns_common *ns)
{
	return put_ipc_ns(to_ipc_ns(ns));
}

static int ipcns_install(struct nsproxy *nsproxy, struct ns_common *new)
{
	struct ipc_namespace *ns = to_ipc_ns(new);
	if (!ns_capable(ns->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	/* Ditch state from the old ipc namespace */
	exit_sem(current);
	put_ipc_ns(nsproxy->ipc_ns);
	nsproxy->ipc_ns = get_ipc_ns(ns);
	return 0;
}

static struct user_namespace *ipcns_owner(struct ns_common *ns)
{
	return to_ipc_ns(ns)->user_ns;
}

int register_ipc_priv_ops(struct ipc_priv_ops *ops)
{
	int ret = -EBUSY;
	struct ipc_priv_ops_entry *entry;
	struct ipc_priv_data *priv_data;

	/* The register is only allowed in the init IPC namespace */
	if (&init_ipc_ns != current->nsproxy->ipc_ns) {
		pr_err("%s(): Being called from a non-init namespace.\n",
			__func__);
		return -EINVAL;
	}

	if (!ops)
		return -EINVAL;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->ops = ops;
	entry->token = ipc_priv_token_count;

	mutex_lock(&per_ipc_ops_mutex);
	hlist_add_head(&entry->hlist, &per_ipc_ops_list);

	/* Create the ipc_priv_data for this IPC */
	priv_data = kzalloc(sizeof(*priv_data), GFP_KERNEL);
	if (!priv_data) {
		ret = -ENOMEM;
		goto priv_data_err;
	}
	priv_data->token = entry->token;
	hlist_add_head(&priv_data->hlist, &init_ipc_ns.ipc_priv_data_list);

	ret = entry->ops->init(&init_ipc_ns, ipc_priv_token_count);
	if (ret < 0) {
		goto init_err;
	} else {
		ret = ipc_priv_token_count++;
		goto success;
	}

init_err:
	hlist_del(&priv_data->hlist);
	kfree(priv_data);
priv_data_err:
	hlist_del(&entry->hlist);
	kfree(entry);
success:
	mutex_unlock(&per_ipc_ops_mutex);
	return ret;
}

void unregister_per_ipc_ops(int token)
{
	struct ipc_priv_ops_entry *entry;
	struct ipc_priv_data *data;
	bool found = false;

	/* The un-register is only allowed in the init IPC namespace */
	if (&init_ipc_ns != current->nsproxy->ipc_ns) {
		pr_err("%s(): Being called from a non-init namespace.\n",
			__func__);
		return;
	}

	mutex_lock(&per_ipc_ops_mutex);

	/* First, remove the operations from the per_ipc_ops_list. */
	hlist_for_each_entry(entry, &per_ipc_ops_list, hlist) {
		if (entry->token == token) {
			found = true;
			break;
		}
	}
	if (found) {
		entry->ops->exit(&init_ipc_ns, token);
		hlist_del(&entry->hlist);
		kfree(entry);
	}

	found = false;
	hlist_for_each_entry(data, &init_ipc_ns.ipc_priv_data_list, hlist) {
		if (data->token == token) {
			found = true;
			break;
		}
	}
	if (found) {
		hlist_del(&data->hlist);
		kfree(data);
	}

	mutex_unlock(&per_ipc_ops_mutex);
}

void ipc_assign_generic_locked(struct ipc_namespace *ns, void *data, int token)
{
	struct ipc_priv_data *entry;

	hlist_for_each_entry(entry, &ns->ipc_priv_data_list, hlist) {
		if (entry->token == token) {
			entry->gen = data;
			break;
		}
	}
}

void *ipc_access_generic_locked(struct ipc_namespace *ns, int token)
{
	struct ipc_priv_data *entry;
	void *ret = NULL;

	hlist_for_each_entry(entry, &ns->ipc_priv_data_list, hlist) {
		if (entry->token == token) {
			ret = entry->gen;
			break;
		}
	}
	return ret;
}

void *ipc_access_generic(struct ipc_namespace *ns, int token)
{
	void *ret = NULL;

	mutex_lock(&per_ipc_ops_mutex);
	ret = ipc_access_generic_locked(ns, token);
	mutex_unlock(&per_ipc_ops_mutex);

	return ret;
}

const struct proc_ns_operations ipcns_operations = {
	.name		= "ipc",
	.type		= CLONE_NEWIPC,
	.get		= ipcns_get,
	.put		= ipcns_put,
	.install	= ipcns_install,
	.owner		= ipcns_owner,
};
