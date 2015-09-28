/* 
 *  mod-pquota: quota module with punish period for valvula
 *  Copyright (C) 2015 Wouter Paesen <wouter@blue-gate.be>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2.1 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307 USA
 *  
 *  You may find a copy of the license under this software is released
 *  at COPYING file. 
 *
 */
#include <mod-pquota.h>

/* use this declarations to avoid c++ compilers to mangle exported
 * names. */
BEGIN_C_DECLS

typedef struct _ModPQuotaBucket {
	ModPQuotaBucketConfig config[1];

	int life;
	axlHash *hash;
} ModPQuotaBucket;
	
ValvuladCtx * ctx = NULL;

/* hashes to track activity */
ValvulaMutex hash_mutex;

axl_bool __mod_pquota_enable_debug = axl_false;
axl_bool __mod_pquota_enabled = axl_false;

/* these keep track of current punishment */
axlHash * __mod_pquota_punish;
axlHash * __mod_pquota_domain_punish;

/* tracking buckets */
ModPQuotaBucket       __mod_pquota_bucket[2];

/** 
 * Internal handler used to resent all hashes according to its current
 * configuration.
 */
axl_bool __mod_pquota_minute_handler        (ValvulaCtx  * _ctx, 
					     axlPointer   user_data,
					     axlPointer   user_data2)
{
	int i;
	axlHash         * hash;
	axlHashCursor   * iter;

	if (__mod_pquota_enable_debug) {
		msg ("mod-pquota: updating accounting info"); 
	} /* end if */

	/* lock */
	valvula_mutex_lock (&hash_mutex);

	/* refresh bucket lists */
	for (i=0; i<2; ++i) {
		ModPQuotaBucket *bucket = & __mod_pquota_bucket[i];

		if (! bucket->config->enabled) continue;
		if ((--bucket->life) > 0) continue;

		if (__mod_pquota_enable_debug) {
			msg ("mod-pquota: emptying bucket %d", i);
		}
				
		hash = bucket->hash;
		bucket->hash = axl_hash_new (axl_hash_string, axl_hash_equal_string);
		axl_hash_free (hash);

		bucket->life = bucket->config->duration;
	}

	/* keep track of punishment buckets */
	iter = axl_hash_cursor_new (__mod_pquota_punish);
	while (axl_hash_cursor_has_item (iter)) {
		long *timeout = axl_hash_cursor_get_value (iter);

		if ((! timeout) || (--timeout[0] <= 0)) {
			msg ("mod-pquota: end of punish period for %s", (const char *)axl_hash_cursor_get_key (iter));
			axl_hash_cursor_remove (iter);
		}

		axl_hash_cursor_next (iter);
	}

	iter = axl_hash_cursor_new (__mod_pquota_domain_punish);
	while (axl_hash_cursor_has_item (iter)) {
		long *timeout = axl_hash_cursor_get_value (iter);

		if ((! timeout) || (--timeout[0] <= 0)) {
			msg ("mod-pquota: end of punish period for %s", (const char *)axl_hash_cursor_get_key (iter));
			axl_hash_cursor_remove (iter);
		}

		axl_hash_cursor_next (iter);
	}

	/* lock */
	valvula_mutex_unlock (&hash_mutex);
			
	return axl_false; /* signal the system to receive a call again */
}

