/**
 * Copyright (C) 2007-2009 Felipe Contreras
 * Copyright (C) 1998-2006 Pidgin (see pidgin-copyright)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "pn_contact.h"
#include "pn_contact_priv.h"
#include "pecan_contactlist_priv.h"
#include "pn_log.h"
#include "pn_util.h"

#include "cvr/slp.h"
#include "pn_dp_manager.h" /* for pn_dp_manager_contact_set_object */

#include <string.h>
#include <stdbool.h>

#ifdef HAVE_LIBPURPLE
#include "fix_purple.h"

/* libpurple stuff. */
#include "fix_purple_win32.h"
#include <cipher.h>
#include <account.h>
#endif /* HAVE_LIBPURPLE */

PnContact *
pn_contact_new (PecanContactList *contactlist)
{
    PnContact *contact;

    contact = g_new0 (PnContact, 1);

    contact->contactlist = contactlist;
    contact->groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    return contact;
}

void
pn_contact_free (PnContact *contact)
{
    if (!contact)
        return;

    if (contact->clientcaps)
        g_hash_table_destroy (contact->clientcaps);

    g_hash_table_destroy (contact->groups);

#if defined(PECAN_CVR)
    if (contact->msnobj)
        pn_msnobj_free (contact->msnobj);
#endif /* defined(PECAN_CVR) */

    g_free (contact->passport);
    g_free (contact->friendly_name);
    g_free (contact->personal_message);
    g_free (contact->store_name);
    g_free (contact->guid);
    g_free (contact->phone.home);
    g_free (contact->phone.work);
    g_free (contact->phone.mobile);

    g_free (contact);
}

void
pn_contact_update (PnContact *contact)
{
#ifdef HAVE_LIBPURPLE
    PurpleAccount *account;
    const char *pidgin_status;
    gboolean idle = FALSE;

    account = msn_session_get_user_data (contact->contactlist->session);

    switch (contact->status)
    {
        case PN_STATUS_OFFLINE:
            pidgin_status = "offline"; break;
        case PN_STATUS_BUSY:
            pidgin_status = "busy"; break;
        case PN_STATUS_BRB:
            pidgin_status = "brb"; break;
        case PN_STATUS_AWAY:
            pidgin_status = "away"; break;
        case PN_STATUS_PHONE:
            pidgin_status = "phone"; break;
        case PN_STATUS_LUNCH:
            pidgin_status = "lunch"; break;
        case PN_STATUS_HIDDEN:
            pidgin_status = "invisible"; break;
        case PN_STATUS_IDLE:
            idle = TRUE;
        case PN_STATUS_ONLINE:
            pidgin_status = "available"; break;
        default:
            pidgin_status = NULL; break;
    }

    purple_prpl_got_user_status (account, contact->passport, pidgin_status, NULL);

    if (contact->mobile && contact->status == PN_STATUS_OFFLINE)
        purple_prpl_got_user_status (account, contact->passport, "mobile", NULL);
    else
        purple_prpl_got_user_status_deactive (account, contact->passport, "mobile");

    purple_prpl_got_user_idle (account, contact->passport, idle, idle ? -1 : 0);
#endif /* HAVE_LIBPURPLE */
}

gboolean
pn_contact_is_account (PnContact *contact)
{
    if (strcmp (msn_session_get_username (contact->contactlist->session), contact->passport) == 0)
        return TRUE;

    return FALSE;
}

void
pn_contact_set_state (PnContact *contact,
                         const gchar *state)
{
    PecanStatus status;

    if (!state)
        status = PN_STATUS_OFFLINE;
    else if (strcmp (state, "NLN") == 0)
        status = PN_STATUS_ONLINE;
    else if (strcmp (state, "BSY") == 0)
        status = PN_STATUS_BUSY;
    else if (strcmp (state, "IDL") == 0)
        status = PN_STATUS_IDLE;
    else if (strcmp (state, "BRB") == 0)
        status = PN_STATUS_BRB;
    else if (strcmp (state, "AWY") == 0)
        status = PN_STATUS_AWAY;
    else if (strcmp (state, "PHN") == 0)
        status = PN_STATUS_PHONE;
    else if (strcmp (state, "LUN") == 0)
        status = PN_STATUS_LUNCH;
    else if (strcmp (state, "HDN") == 0)
        status = PN_STATUS_HIDDEN;
    else
        status = PN_STATUS_WRONG;

    contact->status = status;
}

