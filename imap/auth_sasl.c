/*
 * Copyright (C) 2000 Brendan Cully <brendan@kublai.com>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

/* SASL login/authentication code */

#include "mutt.h"
#include "mutt_sasl.h"
#include "imap_private.h"
#include "auth.h"

#include <sasl.h>
#include <saslutil.h>

/* imap_auth_sasl: Default authenticator if available. */
imap_auth_res_t imap_auth_sasl (IMAP_DATA* idata)
{
  sasl_conn_t* saslconn;
  int rc;
  char buf[LONG_STRING];
  const char* mech;
  char* pc;
  unsigned int len;

  if (mutt_sasl_start () != SASL_OK)
    return IMAP_AUTH_FAILURE;

  /* TODO: set fourth option to SASL_SECURITY_LAYER once we have a wrapper
   *  (ie more than auth code) for SASL. */
  rc = sasl_client_new ("imap", idata->conn->account.host,
    mutt_sasl_get_callbacks (&idata->conn->account), 0, &saslconn);

  if (rc != SASL_OK)
  {
    dprint (1, (debugfile, "imap_auth_sasl: Error allocating SASL connection.\n"));
    return IMAP_AUTH_FAILURE;
  }

  rc = sasl_client_start (saslconn, idata->capstr, NULL, NULL, &pc, &len,
    &mech);

  if (rc != SASL_OK && rc != SASL_CONTINUE)
  {
    dprint (1, (debugfile, "imap_auth_sasl: Failure starting authentication exchange. No shared mechanisms?\n"));
    /* SASL doesn't support LOGIN, so fall back */

    return IMAP_AUTH_UNAVAIL;
  }

  mutt_message _("Authenticating (SASL)...");

  snprintf (buf, sizeof (buf), "AUTHENTICATE %s", mech);
  imap_cmd_start (idata, buf);

  /* I believe IMAP requires a server response (even if empty) before the
   * client can send data, so len should always be zero at this point */
  if (len != 0)
  {
    dprint (1, (debugfile, "imap_auth_sasl: SASL produced invalid exchange!\n"));
    goto bail;
  }

  while (rc == SASL_CONTINUE)
  {
    if (mutt_socket_readln (buf, sizeof (buf), idata->conn) < 0)
      goto bail;

    if (!mutt_strncmp (buf, "+ ", 2))
      if (sasl_decode64 (buf+2, strlen (buf+2), buf, &len) != SASL_OK)
      {
	dprint (1, (debugfile, "imap_auth_sasl: error base64-decoding server response.\n"));
	goto bail;
      }

    rc = sasl_client_step (saslconn, buf, len, NULL, &pc, &len);

    if (rc != SASL_CONTINUE)
      break;

    /* send out response, or line break if none needed */
    if (sasl_encode64 (pc, len, buf, sizeof (buf), &len) != SASL_OK)
    {
      dprint (1, (debugfile, "imap_auth_sasl: error base64-encoding client response.\n"));
      goto bail;
    }
      
    strfcpy (buf + len, "\r\n", sizeof (buf) - len);
    mutt_socket_write (idata->conn, buf);
  }

  if (rc != SASL_OK)
    goto bail;

  if (imap_code (buf))
  {
    /* later we'll want to keep saslconn, when we support a protection layer.
     * For now it shouldn't hurt to dispose of it at this point. */
    sasl_dispose (&saslconn);
    return IMAP_AUTH_SUCCESS;
  }

 bail:
  mutt_error _("SASL authentication failed.");
  sleep(2);
  sasl_dispose (&saslconn);

  return IMAP_AUTH_FAILURE;
}