static axl_bool pquota_bucket_init (ModPQuotaBucket *bucket, axl_bool is_domain, axlNode *node)
{
	ModPQuotaBucketConfig *cfg = bucket->config;

	if (! node) {
		cfg->enabled  = axl_false;
	} else {
		cfg->domain   = is_domain;
		cfg->duration = valvula_support_strtod (ATTR_VALUE (node, "duration"), NULL);
		cfg->size     = valvula_support_strtod (ATTR_VALUE (node, "limit"), NULL);
		cfg->punish   = valvula_support_strtod (ATTR_VALUE (node, "punish"), NULL);
		cfg->enabled  = ((cfg->duration > 0) || (cfg->size > 0) || (cfg->punish > 0)) ? axl_true : axl_false;
	}
	
	if (cfg->enabled) {
		msg ("add %s pquota, duration=%d, limit=%d, punish time=%d",
				 cfg->domain ? "domain" : "user",
				 cfg->duration, cfg->size, cfg->punish);
		
		bucket->life = cfg->duration;
		bucket->hash = axl_hash_new (axl_hash_string, axl_hash_equal_string);
	} else {
		bucket->hash = NULL;
	}

	return cfg->enabled;
}

				
/** 
 * @brief Init function, perform all the necessary code to register
 * profiles, configure Vortex, and any other init task. The function
 * must return true to signal that the module was properly initialized
 * Otherwise, false must be returned.
 */
static int  pquota_init (ValvuladCtx * _ctx)
{
	axlNode *node;
	
	/* configure the module */
	ctx = _ctx;

	msg ("Valvulad pquota module: init");
	
	/* init mutex */
	valvula_mutex_create (&hash_mutex);
	
	/* init tracking hashes */
	__mod_pquota_punish   = axl_hash_new (axl_hash_string, axl_hash_equal_string);
	__mod_pquota_domain_punish = axl_hash_new (axl_hash_string, axl_hash_equal_string);;

	/* get debug support */
	node = axl_doc_get (_ctx->config, "/valvula/enviroment/pq-quota");
	if (node) {
		if (HAS_ATTR_VALUE (node, "debug", "yes")) {
			__mod_pquota_enable_debug = axl_true;
			msg ("Valvulad pquota module debug enabled");
		} /* end if */

	} /* end if */

	/* parse limits */
	__mod_pquota_enabled = axl_false;

	if (pquota_bucket_init (& __mod_pquota_bucket[0],
													axl_false,
													axl_doc_get (_ctx->config, "/valvula/enviroment/pq-quota/limit")
													))
		__mod_pquota_enabled = true;

	if (pquota_bucket_init (& __mod_pquota_bucket[1],
												axl_true,
												axl_doc_get (_ctx->config, "/valvula/enviroment/pq-quota/domain-limit")
													))
		__mod_pquota_enabled = true;

	/* create databases to be used by the module */
	valvulad_db_ensure_table (ctx, 
				  /* table name */
				  "pquota_exception", 
				  /* attributes */
				  "id", "autoincrement int", 
				  /* rule status */
				  "is_active", "int",
				  /* sasl user */
				  "sasl_user", "varchar(1024)",
				  /* rule description */
				  "description", "varchar(500)",
				  NULL);

	/* install time handlers */
	valvula_thread_pool_new_event (ctx->ctx, 60000000, __mod_pquota_minute_handler, NULL, NULL);

	return axl_true;
}

axl_bool __mod_pquota_update_quota (ValvuladCtx *_ctx, ValvulaRequest *req,
																ModPQuotaBucket *bucket, const char *key)
{
	long *value = (long *)axl_hash_get (bucket->hash, (axlPointer) key);

	if (! value) {
		value = axl_new (long, 1);
		axl_hash_insert_full (bucket->hash, (axlPointer) axl_strdup (key), axl_free,
													(axlPointer) value, axl_free);
	}

	if (__mod_pquota_enable_debug) {
		msg ("quota for %s is %ld", key, value[0]);
	}

	if (++ value[0] > bucket->config->size) {
		if (bucket->config->domain) {
			long *lifetime = axl_new (long, 1);

			lifetime[0] = bucket->config->punish;

			axl_hash_insert_full (__mod_pquota_domain_punish, (axlPointer) axl_strdup (key), axl_free,
														(axlPointer) lifetime, axl_free);

			valvulad_reject (_ctx, VALVULA_STATE_REJECT, req,
											 "pquota domain %s limit reached, wait %ld minutes",
											 key, lifetime[0]);

		} else {
			long *lifetime = axl_new (long, 1);

			lifetime[0] = bucket->config->punish;

			axl_hash_insert_full (__mod_pquota_punish, (axlPointer) axl_strdup (key), axl_free,
														(axlPointer) lifetime, axl_free);

			valvulad_reject (_ctx, VALVULA_STATE_REJECT, req,
											 "pquota user %s limit reached, wait %ld minutes",
											 key, lifetime[0]);
		}

		return axl_true;
	}

	return axl_false;
}

