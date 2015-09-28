/* 
 *  Valvula: a high performance policy daemon
 *  Copyright (C) 2014 Advanced Software Production Line, S.L.
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
 *  For comercial support about integrating valvula or any other ASPL
 *  software production please contact as at:
 *          
 *      Postal address:
 *         Advanced Software Production Line, S.L.
 *         C/ Antonio Suarez Nº 10, 
 *         Edificio Alius A, Despacho 102
 *         Alcalá de Henares 28802 (Madrid)
 *         Spain
 *
 *      Email address:
 *         info@aspl.es - http://www.aspl.es/valvula
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
 * \page valvulad_mod_mquota mod-mquota: Valvulad sending control quota module
 *
 * \section mquota_intro Introduction to mquota
 *
 * Mod-Mquota applies to sending mail operations when they are
 * authenticated. It is mainly designed for shared hosting solutions
 * where it is required to limit user sending rate and to control and
 * minimize the impact of compromised accounts.
 *
 * The plugin has a straightforward operation method were you configure
 * different time periods inside which you limit the amount of mails
 * that can be sent by minute, by hour and inside the total
 * period. 
 *
 * Those limits apply to account level and whole domain level (so
 * "smart" users cannot use user1@yourdomain.com,
 * user2@yourdomani.com..and so on to bypass limits).
 *
 * \section mquota_configuration_example mod-mquota Configuration examples
 *
 * Take a look inside &lt;default-sending-quota> node inside /etc/valvula/valvula.conf and you'll find something like this:
 * \code
 *    <!-- sending and receiving quotas: used by mod-mquota  -->
 *    <default-sending-quota status="full" if-no-match="first">
 *      <!-- account limit: 50/minute,  250/hour  and  750/global from 09:00 to 21:00 
 *           domain limit:  100/minute, 375/hour  and 1100/global 
 *
 *           note: use -1 to disable any of the limits.  
 *           For example, to disable global limit, use globa-limit="-1" 
 *      -->
 *      <limit label='day quota' from="9:00" to="21:00"  status="full" 
 *	     minute-limit="50" hour-limit="250" global-limit="750" 
 *	     domain-minute-limit="100" domain-hour-limit="375" domain-global-limit="1100" />
 *
 *      <!-- limit 15/minute, 50/hour  and 150/global from 21:00 to 09:00 -->
 *      <limit label='night quota' from="21:00" to="9:00"  status="full" 
 *	     minute-limit="15" hour-limit="50" global-limit="150" 
 *	     domain-minute-limit="15" domain-hour-limit="50" domain-global-limit="150" />
 *    </default-sending-quota>
 * \endcode
 *
 * Taking as a reference previous example, operation mode is applied following next rules:
 *
 * - 1. First it is found what period applies at this time by looking into <b>from</b> and <b>to</b> attribute on every <b>&lt;limit></b> node. 
 *
 * - 2. If no period matches, <b>&lt;if-no-match></b> attribute comes into play (we will talk about this later). 
 *
 * - 3. Once the period is selected, accounting is done to the user account and domain looking at the self-explaining limits. For example, if the user sends more that 50 mails by minute at 11:00 am, then valvula will reject accepting sending more.
 *
 * - 4. Again, if the total amount sent by a domain (including all accounts involved in previous send operations) reached provided limits (for example, domain-minute-limit) then valvula will reject accepting sending more. 
 *
 * - 5. Finally, if the minute limit is reached, then a minute after it will be restarted so the user only have to wait that time. The same applies to the hour limit and to the global limit. 
 *
 * \section mquota_if_no_match_period mod-mquota Selecting a default period when no match is found (<if-no-match>)
 *
 * When no period is found to apply, if-no-match attribute is used (at
 * <default-sending-quota>). This allows to define a particular period
 * where limits applies and then, outside that limit, a default
 * period, no limit or just reject is applied.
 *
 * Allowed values are:
 *
 * - 1) first : if no period matches, then the first period in the definition list is used.
 *
 * - 2) no-limit : if no period matches, then, apply no limit and let the user to send without limit. This is quite useful to define night limits. That is, you only have to define a period to cover nights period and then, during the day no limit is applied where you can have a better supervision.
 *
 * - 3) reject : if no period matches, then just reject the send operation.
 * 
 *
 * \section mquota_exceptions mod-mquota Configuring exceptions to certain users
 *
 * In the case you want to apply quotas in a general manner but need a
 * way to avoid applying these quotas to certain users, then connect
 * to the valvula database (take a look what's defined at
 * <b>/etc/valvula/valvula.conf</b>) and then run the following query:
 *
 * \code
 * INSERT INTO mquota_exception (is_active, sasl_user) VALUES ('1', 'test4@unlimited2.com');
 * \endcode
 *
 * This will allow sasl user test4@unlimited2.com to send without any restriction.
 *
 */
