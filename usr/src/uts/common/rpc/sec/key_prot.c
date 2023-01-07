/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include <rpc/key_prot.h>
/* @(#)key_prot.x	1.10 90/01/03 Copyright (c)  1990, 1991 SMI */

/*
 * Compiled from key_prot.x using rpcgen.
 * DO NOT EDIT THIS FILE!
 * This is NOT source code!
 */

bool_t
xdr_keystatus(XDR *xdrs, keystatus *objp)
{
	if (!xdr_enum(xdrs, (enum_t *)objp))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_keybuf(XDR *xdrs, keybuf objp)
{
	if (!xdr_opaque(xdrs, objp, HEXKEYBYTES))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_netnamestr(XDR *xdrs, netnamestr *objp)
{
	if (!xdr_string(xdrs, objp, MAXNETNAMELEN))
		return (FALSE);

	return (TRUE);
}

bool_t
xdr_cryptkeyarg(XDR *xdrs, cryptkeyarg *objp)
{
	if (!xdr_netnamestr(xdrs, &objp->remotename))
		return (FALSE);
	if (!xdr_des_block(xdrs, &objp->deskey))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_cryptkeyarg2(XDR *xdrs, cryptkeyarg2 *objp)
{
	if (!xdr_netnamestr(xdrs, &objp->remotename))
		return (FALSE);
	if (!xdr_netobj(xdrs, &objp->remotekey))
		return (FALSE);
	if (!xdr_des_block(xdrs, &objp->deskey))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_cryptkeyres(XDR *xdrs, cryptkeyres *objp)
{
	if (!xdr_keystatus(xdrs, &objp->status))
		return (FALSE);
	switch (objp->status) {
	case KEY_SUCCESS:
		if (!xdr_des_block(xdrs, &objp->cryptkeyres_u.deskey))
			return (FALSE);
		break;
	default:
		break;
	}
	return (TRUE);
}

bool_t
xdr_unixcred(XDR *xdrs, unixcred *objp)
{
	if (!xdr_u_int(xdrs, &objp->uid))
		return (FALSE);
	if (!xdr_u_int(xdrs, &objp->gid))
		return (FALSE);
	if (!xdr_array(xdrs, (char **)&objp->gids.gids_val,
	    (uint_t *)&objp->gids.gids_len, MAXGIDS,
	    sizeof (uint_t), (xdrproc_t)xdr_u_int))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_getcredres(XDR *xdrs, getcredres *objp)
{
	if (!xdr_keystatus(xdrs, &objp->status))
		return (FALSE);
	switch (objp->status) {
	case KEY_SUCCESS:
		if (!xdr_unixcred(xdrs, &objp->getcredres_u.cred))
			return (FALSE);
		break;
	default:
		break;
	}
	return (TRUE);
}

bool_t
xdr_key_netstarg(XDR *xdrs, key_netstarg *objp)
{
	if (!xdr_keybuf(xdrs, objp->st_priv_key))
		return (FALSE);
	if (!xdr_keybuf(xdrs, objp->st_pub_key))
		return (FALSE);


	if (!xdr_netnamestr(xdrs, &objp->st_netname))
		return (FALSE);

	return (TRUE);
}

bool_t
xdr_key_netstres(XDR *xdrs, key_netstres *objp)
{
	if (!xdr_keystatus(xdrs, &objp->status))
		return (FALSE);
	switch (objp->status) {
	case KEY_SUCCESS:
		if (!xdr_key_netstarg(xdrs, &objp->key_netstres_u.knet))
			return (FALSE);
		break;
	default:
		break;
	}
	return (TRUE);
}