axl_bool __mod_pquota_check_punished (ValvuladCtx *_ctx, ValvulaRequest *req,
																			const char * sasl_user, const char * sasl_domain)
{
	long *lifetime = (long *)axl_hash_get (__mod_pquota_punish, (axlPointer) sasl_user);

	if (lifetime) {
		valvulad_reject (_ctx, VALVULA_STATE_REJECT, req,
										 "pquota user %s limit reached, wait %ld minutes",
										 sasl_user, lifetime[0]);

		return axl_true;
	}

	if (sasl_domain) {
		lifetime = (long *)axl_hash_get (__mod_pquota_domain_punish, (axlPointer) sasl_domain);
	} else {
		lifetime = NULL;
	}

	if (lifetime) {
			valvulad_reject (_ctx, VALVULA_STATE_REJECT, req,
											 "pquota domain %s limit reached, wait %ld minutes",
											 sasl_domain, lifetime[0]);

		return axl_true;
	}
	
	return axl_false;
}

axl_bool __mod_pquota_has_exception (const char * sasl_user, const char * sasl_domain)
{
	/* check if has an exception */
	if (valvulad_db_boolean_query (ctx, "SELECT * FROM mquota_exception WHERE sasl_user = '%s' OR sasl_user = '%s'", sasl_user, sasl_domain)) {
		return axl_true; /* report we have an exception */
	} /* end if */

	/* no exception found */
	return axl_false;
}

/** 
 * @brief Process request for the module.
 */
ValvulaState pquota_process_request (ValvulaCtx        * _ctx, 
				     ValvulaConnection * connection, 
				     ValvulaRequest    * request,
				     axlPointer          request_data,
				     char             ** message)
{
	const char     * sasl_user;
	const char     * sasl_domain;
	axl_bool has_domain = axl_false;
	axl_bool denied = axl_false;
	int i;
	
	/* do nothing if module is disabled */
	if (! __mod_pquota_enabled) {
		return VALVULA_STATE_DUNNO;
	}
	
	/* skip if request is not autenticated */
	if (! valvula_is_authenticated (request)) {
		return VALVULA_STATE_DUNNO;
	}
	
	/* get sasl user */
	sasl_user   = valvula_get_sasl_user (request);
	sasl_domain = valvula_get_domain (sasl_user);
	/* msg ("Checking sasl user: %s", sasl_user); */

	/* check user exceptions to avoid applying quotas to him */
	if (__mod_pquota_has_exception (sasl_user, sasl_domain))
		return VALVULA_STATE_DUNNO;

	if (strstr (sasl_user, "@"))
		has_domain = axl_true;
	
	/* check if the sender is in a punish list */
	valvula_mutex_lock (&hash_mutex);

	if (__mod_pquota_check_punished (ctx, request, sasl_user, has_domain ? sasl_domain : NULL)) {
		valvula_mutex_unlock (&hash_mutex);
		return VALVULA_STATE_REJECT;
	}

	for (i=0; i<2; ++i) {
		ModPQuotaBucket *bucket = & __mod_pquota_bucket[i];

		if (! bucket->config->enabled) continue;

		if (bucket->config->domain) {
			if (has_domain)
				denied |= __mod_pquota_update_quota (ctx, request, bucket, sasl_domain);
		} else {
			denied |= __mod_pquota_update_quota (ctx, request, bucket, sasl_user);
		}
	}

	/* release */
	valvula_mutex_unlock (&hash_mutex);

	/* by default report return dunno */
	return denied ? VALVULA_STATE_REJECT : VALVULA_STATE_DUNNO;
}