void
pn_contact_set_passport (PnContact *contact,
                         const gchar *passport)
{
    g_free (contact->passport);
    contact->passport = pn_normalize (passport);

    if (contact->contactlist)
    {
        g_hash_table_insert (contact->contactlist->contact_names,
                             g_strdup (passport), contact);
    }
}

void
pn_contact_set_client_id (PnContact *contact,
                          gulong client_id)
{
    contact->client_id = client_id;
}

gulong
pn_contact_get_client_id (PnContact *contact)
{
    return contact->client_id;
}

void
pn_contact_set_friendly_name (PnContact *contact,
                              const gchar *name)
{
    pn_debug ("passport=[%s],name=[%s]", contact->passport, name);

    if (g_strcmp0 (contact->friendly_name, name) == 0)
        return;

#ifdef HAVE_LIBPURPLE
    PurpleAccount *account;
    account = msn_session_get_user_data (contact->contactlist->session);

    if (purple_account_get_bool (account, "hide_msgplus_tags", TRUE))
    {
        char *parsed_name;

        parsed_name = remove_plus_tags_from_str (name);

        if (g_strcmp0 (contact->friendly_name, parsed_name) == 0) {
            g_free (parsed_name);
            return;
        }

        if (!parsed_name)
            parsed_name = g_strdup (name);

        g_free (contact->friendly_name);
        contact->friendly_name = parsed_name;
    }
    else
    {
        g_free (contact->friendly_name);
        contact->friendly_name = g_strdup (name);
    }

    purple_buddy_set_public_alias (purple_account_get_connection (account),
                                   contact->passport, contact->friendly_name);

    /** @todo temporarily disable this until we have proper server-side aliases
     * support. */
#if 0
    /* If contact == account; display and friendly are the same thing. */
    /** @todo this is a libpurple specific thing */
    if (pn_contact_is_account (contact))
    {
        pn_debug ("contact is account");
        pn_contact_set_store_name (contact, name);
    }
#endif
#else
    g_free (contact->friendly_name);
    contact->friendly_name = g_strdup (name);
#endif /* HAVE_LIBPURPLE */
}

void
pn_contact_set_personal_message (PnContact *contact,
                                 const gchar *value)
{
    pn_debug ("passport=[%s],value=[%s]", contact->passport, value);

    if (contact->personal_message && value &&
        strcmp (contact->personal_message, value) == 0)
    {
        return;
    }

#ifdef HAVE_LIBPURPLE
    PurpleAccount *account;
    account = msn_session_get_user_data (contact->contactlist->session);

    if (value && purple_account_get_bool (account, "hide_msgplus_tags", TRUE))
    {
        char* parsed_value;

        parsed_value = remove_plus_tags_from_str (value);

        if (!parsed_value)
            parsed_value = g_strdup (value);
        if (contact->personal_message && parsed_value &&
            strcmp (contact->personal_message, parsed_value) == 0)
            return;

        g_free (contact->personal_message);
        contact->personal_message = parsed_value;
    }
    else
    {
        g_free (contact->personal_message);
        contact->personal_message = g_strdup (value);
    }
#else
    g_free (contact->personal_message);
    contact->personal_message = g_strdup (value);
#endif /* HAVE_LIBPURPLE */
}

