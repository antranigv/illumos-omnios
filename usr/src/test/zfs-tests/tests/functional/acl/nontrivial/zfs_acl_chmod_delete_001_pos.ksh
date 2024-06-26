#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright 2016 Nexenta Systems, Inc.
# Copyright 2023 RackTop Systems, Inc.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

# DESCRIPTION:
# Verify that the combined delete_child/delete permission for
# owner/group/everyone are correct.
#
#        -------------------------------------------------------
#        |   Parent Dir  |           Target Object Permissions |
#        |  permissions  |                                     |
#        -------------------------------------------------------
#        |               | ACL Allows | ACL Denies| Delete     |
#        |               |  Delete    |  Delete   | unspecified|
#        -------------------------------------------------------
#        | ACL Denies    | Permit     | Deny      | Deny       |
#        | DELETE_CHILD  |            |           |            |
#        | or WRITE_DATA |            |           |            |
#        -------------------------------------------------------
#        | ACL Allows    | Permit     | Deny      | Permit     |
#        | DELETE_CHILD  |            |           |            |
#        | or WRITE_DATA |            |           |            |
#        -------------------------------------------------------
#
# STRATEGY:
# 1. Create file and  directory in zfs filesystem
# 2. Set special ACE combination to the file and directory
# 3. Try to remove the file
# 4. Verify that combined permissions for owner/group/everyone are correct.

verify_runnable "both"

