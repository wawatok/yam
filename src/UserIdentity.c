/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 Marcel Beck
 Copyright (C) 2000-2012 YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site :  http://www.yam.ch
 YAM OpenSource project    :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if !defined(__amigaos4__)
#include <clib/alib_protos.h>
#endif

#include <proto/exec.h>

#include "YAM_config.h"
#include "YAM_utilities.h"

#include "extrasrc.h"

#include "UserIdentity.h"

#include "Debug.h"

/***************************************************************************
 Module: UserIdentity related routines
***************************************************************************/

/// CreateNewUserIdentity
//  Initializes a new UserIdentityNode
struct UserIdentityNode *CreateNewUserIdentity(const struct Config *co)
{
  struct UserIdentityNode *uin;

  ENTER();

  if((uin = AllocSysObjectTags(ASOT_NODE, ASONODE_Size, sizeof(*uin),
                                          ASONODE_Min, TRUE,
                                          TAG_DONE)) != NULL)
  {
    // initialize all variables as AllocSysObject() does not clear the memory
    memset(uin, 0, sizeof(*uin));
    uin->active = TRUE;

    // now we fill the UserIdentity structure with some sensible
    // defaults

  }

  RETURN(uin);
  return uin;
}

///
/// FreeUserIdentityList
void FreeUserIdentityList(struct MinList *userIdentityList)
{
  struct Node *curNode;

  ENTER();


  // we have to free the userIdentityList
  while((curNode = RemHead((struct List *)userIdentityList)) != NULL)
  {
    struct UserIdentityNode *uin = (struct UserIdentityNode *)curNode;

    FreeSysObject(ASOT_NODE, uin);
  }

  NewMinList(userIdentityList);

  LEAVE();
}

///
/// CompareUserIdentityNodes
static BOOL CompareUserIdentityNodes(const struct Node *n1, const struct Node *n2)
{
  BOOL equal = TRUE;
  const struct UserIdentityNode *uid1 = (const struct UserIdentityNode *)n1;
  const struct UserIdentityNode *uid2 = (const struct UserIdentityNode *)n2;

  ENTER();

  // compare every single member of the structure
  if(uid1->active != uid2->active ||
     strcmp(uid1->description, uid2->description) != 0 ||
     strcmp(uid1->realname, uid2->realname) != 0 ||
     strcmp(uid1->address, uid2->address) != 0 ||
     strcmp(uid1->organization, uid2->organization) != 0 ||
     uid1->mailServer != uid2->mailServer ||
     uid1->signature != uid2->signature ||
     strcmp(uid1->mailCC, uid2->mailCC) != 0 ||
     strcmp(uid1->mailBCC, uid2->mailBCC) != 0 ||
     strcmp(uid1->mailReplyTo, uid2->mailReplyTo) != 0 ||
     strcmp(uid1->extraHeaders, uid2->extraHeaders) != 0 ||
     strcmp(uid1->photoURL, uid2->photoURL) != 0 ||
     uid1->sentFolder != uid2->sentFolder ||
     uid1->quoteMails != uid2->quoteMails ||
     uid1->quotePosition != uid2->quotePosition ||
     uid1->signaturePosition != uid2->signaturePosition ||
     uid1->sigReply != uid2->sigReply ||
     uid1->sigForwarding != uid2->sigForwarding ||
     uid1->addPersonalInfo != uid2->addPersonalInfo ||
     uid1->requestMDN != uid2->requestMDN ||
     uid1->usePGP != uid2->usePGP ||
     strcmp(uid1->pgpKeyID, uid2->pgpKeyID) != 0 ||
     strcmp(uid1->pgpKeyURL, uid2->pgpKeyURL) != 0 ||
     uid1->pgpSignUnencrypted != uid2->pgpSignUnencrypted ||
     uid1->pgpSignEncrypted != uid2->pgpSignEncrypted ||
     uid1->pgpEncryptAll != uid2->pgpEncryptAll ||
     uid1->pgpSelfEncrypt != uid2->pgpSelfEncrypt)
  {
    // something does not match
    equal = FALSE;
  }

  RETURN(equal);
  return equal;
}

///
/// CompareUserIdentityLists
// compare two UserIdentity lists
BOOL CompareUserIdentityLists(const struct MinList *msl1, const struct MinList *msl2)
{
  BOOL equal;

  ENTER();

  equal = CompareLists((const struct List *)msl1, (const struct List *)msl2, CompareUserIdentityNodes);

  RETURN(equal);
  return equal;
}

///
/// GetUserIdentity
// function to extract the structure of a User Identity from our user identity list
struct UserIdentityNode *GetUserIdentity(struct MinList *userIdentityList, const unsigned int num)
{
  struct UserIdentityNode *result = NULL;
  unsigned int count = 0;
  struct Node *curNode;

  ENTER();

  IterateList(userIdentityList, curNode)
  {
    struct UserIdentityNode *uin = (struct UserIdentityNode *)curNode;

    if(count == num)
    {
      result = uin;
      break;
    }

    count++;
  }

  RETURN(result);
  return result;
}

///