void
pn_contact_set_current_media (PnContact *contact,
                              const gchar *value)
{
    gchar **array;
    char *dec;
    int count = 0;

    /*
    * 0: Application
    * 1: 'Music'/'Games'/'Office'
    * 2: Enabled
    * 3: Format
    * 4: Title
    * If 'Music':
    *  5: Artist
    *  6: Album
    *  7: ?
    */

    contact->media.type = CURRENT_MEDIA_UNKNOWN;
    g_free (contact->media.title);
    contact->media.title = NULL;
    g_free (contact->media.artist);
    contact->media.artist = NULL;
    g_free (contact->media.album);
    contact->media.album = NULL;

    if (!value)
        return;

    dec = pn_html_unescape (value);

    if (!dec) {
        pn_error ("couldn't parse [%s]", value);
        return;
    }

    array = g_strsplit (dec, "\\0", 0);

#if GLIB_CHECK_VERSION(2,6,0)
    count  = g_strv_length (array);
#else
    while (array[++count]);
#endif

    if (count >= 4 && strcmp (array[2], "1") == 0)
    {
        if (strcmp (array[1], "Music") == 0)
            contact->media.type = CURRENT_MEDIA_MUSIC;
        else if (strcmp (array[1], "Games") == 0)
            contact->media.type = CURRENT_MEDIA_GAMES;
        else if (strcmp (array[1], "Office") == 0)
            contact->media.type = CURRENT_MEDIA_OFFICE;

        if (count == 4)
            contact->media.title = g_strdup (array[3]);
        else
            contact->media.title = g_strdup (array[4]);

        if (count > 5)
            contact->media.artist = g_strdup (array[5]);

        if (count > 6)
            contact->media.album = g_strdup (array[6]);
    }

    g_strfreev (array);
    g_free (dec);
}

void
pn_contact_set_store_name (PnContact *contact,
                           const gchar *name)
{
    pn_debug ("passport=[%s],name=[%s]", contact->passport, name);

    if (contact->contactlist)
    {
        if (msn_session_get_bool (contact->contactlist->session, "use_server_alias"))
        {
            /** @todo this is a hack to disable display names. */
            if (name &&
                strcmp (contact->passport, name) == 0)
            {
                name = NULL;
            }
        }
    }

    if (contact->store_name && name &&
        strcmp (contact->store_name, name) == 0)
    {
        return;
    }

    g_free (contact->store_name);
    contact->store_name = g_strdup (name);

#ifdef HAVE_LIBPURPLE
    {
        PurpleAccount *account;
        PurpleConnection *connection;
        MsnSession *session;

        session = contact->contactlist->session;
        account = msn_session_get_user_data (session);
        connection = purple_account_get_connection (account);

        purple_buddy_set_private_alias (connection, contact->passport, contact->store_name);
    }

    /** @todo temporarily disable this until we have proper server-side aliases
     * support. */
#if 0
    /* If contact == account; display and friendly are the same thing. */
    /** @todo this is a libpurple specific thing */
    if (pn_contact_is_account (contact))
    {
        pn_debug ("contact is account");
        pn_contact_set_friendly_name (contact, name);
    }
#endif
#endif /* HAVE_LIBPURPLE */
}

void
pn_contact_set_guid (PnContact *contact,
                     const gchar *guid)
{
    g_free (contact->guid);
    contact->guid = g_strdup (guid);
    if (contact->contactlist && guid)
    {
        g_hash_table_insert (contact->contactlist->contact_guids, g_strdup (guid), contact);
    }
}

void
pn_contact_set_buddy_icon (PnContact *contact,
                           PnBuffer *image)
{
#if defined(PECAN_CVR)
#ifdef HAVE_LIBPURPLE
    PnMsnObj *obj;

    obj = pn_msnobj_new_from_image (image, "TFR2C2.tmp", pn_contact_get_passport (contact),
                                    PN_MSNOBJ_USERTILE);
    pn_contact_set_object (contact, obj);
#endif /* HAVE_LIBPURPLE */
#endif /* defined(PECAN_CVR) */
}

void
pn_contact_add_group_id (PnContact *contact,
                         const gchar *group_guid)
{
    const gchar *passport;

    passport = pn_contact_get_passport (contact);

    pn_debug ("passport=[%s],group_guid=[%s]", passport, group_guid);

    if (group_guid)
    {
        g_hash_table_insert (contact->groups, g_strdup (group_guid), "foo");
    }

#ifdef HAVE_LIBPURPLE
    {
        PecanContactList *contactlist;
        PurpleAccount *account;
        PurpleBuddy *b = NULL;
        PurpleGroup *g = NULL;
        const gchar *group_name;

        contactlist = contact->contactlist;
        group_name = pecan_contactlist_find_group_name (contactlist, group_guid);
        account = msn_session_get_user_data (contactlist->session);

        /* If this contact is in the no-group, remove him, since now he is in a
         * group. */
        if (group_guid)
        {
            const gchar *t_group_name;
            PurpleGroup *t_g;

            t_group_name = pecan_contactlist_find_group_name (contactlist, NULL);
            t_g = purple_find_group (t_group_name);

            if (t_g)
            {
                b = purple_find_buddy_in_group (account, passport, t_g);

                if (b)
                {
                    purple_blist_remove_buddy (b);
                }
            }
        }

        if (group_name)
            g = purple_find_group (group_name);

        /* If the group is not there, add it */
        if (!g)
        {
            g = purple_group_new (group_name);
            purple_blist_add_group (g, NULL);
        }

        b = purple_find_buddy_in_group (account, passport, g);

        if (!b)
        {
            b = purple_buddy_new (account, passport, NULL);
            purple_blist_add_buddy (b, NULL, g, NULL);
        }

        b->proto_data = contact;
    }
#endif /* HAVE_LIBPURPLE */
}