function cleanup
{
	if [[ ! -e $target ]]; then
		log_must tar xpf $TESTDIR/$ARCHIVEFILE
	fi

	(( ${#cwd} != 0 )) && cd $cwd
	cleanup_test_files $TESTDIR/basedir
	if [[ -e $TESTDIR/$ARCHIVEFILE ]]; then
		log_must rm -f $TESTDIR/$ARCHIVEFILE
	fi
	return 0
}

#owner@	          group	                 group_users       other_users
set -A users \
"root"            "root"                 "$ZFS_ACL_ADMIN"  "$ZFS_ACL_OTHER1" \
"$ZFS_ACL_STAFF1" "$ZFS_ACL_STAFF_GROUP" "$ZFS_ACL_STAFF2" "$ZFS_ACL_OTHER1"

set -A access_parent \
	"delete_child:allow" \
	"delete_child:deny" \
	"write_data:allow" \
	"write_data:deny" \
	"delete_child:deny write_data:allow" \
	"delete_child:allow write_data:deny"

set -A access_target \
	"delete:allow" \
	"delete:deny" \
	""

set -A a_flag "owner@" "group@" "everyone@" "user:$ZFS_ACL_STAFF1"

log_assert "Verify that the combined delete_child/delete permission for" \
    "owner/group/everyone are correct."
log_onexit cleanup

function operate_node #user node
{
	typeset user=$1
	typeset node=$2
	typeset ret

	if [[ $user == "" || $node == "" ]]; then
		log_fail "user, node are not defined."
	fi
	if [[ -d $node ]]; then
		chgusr_exec $user rm -rf $node ; ret=$?
	else
		chgusr_exec $user rm -f $node ; ret=$?
	fi

	if [[ -e $node ]]; then
		if [[ $ret -eq 0 ]]; then
			log_note "$node not removed, but return code is 0."
			return 1
		fi
	else
		log_must tar xpf $TESTDIR/$ARCHIVEFILE
		if [[ $ret -ne 0 ]]; then
			log_note "$node removed, but return code is $ret."
			return 1
		fi
	fi
	return $ret
}

function logname #acl_parent acl_target user
{
	typeset acl_parent=$1
	typeset acl_target=$2
	typeset user=$3

	# To super user, read and write deny permission was override.
	if [[ $user == "root" || $acl_target == *":allow"* ]]; then
		print "log_must"
	# If target ACL has an ACE deny'ing delete, DENY
	elif [[ $acl_target == *"delete:deny"* ]]; then
		print "log_mustnot"
	# If target ACL has an ACE allow'ing delete, ALLOW
	elif [[ $acl_target == *"delete:allow"* ]]; then
		print "log_must"
	# If container ACL has an ACE deny'ing delete_child or
	# write_data, DENY
	elif [[ $acl_parent == *"delete_child:deny"* ||
	    $acl_parent == *"write_data:deny"* ]]; then
		print "log_mustnot"
	# If container ACL has an ACE allow'ing delete_child or
	# write_data, ALLOW
	elif [[ $acl_parent == *"delete_child:allow"* ||
	    $acl_parent == *"write_data:allow"* ]]; then
		print "log_must"
	# Otherwise, DENY
	else
		print "log_mustnot"
	fi
}

function check_chmod_results #node flag acl_parent acl_target g_usr o_usr
{
	typeset node=$1
	typeset flag=$2
	typeset acl_parent=$3
	typeset acl_target=$2:$4
	typeset g_usr=$5
	typeset o_usr=$6
	typeset log acl_tmp

	for acl in $acl_parent ; do
		acl_tmp="$2:$acl $acl_tmp"
	done
	acl_parent=$acl_tmp

	if [[ $flag == "owner@" || $flag == "everyone@" ]]; then
		log=$(logname "$acl_parent" $acl_target $ZFS_ACL_CUR_USER)
		$log operate_node $ZFS_ACL_CUR_USER $node
	fi
	if [[ $flag == "group@" || $flag == "everyone@" ]]; then
		log=$(logname "$acl_parent" $acl_target $g_usr)
		$log operate_node $g_usr $node
	fi
	if [[ $flag == "everyone@" ]]; then
		log=$(logname "$acl_parent" $acl_target $o_usr)
		$log operate_node $o_usr $node
	fi
	if [[ $flag == "user:"* ]]; then
		typeset user=${flag#user:}
		log=$(logname "$acl_parent" $acl_target $user)
		$log operate_node $user $node
	fi
}

function test_chmod_basic_access #node g_usr o_usr
{
	typeset node=${1%/}
	typeset g_usr=$2
	typeset o_usr=$3
	typeset flag acl_p acl_t parent

	parent=${node%/*}

	for flag in ${a_flag[@]}; do
	for acl_p in "${access_parent[@]}"; do
		for acl in $acl_p ; do
			log_must usr_exec chmod A+$flag:$acl $parent
		done

		for acl_t in "${access_target[@]}"; do
			[[ -n $acl_t ]] && \
				log_must usr_exec chmod A+$flag:$acl_t $node

			log_must tar cpf $TESTDIR/$ARCHIVEFILE basedir

			check_chmod_results "$node" "$flag" \
				 "$acl_p" "$acl_t" "$g_usr" "$o_usr"

			[[ -n $acl_t ]] && \
				log_must usr_exec chmod A0- $node
		done

		for acl in $acl_p ; do
			log_pos usr_exec chmod A-$flag:$acl $parent
		done
	done
	done
}

function setup_test_files #base_node user group
{
	typeset base_node=$1
	typeset user=$2
	typeset group=$3

	cleanup_test_files $base_node

	log_must mkdir -p $base_node
	log_must chown $user:$group $base_node

	log_must set_cur_usr $user

	# Prepare all files/sub-dirs for testing.
	file0=$base_node/testfile_rm
	dir0=$base_node/testdir_rm

	log_must usr_exec touch $file0
	log_must usr_exec chmod 444 $file0

	log_must usr_exec mkdir -p $dir0
	log_must usr_exec chmod 444 $dir0

	log_must usr_exec chmod 555 $base_node
	return 0
}

function cleanup_test_files #base_node
{
	typeset base_node=$1

	if [[ -d $base_node ]]; then
		log_must rm -rf $base_node
	elif [[ -e $base_node ]]; then
		log_must rm -f $base_node
	fi

	return 0
}

typeset cwd=$PWD
typeset ARCHIVEFILE=archive.tar

typeset -i i=0
typeset -i j=0
typeset target
cd $TESTDIR
while (( i < ${#users[@]} )); do
	setup_test_files $TESTDIR/basedir ${users[i]} ${users[((i+1))]}

	j=0
	while (( j < 1 )); do
		eval target=\$file$j
		test_chmod_basic_access $target \
			"${users[((i+2))]}" "${users[((i+3))]}"

		eval target=\$dir$j
		test_chmod_basic_access $target \
			"${users[((i+2))]}" "${users[((i+3))]}"

		(( j = j + 1 ))
	done

	(( i += 4 ))
done

log_pass "Verify that the combined delete_child/delete permission for" \
    "owner/group/everyone are correct."