/** 
 * @brief Close function called once the valvulad server wants to
 * unload the module or it is being closed. All resource deallocation
 * and stop operation required must be done here.
 */
void pquota_close (ValvuladCtx * ctx)
{
	int i;
	
	msg ("Valvulad pquota module: close");

	/* release hash */
	axl_hash_free (__mod_pquota_punish);
	axl_hash_free (__mod_pquota_domain_punish);

	for (i=0; i<2; ++i)
		if (__mod_pquota_bucket[i].hash) axl_hash_free (__mod_pquota_bucket[i].hash);

	/* release mutex */
	valvula_mutex_destroy (&hash_mutex);
	return;
}

/** 
 * @brief Public entry point for the module to be loaded. This is the
 * symbol the valvulad will lookup to load the rest of items.
 */
ValvuladModDef module_def = {
	"mod-pquota",
	"Bluegate mail quotas module",
	pquota_init,
	pquota_close,
	pquota_process_request,
	NULL,
	NULL
};

END_C_DECLS


/** 
 * \page valvulad_mod_pquota mod-pquota: sending control quota module
 *
 * \section pquota_intro Introduction to pquota
 *
 * Mod-Pquota applies to sending mail operations when they are
 * authenticated. It is mainly designed for shared hosting solutions
 * where it is required to limit user sending rate and to control and
 * minimize the impact of compromised accounts.
 *
 * The plugin has a straightforward operation method where you configure
 * an accounting period, an accounting limit and a punishment duration.
 *
 * If more e-mails than the defined limit are sent in the configured 
 * period the account will be barred from sending e-mail for the 
 * punishment period.
 *
 *
 * Those limits can apply to account level and whole domain level (so
 * "smart" users cannot use user1@yourdomain.com,
 * user2@yourdomani.com..and so on to bypass limits).
 *
 * \section pquota_configuration_example mod-pquota Configuration examples
 *
 * The mod-pquota module is configured by a &lt;pq-quota> node under the 
 * &lt;enviroment> node inside /etc/valvula/valvula.conf:
 *
 * \code
 *    <!-- sending and receiving quotas: used by mod-pquota  -->
 *    <pq-quota debug="no">
 *      <!-- user limit, 50/5 minutes, punishment for 33 minutes -->
 *      <limit duration="5" limit="50" punish="33" />
 *      <!-- domain limit, 100/10 minutes, punisment for 66 minutes -->
 *      <domain-limit duration="10" limit="100" punish="66" />
 *    </pq-quota>
 * \endcode
 *
 * Taking as a reference previous example, operation mode is applied following next rules:
 *
 * - 1. First the punish list for the user and the domains is consulted.  If either the
 *      user or the domain is found in the punish list the e-mail is rejected.
 *
 * - 2. The accounting bucket for the user and the account bucket for the domain is 
 *      incremented by 1.
 *
 * - 3. If either accounting bucket overflows, the user or the domain is added to the
 *      punish list and the e-mail is rejected.
 *
 *
 * Both the &lt;limit> node and the &lt;domain-limit> node can be left out to disable
 * the corresponding limit.   If none of both are found in the config, the module
 * is effectively disabled.
 *
 * \section pquota_exceptions mod-pquota Configuring exceptions to certain users
 *
 * In the case you want to apply quotas in a general manner but need a
 * way to avoid applying these quotas to certain users, then connect
 * to the valvula database (take a look what's defined at
 * <b>/etc/valvula/valvula.conf</b>) and then run the following query:
 *
 * \code
 * INSERT INTO pquota_exception (is_active, sasl_user) VALUES ('1', 'test4@unlimited2.com');
 * \endcode
 *
 * This will allow sasl user test4@unlimited2.com to send without any restriction.
 *
 */