void
pn_contact_remove_group_id (PnContact *contact,
                            const gchar *group_guid)
{
    pn_debug ("passport=[%s],group_guid=[%s]", contact->passport, group_guid);

    g_hash_table_remove (contact->groups, group_guid);
}

guint
pn_contact_get_group_count (PnContact *contact)
{
    return g_hash_table_size (contact->groups);
}

void
pn_contact_set_home_phone (PnContact *contact,
                           const gchar *number)
{
    g_free (contact->phone.home);

    contact->phone.home = (!number ? NULL : g_strdup (number));
}

void
pn_contact_set_work_phone (PnContact *contact,
                           const gchar *number)
{
    g_free (contact->phone.work);

    contact->phone.work = (!number ? NULL : g_strdup (number));
}

void
pn_contact_set_mobile_phone (PnContact *contact,
                             const gchar *number)
{
    g_free (contact->phone.mobile);

    contact->phone.mobile = (!number ? NULL : g_strdup (number));
}

#if defined(PECAN_CVR)
void
pn_contact_set_object (PnContact *contact,
                       PnMsnObj *obj)
{
    /** @todo sometimes we need to force an update, in those cases the old and
     * new object will be the same, we must not free the object, so in order to
     * do that properly we need to implement msnobj ref/unref. */
    /* For now just assume there was no obj set. */
    if (contact->msnobj == obj)
        contact->msnobj = NULL;

    if (contact->msnobj)
        pn_msnobj_free (contact->msnobj);

    contact->msnobj = obj;

    /** @todo make this a hook. */
    pn_dp_manager_contact_set_object (contact, obj);
}
#endif /* defined(PECAN_CVR) */

void
pn_contact_set_client_caps (PnContact *contact,
                            GHashTable *info)
{
    if (contact->clientcaps)
        g_hash_table_destroy (contact->clientcaps);

    contact->clientcaps = info;
}

const gchar *
pn_contact_get_passport (const PnContact *contact)
{
    return contact->passport;
}

const gchar *
pn_contact_get_friendly_name (const PnContact *contact)
{
    return contact->friendly_name;
}

const gchar *
pn_contact_get_personal_message (const PnContact *contact)
{
    return contact->personal_message;
}

const gchar *
pn_contact_get_store_name (const PnContact *contact)
{
    return contact->store_name;
}

const gchar *
pn_contact_get_guid (const PnContact *contact)
{
    return contact->guid;
}

const gchar *
pn_contact_get_home_phone (const PnContact *contact)
{
    return contact->phone.home;
}

const gchar *
pn_contact_get_work_phone (const PnContact *contact)
{
    return contact->phone.work;
}

const gchar *
pn_contact_get_mobile_phone (const PnContact *contact)
{
    return contact->phone.mobile;
}

#if defined(PECAN_CVR)
PnMsnObj *
pn_contact_get_object (const PnContact *contact)
{
    return contact->msnobj;
}
#endif /* defined(PECAN_CVR) */

GHashTable *
pn_contact_get_client_caps (const PnContact *contact)
{
    return contact->clientcaps;
}

static inline gboolean
is_blocked (const PnContact *contact)
{
    return ((contact->list_op & (1 << MSN_LIST_BL)) ? true : false);
}

static inline gboolean
is_offline (const PnContact *contact)
{
    return (contact->status == PN_STATUS_OFFLINE ? true : false);
}

gboolean
pn_contact_can_receive (const PnContact *contact)
{
    if (is_blocked (contact))
        return false;

    if (is_offline (contact))
        return false;

    return true;
}