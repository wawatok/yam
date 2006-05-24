/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2006 by YAM Open Source Team

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

#include <clib/alib_protos.h>
#include <libraries/asl.h>
#include <mui/BetterString_mcc.h>
#include <mui/TextEditor_mcc.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/xpkmaster.h>
#include <proto/codesets.h>

#include "extra.h"
#include "SDI_hook.h"
#include "classes/Classes.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_config.h"
#include "YAM_find.h"
#include "YAM_global.h"
#include "YAM_locale.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_mime.h"
#include "YAM_utilities.h"

#include "UpdateCheck.h"

#include "Debug.h"

enum VarPopMode { VPM_FORWARD=0, VPM_REPLYHELLO, VPM_REPLYINTRO, VPM_REPLYBYE,
                  VPM_QUOTE, VPM_ARCHIVE, VPM_MAILSTATS };

/* local protos */
static Object *MakeVarPop(Object **, enum VarPopMode, int, char *);
static Object *MakePhraseGroup(Object **, Object **, Object **, char*, char*);
static Object *MakeStaticCheck(void);

/***************************************************************************
 Module: Configuration - GUI for sections
***************************************************************************/

/*** Hooks ***/
/// PO_List2TextFunc
//  Copies listview selection to text gadget
HOOKPROTONH(PO_List2TextFunc, void, Object *list, Object *text)
{
   char *selection;
   DoMethod(list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &selection);
   if (selection) if (strcmp(selection, GetStr(MSG_MA_Cancel))) set(text, MUIA_Text_Contents, selection);
}
MakeStaticHook(PO_List2TextHook, PO_List2TextFunc);

///
/// CO_LV_RxDspFunc
//  ARexx listview display hook
HOOKPROTONH(CO_LV_RxDspFunc, long, char **array, int num)
{
   static char rexxoptm[SIZE_DEFAULT];
   int scr = num-1;

   rexxoptm[0] = '\0';
   array[0] = rexxoptm;

   if(*CE->RX[scr].Script)
     strlcat(rexxoptm, MUIX_PH, sizeof(rexxoptm));

   switch(scr)
   {
      case MACRO_STARTUP:   strlcat(rexxoptm, GetStr(MSG_CO_ScriptStartup), sizeof(rexxoptm)); break;
      case MACRO_QUIT:      strlcat(rexxoptm, GetStr(MSG_CO_ScriptTerminate), sizeof(rexxoptm)); break;
      case MACRO_PREGET:    strlcat(rexxoptm, GetStr(MSG_CO_ScriptPreGetMail), sizeof(rexxoptm)); break;
      case MACRO_POSTGET:   strlcat(rexxoptm, GetStr(MSG_CO_ScriptPostGetMail), sizeof(rexxoptm)); break;
      case MACRO_NEWMSG:    strlcat(rexxoptm, GetStr(MSG_CO_ScriptNewMsg), sizeof(rexxoptm)); break;
      case MACRO_PRESEND:   strlcat(rexxoptm, GetStr(MSG_CO_ScriptPreSendMail), sizeof(rexxoptm)); break;
      case MACRO_POSTSEND:  strlcat(rexxoptm, GetStr(MSG_CO_ScriptPostSendMail), sizeof(rexxoptm)); break;
      case MACRO_READ:      strlcat(rexxoptm, GetStr(MSG_CO_ScriptReadMsg), sizeof(rexxoptm)); break;
      case MACRO_PREWRITE:  strlcat(rexxoptm, GetStr(MSG_CO_ScriptPreWriteMsg), sizeof(rexxoptm)); break;
      case MACRO_POSTWRITE: strlcat(rexxoptm, GetStr(MSG_CO_ScriptPostWriteMsg), sizeof(rexxoptm)); break;
      case MACRO_URL:       strlcat(rexxoptm, GetStr(MSG_CO_ScriptClickURL), sizeof(rexxoptm)); break;

      default:
      {
        int p = strlen(rexxoptm);
        snprintf(&rexxoptm[p], sizeof(rexxoptm)-p, GetStr(MSG_CO_ScriptMenu), num);
      }
   }

   return 0;
}
MakeStaticHook(CO_LV_RxDspHook,CO_LV_RxDspFunc);

///
/// PO_XPKOpenHook
//  Sets a popup listview accordingly to its string gadget
HOOKPROTONH(PO_XPKOpenFunc, BOOL, Object *list, Object *str)
{
  char *s;

  ENTER();

  if((s = (char *)xget(str, MUIA_Text_Contents)))
  {
    int i;

    for(i=0;;i++)
    {
      char *x;

      DoMethod(list, MUIM_List_GetEntry, i, &x);
      if(!x)
      {
        set(list, MUIA_List_Active, MUIV_List_Active_Off);
        break;
      }
      else if(!stricmp(x, s))
      {
        set(list, MUIA_List_Active, i);
        break;
      }
    }
  }

  RETURN(TRUE);
  return TRUE;
}
MakeStaticHook(PO_XPKOpenHook, PO_XPKOpenFunc);

///
/// PO_XPKCloseHook
//  Copies XPK sublibrary id from list to string gadget
HOOKPROTONH(PO_XPKCloseFunc, void, Object *pop, Object *text)
{
  char *entry = NULL;

  ENTER();

  DoMethod(pop, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &entry);
  if(entry)
    set(text, MUIA_Text_Contents, entry);

  LEAVE();
}
MakeStaticHook(PO_XPKCloseHook, PO_XPKCloseFunc);

///
/// MakeXPKPop
//  Creates a popup list of available XPK sublibraries
static Object *MakeXPKPop(Object **text, BOOL pack, BOOL encrypt)
{
  Object *lv, *po;

  if((po = PopobjectObject,
    MUIA_Popstring_String, *text = TextObject,
      TextFrame,
      MUIA_Background, MUII_TextBack,
      MUIA_FixWidthTxt, "MMMM",
    End,
    MUIA_Popstring_Button, PopButton(MUII_PopUp),
    MUIA_Popobject_StrObjHook, &PO_XPKOpenHook,
    MUIA_Popobject_ObjStrHook, &PO_XPKCloseHook,
    MUIA_Popobject_WindowHook, &PO_WindowHook,
    MUIA_Popobject_Object, lv = ListviewObject,
      MUIA_Listview_List, ListObject,
        InputListFrame,
        MUIA_List_AutoVisible,   TRUE,
        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook,  MUIV_List_DestructHook_String,
      End,
    End,
  End))
  {
    struct MinNode *curNode;

    for(curNode = G->xpkPackerList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct xpkPackerNode *xpkNode = (struct xpkPackerNode *)curNode;
      BOOL suits = TRUE;

      if(encrypt && isFlagClear(xpkNode->info.xpi_Flags, XPKIF_ENCRYPTION))
        suits = FALSE;
      else if(pack && isFlagClear(xpkNode->info.xpi_Flags, 0x3f))
        suits = FALSE;

      if(suits)
        DoMethod(lv, MUIM_List_InsertSingle, xpkNode->info.xpi_Name, MUIV_List_Insert_Sorted);
    }

    DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, po, 2, MUIM_Popstring_Close, TRUE);
  }

  return po;
}

///
/// PO_CharsetOpenHook
//  Sets the popup listview accordingly to the string gadget
HOOKPROTONH(PO_CharsetOpenFunc, BOOL, Object *list, Object *str)
{
  char *s;

  ENTER();

  if((s = (char *)xget(str, MUIA_String_Contents)))
  {
    int i;

    for(i=0;;i++)
    {
      char *x;

      DoMethod(list, MUIM_List_GetEntry, i, &x);
      if(!x)
      {
        set(list, MUIA_List_Active, MUIV_List_Active_Off);
        break;
      }
      else if(!stricmp(x, s))
      {
        set(list, MUIA_List_Active, i);
        break;
      }
    }
  }

  RETURN(TRUE);
  return TRUE;
}
MakeStaticHook(PO_CharsetOpenHook, PO_CharsetOpenFunc);

///
/// PO_CharsetCloseHook
//  Pastes an entry from the popup listview into string gadget
HOOKPROTONH(PO_CharsetCloseFunc, void, Object *list, Object *str)
{
  char *var = NULL;

  ENTER();

  DoMethod(list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &var);
  if(var)
    set(str, MUIA_String_Contents, var);

  LEAVE();
}
MakeStaticHook(PO_CharsetCloseHook, PO_CharsetCloseFunc);

///
/// PO_CharsetListDisplayHook
//  Pastes an entry from the popup listview into string gadget
HOOKPROTONH(PO_CharsetListDisplayFunc, LONG, char **array, STRPTR str)
{
  if(str)
  {
    struct codeset *cs;

    // the standard name is always in column 0
    array[0] = str;

    // try to find the codeset via codesets.library and
    // display some more information about it.
    if((cs = CodesetsFind(str,
                          CSA_CodesetList,       G->codesetsList,
                          CSA_FallbackToDefault, FALSE,
                          TAG_DONE)))
    {
      if(cs->characterization && stricmp(cs->characterization, str) != 0)
        array[1] = cs->characterization;
      else
        array[1] = "";
    }
    else
      array[1] = "";
  }
  else
  {
    array[0] = "";
    array[1] = "";
  }

  return 0;
}
MakeStaticHook(PO_CharsetListDisplayHook, PO_CharsetListDisplayFunc);

///
/// MakeCharsetPop
//  Creates a popup list of available charsets supported by codesets.library
static Object *MakeCharsetPop(Object **string)
{
  Object *lv;
  Object *po;
  Object *bt;

  ENTER();

  if((po = PopobjectObject,

    MUIA_Popstring_String, *string = BetterStringObject,
      StringFrame,
      MUIA_BetterString_NoInput,  TRUE,
      MUIA_String_AdvanceOnCR,    TRUE,
      MUIA_CycleChain,            TRUE,
    End,

    MUIA_Popstring_Button, bt = PopButton(MUII_PopUp),
    MUIA_Popobject_StrObjHook, &PO_CharsetOpenHook,
    MUIA_Popobject_ObjStrHook, &PO_CharsetCloseHook,
    MUIA_Popobject_WindowHook, &PO_WindowHook,
    MUIA_Popobject_Object, lv = ListviewObject,
       MUIA_Listview_ScrollerPos, MUIV_Listview_ScrollerPos_Right,
       MUIA_Listview_List, ListObject,
          InputListFrame,
          MUIA_List_Format,        "BAR,",
          MUIA_List_AutoVisible,   TRUE,
          MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook,  MUIV_List_DestructHook_String,
          MUIA_List_DisplayHook,   &PO_CharsetListDisplayHook,
       End,
    End,

  End))
  {
    struct codeset *codeset;
    STRPTR *array;

    set(bt, MUIA_CycleChain,TRUE);
    DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, po, 2, MUIM_Popstring_Close, TRUE);

    // Build list of available codesets
    if((array = CodesetsSupported(CSA_CodesetList, G->codesetsList,
                                  TAG_DONE)))
    {
      DoMethod(lv, MUIM_List_Insert, array, -1, MUIV_List_Insert_Sorted);
      CodesetsFreeA(array, NULL);
    }
    else
      set(po, MUIA_Disabled, TRUE);

    // Use the system's default codeset
    if((codeset = CodesetsFindA(NULL, NULL)))
      set(*string, MUIA_String_Contents, codeset->name);
  }
  else
    *string = NULL;

  RETURN(po);
  return po;
}

///
/// PO_MimeTypeListOpenHook
//  Sets the popup listview accordingly to the string gadget
HOOKPROTONH(PO_MimeTypeListOpenFunc, BOOL, Object *list, Object *str)
{
  char *s;

  ENTER();

  if((s = (char *)xget(str, MUIA_String_Contents)))
  {
    struct MinNode *curNode;
    int i;

    // we build the list totally from ground up.
    DoMethod(list, MUIM_List_Clear);

    // populate the list with the user's own defined MIME types but only if the source
    // string isn't the one in the YAM config window.
    if(G->CO == NULL || str != G->CO->GUI.ST_CTYPE)
    {
      for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
      {
        struct MimeTypeNode *mt = (struct MimeTypeNode *)curNode;
        DoMethod(list, MUIM_List_InsertSingle, mt->ContentType, MUIV_List_Insert_Sorted);
      }
    }

    // populate the MUI list with our internal MIME types but check that
    // we don't add duplicate names
    for(i=0; IntMimeTypeArray[i].ContentType != NULL; i++)
    {
      BOOL duplicateFound = FALSE;

      if(G->CO == NULL || str != G->CO->GUI.ST_CTYPE)
      {
        for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
        {
          struct MimeTypeNode *mt = (struct MimeTypeNode *)curNode;

          if(stricmp(mt->ContentType, IntMimeTypeArray[i].ContentType) == 0)
          {
            duplicateFound = TRUE;
            break;
          }
        }
      }

      if(duplicateFound == FALSE)
        DoMethod(list, MUIM_List_InsertSingle, IntMimeTypeArray[i].ContentType, MUIV_List_Insert_Sorted);
    }

    // make sure to make the current entry active
    for(i=0;;i++)
    {
      char *c;

      DoMethod(list, MUIM_List_GetEntry, i, &c);
      if(!c || s[0] == '\0')
      {
        set(list, MUIA_List_Active, MUIV_List_Active_Off);
        break;
      }
      else if(!stricmp(c, s))
      {
        set(list, MUIA_List_Active, i);
        break;
      }
    }
  }

  RETURN(TRUE);
  return TRUE;
}
MakeStaticHook(PO_MimeTypeListOpenHook, PO_MimeTypeListOpenFunc);

///
/// PO_MimeTypeListCloseHook
//  Pastes an entry from the popup listview into string gadget
HOOKPROTONH(PO_MimeTypeListCloseFunc, void, Object *list, Object *str)
{
  char *entry = NULL;

  ENTER();

  DoMethod(list, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &entry);
  if(entry)
  {
    set(str, MUIA_String_Contents, entry);

    // in case that this close function is used with the
    // string gadget in the YAM config window we have to do a deeper search
    // as we also want to set the file extension and description gadgets
    if(G->CO != NULL && str == G->CO->GUI.ST_CTYPE)
    {
      struct CO_GUIData *gui = &G->CO->GUI;
      int i;

      for(i=0; IntMimeTypeArray[i].ContentType != NULL; i++)
      {
        struct IntMimeType *mt = (struct IntMimeType *)&IntMimeTypeArray[i];

        if(stricmp(mt->ContentType, entry) == 0)
        {
          // we also set the file extension
          if(mt->Extension)
            set(gui->ST_EXTENS, MUIA_String_Contents, mt->Extension);

          // we also set the mime description
          set(gui->ST_DESCRIPTION, MUIA_String_Contents, GetStr(mt->Description));

          break;
        }
      }
    }
  }

  LEAVE();
}
MakeStaticHook(PO_MimeTypeListCloseHook, PO_MimeTypeListCloseFunc);

///
/// MakeMimeTypePop
//  Creates a popup list of available internal MIME types
Object *MakeMimeTypePop(Object **string, char *desc)
{
  Object *lv;
  Object *po;
  Object *bt;

  ENTER();

  if((po = PopobjectObject,

    MUIA_Popstring_String, *string = BetterStringObject,
      StringFrame,
      MUIA_String_Accept,      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-/",
      MUIA_String_MaxLen,      SIZE_CTYPE,
      MUIA_ControlChar,        ShortCut(GetStr(desc)),
      MUIA_String_AdvanceOnCR, TRUE,
      MUIA_CycleChain,         TRUE,
    End,
    MUIA_Popstring_Button, bt = PopButton(MUII_PopUp),
    MUIA_Popobject_StrObjHook, &PO_MimeTypeListOpenHook,
    MUIA_Popobject_ObjStrHook, &PO_MimeTypeListCloseHook,
    MUIA_Popobject_WindowHook, &PO_WindowHook,
    MUIA_Popobject_Object, lv = ListviewObject,
       MUIA_Listview_ScrollerPos, MUIV_Listview_ScrollerPos_Right,
       MUIA_Listview_List, ListObject,
          InputListFrame,
          MUIA_List_AutoVisible, TRUE,
          MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
          MUIA_List_DestructHook,  MUIV_List_DestructHook_String,
       End,
    End,

  End))
  {
    set(bt, MUIA_CycleChain,TRUE);
    DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, po, 2, MUIM_Popstring_Close, TRUE);
    DoMethod(*string, MUIM_Notify, MUIA_Disabled, MUIV_EveryTime, po, 3, MUIM_Set, MUIA_Disabled, MUIV_TriggerValue);
  }
  else
    *string = NULL;

  RETURN(po);
  return po;
}

///
/// PO_HandleVar
//  Pastes an entry from variable listview into string gadget
HOOKPROTONH(PO_HandleVar, void, Object *pop, Object *string)
{
   char *var, buf[3];

   DoMethod(pop, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &var);
   if (var)
   {
      buf[0] = var[0]; buf[1] = var[1]; buf[2] = 0;
      DoMethod(string, MUIM_BetterString_Insert, buf, MUIV_BetterString_Insert_BufferPos);
   }
}
MakeStaticHook(PO_HandleVarHook, PO_HandleVar);

///
/// MakeVarPop
//  Creates a popup list containing variables and descriptions for phrases etc.
static Object *MakeVarPop(Object **string, enum VarPopMode mode, int size, char *shortcut)
{
   Object *lv, *po;

   if ((po = PopobjectObject,
      MUIA_Popstring_String, *string = MakeString(size, shortcut),
      MUIA_Popstring_Button, PopButton(MUII_PopUp),
      MUIA_Popobject_ObjStrHook, &PO_HandleVarHook,
      MUIA_Popobject_WindowHook, &PO_WindowHook,
      MUIA_Popobject_Object, lv = ListviewObject,
         MUIA_Listview_ScrollerPos, MUIV_Listview_ScrollerPos_None,
         MUIA_Listview_List, ListObject,
            InputListFrame,
            MUIA_List_AdjustHeight, TRUE,
         End,
      End,
   End))
   {
      switch (mode)
      {
         case VPM_FORWARD:
         case VPM_REPLYHELLO:
         case VPM_REPLYINTRO:
         case VPM_REPLYBYE:
         {
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_LineBreak), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(mode?MSG_CO_RecptName:MSG_CO_ORecptName), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(mode?MSG_CO_RecptFirstname:MSG_CO_ORecptFirstname), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(mode?MSG_CO_RecptAddress:MSG_CO_ORecptAddress), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderName), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderFirstname), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderAddress), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderSubject), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderRFCDateTime), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderDate), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderTime), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderTimeZone), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderDOW), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderMsgID), MUIV_List_Insert_Bottom);

            // depending on the mode we have the "CompleteHeader" feature or not.
            if(mode == VPM_FORWARD || mode == VPM_REPLYINTRO)
            {
              DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_CompleteHeader), MUIV_List_Insert_Bottom);
            }
         }
         break;

         case VPM_QUOTE:
         {
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SenderInitials), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_Sender2Initials), MUIV_List_Insert_Bottom);
         }
         break;

         case VPM_ARCHIVE:
         {
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_ArchiveName), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_ArchiveFiles), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_ArchiveFilelist), MUIV_List_Insert_Bottom);
         }
         break;

         case VPM_MAILSTATS:
         {
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_NEWMSGS), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_UNREADMSGS), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_TOTALMSGS), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_DELMSGS), MUIV_List_Insert_Bottom);
            DoMethod(lv, MUIM_List_InsertSingle, GetStr(MSG_CO_SENTMSGS), MUIV_List_Insert_Bottom);
         }
         break;
      }

      DoMethod(lv,MUIM_Notify,MUIA_Listview_DoubleClick,TRUE,po,2,MUIM_Popstring_Close,TRUE);
      DoMethod(*string, MUIM_Notify, MUIA_Disabled, MUIV_EveryTime, po, 3, MUIM_Set, MUIA_Disabled, MUIV_TriggerValue);
   }
   return po;
}

///
/// MakePhraseGroup
//  Creates a cycle/string gadgets for forward and reply phrases
static Object *MakePhraseGroup(Object **hello, Object **intro, Object **bye, char *label, char *help)
{
   Object *grp, *cycl, *pgrp;
   static char *cytext[4];

   cytext[0] = GetStr(MSG_CO_PhraseOpen);
   cytext[1] = GetStr(MSG_CO_PhraseIntro);
   cytext[2] = GetStr(MSG_CO_PhraseClose);
   cytext[3] = NULL;

   if ((grp = HGroup,
         MUIA_Group_HorizSpacing, 1,
         Child, cycl = CycleObject,
            MUIA_CycleChain, 1,
            MUIA_Font, MUIV_Font_Button,
            MUIA_Cycle_Entries, cytext,
            MUIA_ControlChar, ShortCut(label),
            MUIA_Weight, 0,
         End,
         Child, pgrp = PageGroup,
            Child, MakeVarPop(hello, VPM_REPLYHELLO, SIZE_INTRO, ""),
            Child, MakeVarPop(intro, VPM_REPLYINTRO, SIZE_INTRO, ""),
            Child, MakeVarPop(bye,   VPM_REPLYBYE,   SIZE_INTRO, ""),
         End,
         MUIA_ShortHelp, help,
      End))
   {
      DoMethod(cycl, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, pgrp, 3, MUIM_Set, MUIA_Group_ActivePage, MUIV_TriggerValue);
   }
   return grp;
}

///
/// MakeStaticCheck
//  Creates non-interactive checkmark gadget
static Object *MakeStaticCheck(void)
{
   return
   ImageObject,
      ImageButtonFrame,
      MUIA_Image_Spec  , MUII_CheckMark,
      MUIA_Background  , MUII_ButtonBack,
      MUIA_ShowSelState, FALSE,
      MUIA_Selected    , TRUE,
      MUIA_Disabled    , TRUE,
   End;
}

///
/// CO_PlaySoundFunc
//  Plays sound file referred by the string gadget
HOOKPROTONHNO(CO_PlaySoundFunc, void, int *arg)
{
   PlaySound((STRPTR)xget((Object *)arg[0], MUIA_String_Contents));
}
MakeStaticHook(CO_PlaySoundHook,CO_PlaySoundFunc);
///
/// FilterDisplayFunc
// Filter list display hook
HOOKPROTONHNO(FilterDisplayFunc, LONG, struct NList_DisplayMessage *msg)
{
  struct FilterNode *entry;
  char **array;

  if(!msg)
    return 0;

  // now we set our local variables to the DisplayMessage structure ones
  entry = (struct FilterNode *)msg->entry;
  array = msg->strings;

  if(entry)
  {
    array[0] = entry->name;
    array[1] = entry->remote ? "x" : " ";
    array[2] = entry->applyToNew && !entry->remote ?  "x" : " ";
    array[3] = entry->applyToSent && !entry->remote ? "x" : " ";
    array[4] = entry->applyOnReq && !entry->remote ?  "x" : " ";
  }
  else
  {
    array[0] = GetStr(MSG_CO_Filter_Name);
    array[1] = GetStr(MSG_CO_Filter_RType);
    array[2] = GetStr(MSG_CO_Filter_NType);
    array[3] = GetStr(MSG_CO_Filter_SType);
    array[4] = GetStr(MSG_CO_Filter_UType);
  }

  return 0;
}
MakeStaticHook(FilterDisplayHook, FilterDisplayFunc);

///
/// UpdateCheckHook
// initiates an interactive update check
HOOKPROTONHNONP(UpdateCheckFunc, void)
{
  struct CO_GUIData *gui = &G->CO->GUI;

  // perform the update check and update our open GUI
  // elements accordingly.
  CheckForUpdates();

  // update the status and lastupdatedate label
  switch(C->LastUpdateStatus)
  {
    case UST_NOCHECK:
      set(gui->TX_UPDATESTATUS, MUIA_Text_Contents, GetStr(MSG_CO_LASTSTATUS_NOCHECK));
    break;

    case UST_NOUPDATE:
      set(gui->TX_UPDATESTATUS, MUIA_Text_Contents, GetStr(MSG_CO_LASTSTATUS_NOUPDATE));
    break;

    case UST_NOQUERY:
      set(gui->TX_UPDATESTATUS, MUIA_Text_Contents, GetStr(MSG_CO_LASTSTATUS_NOQUERY));
    break;

    case UST_UPDATESUCCESS:
      set(gui->TX_UPDATESTATUS, MUIA_Text_Contents, GetStr(MSG_CO_LASTSTATUS_UPDATESUCCESS));
    break;
  }

  // set the lastUpdateCheckDate
  if(C->LastUpdateStatus != UST_NOCHECK && C->LastUpdateCheck.Seconds > 0)
  {
    char buf[SIZE_DEFAULT];

    TimeVal2String(buf, &C->LastUpdateCheck, DSS_DATETIME, TZC_NONE);
    set(gui->TX_UPDATEDATE, MUIA_Text_Contents, buf);
  }
  else
  {
    // no update check was yet performed, so we clear our status gadgets
    set(gui->TX_UPDATEDATE, MUIA_Text_Contents, "");
  }
}
MakeStaticHook(UpdateCheckHook, UpdateCheckFunc);

///
/// AddMimeTypeHook()
//  Adds a new MIME type structure to the internal list
HOOKPROTONHNONP(AddMimeTypeFunc, void)
{
  struct CO_GUIData *gui = &G->CO->GUI;
  struct MimeTypeNode *mt;

  ENTER();

  if((mt = CreateNewMimeType()))
  {
    // add the new mime type to our internal list of
    // user definable MIME types.
    AddTail((struct List *)&(CE->mimeTypeList), (struct Node *)mt);

    // add the new MimeType also to the config page.
    DoMethod(gui->LV_MIME, MUIM_List_InsertSingle, mt, MUIV_List_Insert_Bottom);

    // make sure the new entry is the active entry and that the list
    // is also the active gadget in the window.
    set(gui->LV_MIME, MUIA_List_Active, MUIV_List_Active_Bottom);
    set(gui->WI, MUIA_Window_ActiveObject, gui->ST_CTYPE);
  }

  LEAVE();
}
MakeStaticHook(AddMimeTypeHook, AddMimeTypeFunc);

///
/// DelMimeTypeHook()
//  Deletes an entry from the MIME type list.
HOOKPROTONHNONP(DelMimeTypeFunc, void)
{
  struct CO_GUIData *gui = &G->CO->GUI;
  int pos;

  ENTER();

  if((pos = xget(gui->LV_MIME, MUIA_List_Active)) != MUIV_List_Active_Off)
  {
    struct MimeTypeNode *mt = NULL;

    DoMethod(gui->LV_MIME, MUIM_List_GetEntry, pos, &mt);

    if(mt)
    {
      // remove from MUI list
      DoMethod(gui->LV_MIME, MUIM_List_Remove, pos);

      // remove from internal list
      Remove((struct Node *)mt);

      // free memory.
      free(mt);
    }
  }

  LEAVE();
}
MakeStaticHook(DelMimeTypeHook, DelMimeTypeFunc);

///
/// GetMimeTypeEntryHook()
//  Fills form with data from selected list entry
HOOKPROTONHNONP(GetMimeTypeEntryFunc, void)
{
  struct MimeTypeNode *mt = NULL;
  struct CO_GUIData *gui = &G->CO->GUI;

  ENTER();

  DoMethod(gui->LV_MIME, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &mt);
  if(mt)
  {
    nnset(gui->ST_CTYPE, MUIA_String_Contents, mt->ContentType);
    nnset(gui->ST_EXTENS, MUIA_String_Contents, mt->Extension);
    nnset(gui->ST_COMMAND, MUIA_String_Contents, mt->Command);
    nnset(gui->ST_DESCRIPTION, MUIA_String_Contents, mt->Description);
  }

  set(gui->GR_MIME, MUIA_Disabled, !mt);
  set(gui->BT_MDEL, MUIA_Disabled, !mt);

  LEAVE();
}
MakeStaticHook(GetMimeTypeEntryHook, GetMimeTypeEntryFunc);

///
/// PutMimeTypeEntryHook()
//  Fills form data into selected list entry
HOOKPROTONHNONP(PutMimeTypeEntryFunc, void)
{
  struct MimeTypeNode *mt = NULL;
  struct CO_GUIData *gui = &G->CO->GUI;

  ENTER();

  DoMethod(gui->LV_MIME, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, &mt);
  if(mt)
  {
    GetMUIString(mt->ContentType, gui->ST_CTYPE, sizeof(mt->ContentType));
    GetMUIString(mt->Extension, gui->ST_EXTENS, sizeof(mt->Extension));
    GetMUIString(mt->Command, gui->ST_COMMAND, sizeof(mt->Command));
    GetMUIString(mt->Description, gui->ST_DESCRIPTION, sizeof(mt->Description));

    DoMethod(gui->LV_MIME, MUIM_List_Redraw, MUIV_List_Redraw_Active);
  }

  LEAVE();
}
MakeStaticHook(PutMimeTypeEntryHook, PutMimeTypeEntryFunc);
///
/// MimeTypeDisplayHook()
// display hook for the mime type list
HOOKPROTONH(MimeTypeDisplayFunc, LONG, char **array, struct MimeTypeNode *mt)
{
  array[0] = mt->ContentType;

  return 0;
}
MakeStaticHook(MimeTypeDisplayHook, MimeTypeDisplayFunc);

///

/*** Pages ***/
/// CO_Page0  (Start)
Object *CO_Page0(struct CO_ClassData *data)
{
   Object *grp;
   static char *tzone[34];

   ENTER();

   tzone[ 0] = GetStr(MSG_CO_TZoneM12);
   tzone[ 1] = GetStr(MSG_CO_TZoneM11);
   tzone[ 2] = GetStr(MSG_CO_TZoneM10);
   tzone[ 3] = GetStr(MSG_CO_TZoneM9);
   tzone[ 4] = GetStr(MSG_CO_TZoneM8);
   tzone[ 5] = GetStr(MSG_CO_TZoneM7);
   tzone[ 6] = GetStr(MSG_CO_TZoneM6);
   tzone[ 7] = GetStr(MSG_CO_TZoneM5);
   tzone[ 8] = GetStr(MSG_CO_TZoneM4);
   tzone[ 9] = GetStr(MSG_CO_TZoneM330);
   tzone[10] = GetStr(MSG_CO_TZoneM3);
   tzone[11] = GetStr(MSG_CO_TZoneM2);
   tzone[12] = GetStr(MSG_CO_TZoneM1);
   tzone[13] = GetStr(MSG_CO_TZone0);
   tzone[14] = GetStr(MSG_CO_TZone1);
   tzone[15] = GetStr(MSG_CO_TZone2);
   tzone[16] = GetStr(MSG_CO_TZone3);
   tzone[17] = GetStr(MSG_CO_TZone330);
   tzone[18] = GetStr(MSG_CO_TZone4);
   tzone[19] = GetStr(MSG_CO_TZone430);
   tzone[20] = GetStr(MSG_CO_TZone5);
   tzone[21] = GetStr(MSG_CO_TZone530);
   tzone[22] = GetStr(MSG_CO_TZone545);
   tzone[23] = GetStr(MSG_CO_TZone6);
   tzone[24] = GetStr(MSG_CO_TZone630);
   tzone[25] = GetStr(MSG_CO_TZone7);
   tzone[26] = GetStr(MSG_CO_TZone8);
   tzone[27] = GetStr(MSG_CO_TZone9);
   tzone[28] = GetStr(MSG_CO_TZone930);
   tzone[29] = GetStr(MSG_CO_TZone10);
   tzone[30] = GetStr(MSG_CO_TZone11);
   tzone[31] = GetStr(MSG_CO_TZone12);
   tzone[32] = GetStr(MSG_CO_TZone13);
   tzone[33] = NULL;

   if ((grp = VGroup,
         MUIA_HelpNode, "CO00",

         Child, HGroup,
            Child, MakeImageObject("config_firststep_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_FIRSTSTEPS_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_FIRSTSTEPS_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, ColGroup(2), GroupFrameT(GetStr(MSG_CO_MinConfig)),
            Child, Label2(GetStr(MSG_CO_RealName)),
            Child, data->GUI.ST_REALNAME = MakeString(SIZE_REALNAME,GetStr(MSG_CO_RealName)),
            Child, Label2(GetStr(MSG_CO_EmailAddress)),
            Child, data->GUI.ST_EMAIL    = MakeString(SIZE_ADDRESS,GetStr(MSG_CO_EmailAddress)),
            Child, Label2(GetStr(MSG_CO_POPServer)),
            Child, data->GUI.ST_POPHOST0  = MakeString(SIZE_HOST,GetStr(MSG_CO_POPServer)),
            Child, Label2(GetStr(MSG_CO_Password)),
            Child, data->GUI.ST_PASSWD0   = MakePassString(GetStr(MSG_CO_Password)),
         End,

         Child, ColGroup(2), GroupFrameT(GetStr(MSG_CO_SYSTEMSETTINGS)),
            Child, Label2(GetStr(MSG_CO_TimeZone)),
            Child, data->GUI.CY_TZONE = MakeCycle(tzone,GetStr(MSG_CO_TimeZone)),
            Child, HSpace(1),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_DLSAVING, GetStr(MSG_CO_DaylightSaving)),
            Child, Label2(GetStr(MSG_CO_DEFAULTCHARSET)),
            Child, MakeCharsetPop((Object **)&data->GUI.ST_DEFAULTCHARSET),
            Child, HSpace(1),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_DETECTCYRILLIC, GetStr(MSG_CO_DETECT_CYRILLIC)),
         End,
         Child, HVSpace,
      End))
   {
      SetHelp(data->GUI.ST_REALNAME,       MSG_HELP_CO_ST_REALNAME);
      SetHelp(data->GUI.ST_EMAIL,          MSG_HELP_CO_ST_EMAIL);
      SetHelp(data->GUI.ST_POPHOST0,       MSG_HELP_CO_ST_POPHOST);
      SetHelp(data->GUI.ST_PASSWD0,        MSG_HELP_CO_ST_PASSWD);
      SetHelp(data->GUI.CY_TZONE,          MSG_HELP_CO_CY_TZONE);
      SetHelp(data->GUI.CH_DLSAVING,       MSG_HELP_CO_CH_DLSAVING);
      SetHelp(data->GUI.CH_DETECTCYRILLIC, MSG_HELP_CO_DETECT_CYRILLIC);

      DoMethod(data->GUI.ST_POPHOST0 ,MUIM_Notify,MUIA_String_Contents, MUIV_EveryTime,MUIV_Notify_Application ,3,MUIM_CallHook,&CO_GetDefaultPOPHook,0);
      DoMethod(data->GUI.ST_PASSWD0  ,MUIM_Notify,MUIA_String_Contents, MUIV_EveryTime,MUIV_Notify_Application ,3,MUIM_CallHook,&CO_GetDefaultPOPHook,0);
   }

   RETURN(grp);
   return grp;
}

///
/// CO_Page1  (TCP/IP)
Object *CO_Page1(struct CO_ClassData *data)
{
   Object *grp;
   static char *secureMethods[4];

   secureMethods[0] = GetStr(MSG_CO_SMTPSECURE_NO);
   secureMethods[1] = GetStr(MSG_CO_SMTPSECURE_TLS);
   secureMethods[2] = GetStr(MSG_CO_SMTPSECURE_SSL);
   secureMethods[3] = NULL;

   if ((grp = VGroup,
         MUIA_HelpNode, "CO01",

         Child, HGroup,
            Child, MakeImageObject("config_network_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_TCPIP_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_TCPIP_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, ScrollgroupObject,
           MUIA_Scrollgroup_FreeHoriz, FALSE,
           MUIA_Scrollgroup_Contents, VGroupV,
              Child, VGroup, GroupFrameT(GetStr(MSG_CO_SendMail)),
                 MUIA_Weight, 0,
                 Child, HGroup,
                    Child, VGroup,
                       Child, ColGroup(2),
                          Child, Label2(GetStr(MSG_CO_Server)),
                          Child, data->GUI.ST_SMTPHOST = MakeString(SIZE_HOST,GetStr(MSG_CO_Server)),
                          Child, Label2(GetStr(MSG_CO_ST_SMTPPORT)),
                          Child, data->GUI.ST_SMTPPORT = MakeInteger(5, GetStr(MSG_CO_ST_SMTPPORT)),
                          Child, Label2(GetStr(MSG_CO_Domain)),
                          Child, data->GUI.ST_DOMAIN = MakeString(SIZE_HOST,GetStr(MSG_CO_Domain)),
                       End,
                       Child, VSpace(3),
                       Child, MakeCheckGroup((Object **)&data->GUI.CH_SMTP8BIT, GetStr(MSG_CO_Allow8bit)),
                       Child, VSpace(0),
                    End,
                    Child, VGroup,
                       Child, ColGroup(2),
                         Child, data->GUI.CH_USESMTPAUTH = MakeCheck(GetStr(MSG_CO_UseSMTPAUTH)),
                         Child, LLabel1(GetStr(MSG_CO_UseSMTPAUTH)),
                         Child, HSpace(0),
                         Child, ColGroup(2),
                            Child, Label2(GetStr(MSG_CO_SMTPUser)),
                            Child, data->GUI.ST_SMTPAUTHUSER = MakeString(SIZE_USERID,GetStr(MSG_CO_SMTPUser)),
                            Child, Label2(GetStr(MSG_CO_SMTPPass)),
                            Child, data->GUI.ST_SMTPAUTHPASS = MakePassString(GetStr(MSG_CO_SMTPPass)),
                         End,
                       End,
                       Child, LLabel(GetStr(MSG_CO_SMTPSECURE)),
                       Child, HGroup,
                         Child, HSpace(5),
                         Child, data->GUI.RA_SMTPSECURE = RadioObject,
                           MUIA_Radio_Entries, secureMethods,
                           MUIA_CycleChain,    TRUE,
                         End,
                         Child, HSpace(0),
                       End,
                       Child, VSpace(0),
                    End,
                 End,
              End,
              Child, HGroup, GroupFrameT(GetStr(MSG_CO_ReceiveMail)),
                 Child, ListviewObject,
                    MUIA_CycleChain, TRUE,
                    MUIA_Weight,     60,
                    MUIA_Listview_List, data->GUI.LV_POP3 = ListObject,
                       InputListFrame,
                    End,
                 End,
                 Child, VGroup,
                    Child, data->GUI.GR_POP3 = VGroup,
                       Child, ColGroup(2),
                          Child, Label2(GetStr(MSG_CO_POPServer)),
                          Child, data->GUI.ST_POPHOST = MakeString(SIZE_HOST,GetStr(MSG_CO_POPServer)),
                          Child, Label2(GetStr(MSG_CO_ST_POPPORT)),
                          Child, data->GUI.ST_POPPORT = MakeInteger(5, GetStr(MSG_CO_ST_POPPORT)),
                          Child, Label2(GetStr(MSG_CO_POPUserID)),
                          Child, data->GUI.ST_POPUSERID = MakeString(SIZE_USERID,GetStr(MSG_CO_POPUserID)),
                          Child, Label2(GetStr(MSG_CO_Password)),
                          Child, data->GUI.ST_PASSWD = MakePassString(GetStr(MSG_CO_Password)),
                       End,
                       Child, MakeCheckGroup((Object **)&data->GUI.CH_POPENABLED, GetStr(MSG_CO_POPActive)),
                       Child, HGroup,
                         Child, MakeCheckGroup((Object **)&data->GUI.CH_POP3SSL, GetStr(MSG_CO_POP3SSL)),
                         Child, MakeCheckGroup((Object **)&data->GUI.CH_USESTLS, GetStr(MSG_CO_USESTLS)),
                       End,
                       Child, MakeCheckGroup((Object **)&data->GUI.CH_USEAPOP, GetStr(MSG_CO_UseAPOP)),
                       Child, MakeCheckGroup((Object **)&data->GUI.CH_DELETE, GetStr(MSG_CO_DeleteServerMail)),
                    End,
                    Child, HVSpace,
                    Child, ColGroup(2),
                       Child, data->GUI.BT_PADD = MakeButton(GetStr(MSG_Add)),
                       Child, data->GUI.BT_PDEL = MakeButton(GetStr(MSG_Del)),
                    End,
                 End,
              End,
            End,
         End,
      End))
   {
      SetHelp(data->GUI.ST_SMTPHOST    ,MSG_HELP_CO_ST_SMTPHOST     );
      SetHelp(data->GUI.ST_SMTPPORT    ,MSG_HELP_CO_ST_SMTPPORT     );
      SetHelp(data->GUI.ST_DOMAIN      ,MSG_HELP_CO_ST_DOMAIN       );
      SetHelp(data->GUI.CH_SMTP8BIT    ,MSG_HELP_CO_CH_SMTP8BIT     );
      SetHelp(data->GUI.CH_USESMTPAUTH ,MSG_HELP_CO_CH_USESMTPAUTH  );
      SetHelp(data->GUI.ST_SMTPAUTHUSER,MSG_HELP_CO_ST_SMTPAUTHUSER );
      SetHelp(data->GUI.ST_SMTPAUTHPASS,MSG_HELP_CO_ST_SMTPAUTHPASS );
      SetHelp(data->GUI.LV_POP3        ,MSG_HELP_CO_LV_POP3         );
      SetHelp(data->GUI.BT_PADD        ,MSG_HELP_CO_BT_PADD         );
      SetHelp(data->GUI.BT_PDEL        ,MSG_HELP_CO_BT_PDEL         );
      SetHelp(data->GUI.ST_POPHOST     ,MSG_HELP_CO_ST_POPHOST      );
      SetHelp(data->GUI.ST_POPPORT     ,MSG_HELP_CO_ST_POPPORT      );
      SetHelp(data->GUI.ST_POPUSERID   ,MSG_HELP_CO_ST_POPUSERID    );
      SetHelp(data->GUI.ST_PASSWD      ,MSG_HELP_CO_ST_PASSWD       );
      SetHelp(data->GUI.CH_DELETE      ,MSG_HELP_CO_CH_DELETE       );
      SetHelp(data->GUI.CH_USEAPOP     ,MSG_HELP_CO_CH_USEAPOP      );
      SetHelp(data->GUI.CH_POPENABLED  ,MSG_HELP_CO_CH_POPENABLED   );
      SetHelp(data->GUI.RA_SMTPSECURE  ,MSG_HELP_CO_RA_SMTPSECURE   );
      SetHelp(data->GUI.CH_POP3SSL     ,MSG_HELP_CO_CH_POP3SSL      );
      SetHelp(data->GUI.CH_USESTLS     ,MSG_HELP_CO_CH_USESTLS      );

      DoMethod(data->GUI.LV_POP3       ,MUIM_Notify,MUIA_List_Active    ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_GetP3EntryHook,0);
      DoMethod(data->GUI.ST_POPHOST    ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.ST_POPPORT    ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.ST_POPUSERID  ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.ST_PASSWD     ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.CH_POPENABLED ,MUIM_Notify,MUIA_Selected       ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.CH_USEAPOP    ,MUIM_Notify,MUIA_Selected       ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.CH_POP3SSL    ,MUIM_Notify,MUIA_Selected       ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.CH_USESTLS    ,MUIM_Notify,MUIA_Selected       ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.CH_DELETE     ,MUIM_Notify,MUIA_Selected       ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_PutP3EntryHook,0);
      DoMethod(data->GUI.BT_PADD       ,MUIM_Notify,MUIA_Pressed        ,FALSE         ,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_AddPOP3Hook,0);
      DoMethod(data->GUI.BT_PDEL       ,MUIM_Notify,MUIA_Pressed        ,FALSE         ,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_DelPOP3Hook,0);
      DoMethod(data->GUI.CH_USESMTPAUTH,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,MUIV_Notify_Application,5,MUIM_MultiSet,MUIA_Disabled,MUIV_NotTriggerValue,data->GUI.ST_SMTPAUTHUSER, data->GUI.ST_SMTPAUTHPASS);
      DoMethod(data->GUI.CH_POP3SSL    ,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,data->GUI.CH_USESTLS,3,MUIM_Set,MUIA_Disabled,MUIV_NotTriggerValue);
      DoMethod(data->GUI.RA_SMTPSECURE ,MUIM_Notify,MUIA_Radio_Active, 0,data->GUI.RA_SMTPSECURE,3,MUIM_Set,MUIA_Disabled, !G->TR_UseableTLS);

      // disable some gadgets per default
      set(data->GUI.ST_SMTPAUTHUSER, MUIA_Disabled, TRUE);
      set(data->GUI.ST_SMTPAUTHPASS, MUIA_Disabled, TRUE);
   }

   return grp;
}

///
/// CO_Page2  (New Mail)
Object *CO_Page2(struct CO_ClassData *data)
{
   static char *mpsopt[5], *trwopt[4];
   Object *grp;
   Object *pa_notisound;
   Object *bt_notisound;
   Object *pa_noticmd;

   mpsopt[0] = GetStr(MSG_CO_PSNever);
   mpsopt[1] = GetStr(MSG_CO_PSLarge);
   mpsopt[2] = GetStr(MSG_CO_PSAlways);
   mpsopt[3] = GetStr(MSG_CO_PSAlwaysFast);
   mpsopt[4] = NULL;
   trwopt[0] = GetStr(MSG_CO_TWNever);
   trwopt[1] = GetStr(MSG_CO_TWAuto);
   trwopt[2] = GetStr(MSG_CO_TWAlways);
   trwopt[3] = NULL;

   if ((grp = VGroup,
         MUIA_HelpNode, "CO02",

         Child, HGroup,
            Child, MakeImageObject("config_newmail_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_NEWMAIL_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_NEWMAIL_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, HGroup, GroupFrameT(GetStr(MSG_CO_Download)),
            Child, ColGroup(2),

               Child, Label(GetStr(MSG_CO_PreSelect)),
               Child, data->GUI.CY_MSGSELECT = MakeCycle(mpsopt, GetStr(MSG_CO_PreSelect)),

               Child, Label(GetStr(MSG_CO_TransferWin)),
               Child, data->GUI.CY_TRANSWIN = MakeCycle(trwopt, GetStr(MSG_CO_TransferWin)),

               Child, Label(GetStr(MSG_CO_WarnSize1)),
               Child, HGroup,
                  Child, data->GUI.ST_WARNSIZE = MakeInteger(5, GetStr(MSG_CO_WarnSize1)),
                  Child, LLabel(GetStr(MSG_CO_WarnSize2)),
               End,
            End,

            Child, HSpace(0),

            Child, ColGroup(2),
               Child, Label2(GetStr(MSG_CO_AvoidDuplicates)),
               Child, data->GUI.CH_AVOIDDUP = MakeCheck(GetStr(MSG_CO_AvoidDuplicates)),
               Child, Label2(GetStr(MSG_CO_UpdateStatus)),
               Child, data->GUI.CH_UPDSTAT = MakeCheck(GetStr(MSG_CO_UpdateStatus)),
            End,

         End,
         Child, VGroup, GroupFrameT(GetStr(MSG_CO_AutoOperation)),
            Child, HGroup,
               Child, Label(GetStr(MSG_CO_CheckMail)),
               Child, data->GUI.NM_INTERVAL = NumericbuttonObject,
                 MUIA_CycleChain,      TRUE,
                 MUIA_Numeric_Min,     0,
                 MUIA_Numeric_Max,     240,
                 MUIA_Numeric_Default, 5,
               End,
               Child, Label(GetStr(MSG_CO_Minutes)),
               Child, HSpace(0),
            End,
            Child, MakeCheckGroup((Object **)&data->GUI.CH_DLLARGE, GetStr(MSG_CO_DownloadLarge)),
         End,
         Child, ColGroup(3), GroupFrameT(GetStr(MSG_CO_Notification)),
            Child, data->GUI.CH_NOTIREQ = MakeCheck(GetStr(MSG_CO_NotiReq)),
            Child, LLabel(GetStr(MSG_CO_NotiReq)),
            Child, HSpace(0),
            Child, data->GUI.CH_NOTISOUND = MakeCheck(GetStr(MSG_CO_NotiSound)),
            Child, LLabel(GetStr(MSG_CO_NotiSound)),
            Child, HGroup,
               MUIA_Group_HorizSpacing, 0,
               Child, pa_notisound = PopaslObject,
                  MUIA_Popasl_Type     ,0,
                  MUIA_Popstring_String,data->GUI.ST_NOTISOUND = MakeString(SIZE_PATHFILE,""),
                  MUIA_Popstring_Button,PopButton(MUII_PopFile),
               End,
               Child, bt_notisound = PopButton(MUII_TapePlay),
            End,
            Child, data->GUI.CH_NOTICMD = MakeCheck(GetStr(MSG_CO_NotiCommand)),
            Child, LLabel(GetStr(MSG_CO_NotiCommand)),
            Child, pa_noticmd = PopaslObject,
               MUIA_Popasl_Type     ,0,
               MUIA_Popstring_String,data->GUI.ST_NOTICMD = MakeString(SIZE_COMMAND,""),
               MUIA_Popstring_Button,PopButton(MUII_PopFile),
            End,
         End,
         Child, HVSpace,
      End))
   {
      SetHelp(data->GUI.CH_AVOIDDUP  ,MSG_HELP_CO_CH_AVOIDDUP  );
      SetHelp(data->GUI.CY_TRANSWIN  ,MSG_HELP_CO_CH_TRANSWIN  );
      SetHelp(data->GUI.CY_MSGSELECT ,MSG_HELP_CO_CY_MSGSELECT );
      SetHelp(data->GUI.CH_UPDSTAT   ,MSG_HELP_CO_CH_UPDSTAT   );
      SetHelp(data->GUI.ST_WARNSIZE  ,MSG_HELP_CO_ST_WARNSIZE  );
      SetHelp(data->GUI.NM_INTERVAL  ,MSG_HELP_CO_ST_INTERVAL  );
      SetHelp(data->GUI.CH_DLLARGE   ,MSG_HELP_CO_CH_DLLARGE   );
      SetHelp(data->GUI.CH_NOTIREQ   ,MSG_HELP_CO_CH_NOTIREQ   );
      SetHelp(data->GUI.ST_NOTICMD   ,MSG_HELP_CO_ST_NOTICMD   );
      SetHelp(data->GUI.ST_NOTISOUND ,MSG_HELP_CO_ST_NOTISOUND );
      DoMethod(G->App, MUIM_MultiSet, MUIA_Disabled, TRUE, pa_notisound, bt_notisound, pa_noticmd, NULL);
      set(bt_notisound,MUIA_CycleChain,1);
      DoMethod(bt_notisound          ,MUIM_Notify,MUIA_Pressed ,FALSE         ,MUIV_Notify_Application,3,MUIM_CallHook,&CO_PlaySoundHook,data->GUI.ST_NOTISOUND);
      DoMethod(data->GUI.CH_NOTISOUND,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,pa_notisound           ,3,MUIM_Set,MUIA_Disabled,MUIV_NotTriggerValue);
      DoMethod(data->GUI.CH_NOTISOUND,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,bt_notisound           ,3,MUIM_Set,MUIA_Disabled,MUIV_NotTriggerValue);
      DoMethod(data->GUI.CH_NOTICMD  ,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,pa_noticmd             ,3,MUIM_Set,MUIA_Disabled,MUIV_NotTriggerValue);
   }
   return grp;
}

///
/// CO_Page3  (Filters)
Object *CO_Page3(struct CO_ClassData *data)
{
   static char *rtitles[4];
   Object *grp;
   Object *bt_moveto;

   rtitles[0] = GetStr(MSG_Options);
   rtitles[1] = GetStr(MSG_CO_Comparison);
   rtitles[2] = GetStr(MSG_CO_Action);
   rtitles[3] = NULL;

   if((grp = VGroup,
         MUIA_HelpNode, "CO03",

         Child, HGroup,
            Child, MakeImageObject("config_filters_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_FILTER_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_FILTER_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, HGroup,
              Child, VGroup,
                 MUIA_Weight, 70,
                 Child, NListviewObject,
                    MUIA_CycleChain, TRUE,
                    MUIA_NListview_NList, data->GUI.LV_RULES = NListObject,
                       InputListFrame,
                       MUIA_NList_Format,       "BAR, P=\033c NB CW=1 MICW=1 MACW=1, P=\033c NB CW=1 MICW=1 MACW=1, P=\033c NB CW=1 MICW=1 MACW=1, P=\033c NB CW=1 MICW=1 MACW=1",
                       MUIA_NList_Title,        TRUE,
                       MUIA_NList_DragType,     MUIV_NList_DragType_Immediate,
                       MUIA_NList_DragSortable, TRUE,
                       MUIA_NList_DisplayHook2, &FilterDisplayHook,
                    End,
                 End,
                 Child, ColGroup(3),
                    Child, data->GUI.BT_RADD = MakeButton(GetStr(MSG_Add)),
                    Child, data->GUI.BT_RDEL = MakeButton(GetStr(MSG_Del)),
                    Child, ColGroup(2),
                      Child, data->GUI.BT_FILTERUP = PopButton(MUII_ArrowUp),
                      Child, data->GUI.BT_FILTERDOWN = PopButton(MUII_ArrowDown),
                    End,
                 End,
              End,
              Child, BalanceObject, End,
              Child, RegisterGroup(rtitles),
                 MUIA_CycleChain, TRUE,
                 Child, VGroup,
                    Child, HGroup,
                       Child, Label2(GetStr(MSG_CO_Name)),
                       Child, data->GUI.ST_RNAME = MakeString(SIZE_NAME,GetStr(MSG_CO_Name)),
                    End,
                    Child, MakeCheckGroup((Object **)&data->GUI.CH_REMOTE, GetStr(MSG_CO_Remote)),
                    Child, MakeCheckGroup((Object **)&data->GUI.CH_APPLYNEW, GetStr(MSG_CO_ApplyToNew)),
                    Child, MakeCheckGroup((Object **)&data->GUI.CH_APPLYSENT, GetStr(MSG_CO_ApplyToSent)),
                    Child, MakeCheckGroup((Object **)&data->GUI.CH_APPLYREQ, GetStr(MSG_CO_ApplyOnReq)),
                    Child, HVSpace,
                 End,
                 Child, data->GUI.GR_RGROUP = VGroup,
                    Child, ScrollgroupObject,
                       GroupSpacing(3),
                       MUIA_Weight, 100,
                       MUIA_Scrollgroup_FreeHoriz, FALSE,
                       MUIA_Scrollgroup_FreeVert,  TRUE,
                       MUIA_Scrollgroup_Contents,  data->GUI.GR_SGROUP = VirtgroupObject,
                         Child, SearchControlGroupObject, // we need a minimum of one dummy control group
                         End,
                       End,
                    End,
                    Child, RectangleObject,
                       MUIA_Weight, 1,
                    End,
                     Child, RectangleObject,
                         MUIA_Rectangle_HBar, TRUE,
                        MUIA_FixHeight,      4,
                     End,
                    Child, HGroup,
                       Child, data->GUI.BT_MORE = MakeButton(GetStr(MSG_CO_More)),
                       Child, HVSpace,
                       Child, data->GUI.BT_LESS = MakeButton(GetStr(MSG_CO_Less)),
                    End,
                 End,
                 Child, VGroup,
                    Child, ColGroup(3),
                       Child, data->GUI.CH_ABOUNCE = MakeCheck(GetStr(MSG_CO_ActionBounce)),
                       Child, LLabel2(GetStr(MSG_CO_ActionBounce)),
                       Child, MakeAddressField(&data->GUI.ST_ABOUNCE, GetStr(MSG_CO_ActionBounce), MSG_HELP_CO_ST_ABOUNCE, ABM_EDIT, -1, FALSE),
                       Child, data->GUI.CH_AFORWARD = MakeCheck(GetStr(MSG_CO_ActionForward)),
                       Child, LLabel2(GetStr(MSG_CO_ActionForward)),
                       Child, MakeAddressField(&data->GUI.ST_AFORWARD, GetStr(MSG_CO_ActionForward), MSG_HELP_CO_ST_AFORWARD, ABM_EDIT, -1, FALSE),
                       Child, data->GUI.CH_ARESPONSE = MakeCheck(GetStr(MSG_CO_ActionReply)),
                       Child, LLabel2(GetStr(MSG_CO_ActionReply)),
                       Child, data->GUI.PO_ARESPONSE = PopaslObject,
                          MUIA_Popstring_String, data->GUI.ST_ARESPONSE = MakeString(SIZE_PATHFILE, ""),
                          MUIA_Popstring_Button, PopButton(MUII_PopFile),
                       End,
                       Child, data->GUI.CH_AEXECUTE = MakeCheck(GetStr(MSG_CO_ActionExecute)),
                       Child, LLabel2(GetStr(MSG_CO_ActionExecute)),
                       Child, data->GUI.PO_AEXECUTE = PopaslObject,
                          MUIA_Popstring_String, data->GUI.ST_AEXECUTE = MakeString(SIZE_PATHFILE, ""),
                          MUIA_Popstring_Button, PopButton(MUII_PopFile),
                       End,
                       Child, data->GUI.CH_APLAY = MakeCheck(GetStr(MSG_CO_ActionPlay)),
                       Child, LLabel2(GetStr(MSG_CO_ActionPlay)),
                       Child, HGroup,
                          MUIA_Group_HorizSpacing, 0,
                          Child, data->GUI.PO_APLAY = PopaslObject,
                             MUIA_Popstring_String, data->GUI.ST_APLAY = MakeString(SIZE_PATHFILE, ""),
                             MUIA_Popstring_Button, PopButton(MUII_PopFile),
                          End,
                          Child, data->GUI.BT_APLAY = PopButton(MUII_TapePlay),
                       End,
                       Child, data->GUI.CH_AMOVE = MakeCheck(GetStr(MSG_CO_ActionMove)),
                       Child, LLabel2(GetStr(MSG_CO_ActionMove)),
                       Child, data->GUI.PO_MOVETO = PopobjectObject,
                           MUIA_Popstring_String, data->GUI.TX_MOVETO = TextObject,
                              TextFrame,
                           End,
                           MUIA_Popstring_Button,bt_moveto = PopButton(MUII_PopUp),
                           MUIA_Popobject_StrObjHook,&PO_InitFolderListHook,
                           MUIA_Popobject_ObjStrHook,&PO_List2TextHook,
                           MUIA_Popobject_WindowHook,&PO_WindowHook,
                           MUIA_Popobject_Object, data->GUI.LV_MOVETO = ListviewObject,
                              MUIA_Listview_List, ListObject, InputListFrame, End,
                          End,
                        End,
                    End,
                    Child, MakeCheckGroup((Object **)&data->GUI.CH_ADELETE, GetStr(MSG_CO_ActionDelete)),
                    Child, MakeCheckGroup((Object **)&data->GUI.CH_ASKIP, GetStr(MSG_CO_ActionSkip)),
                    Child, HVSpace,
                 End,
              End,
         End,
      End))
   {
      SetHelp(data->GUI.LV_RULES     ,MSG_HELP_CO_LV_RULES     );
      SetHelp(data->GUI.ST_RNAME     ,MSG_HELP_CO_ST_RNAME     );
      SetHelp(data->GUI.CH_REMOTE    ,MSG_HELP_CO_CH_REMOTE    );
      SetHelp(data->GUI.CH_APPLYNEW  ,MSG_HELP_CO_CH_APPLYNEW  );
      SetHelp(data->GUI.CH_APPLYSENT ,MSG_HELP_CO_CH_APPLYSENT );
      SetHelp(data->GUI.CH_APPLYREQ  ,MSG_HELP_CO_CH_APPLYREQ  );
      SetHelp(data->GUI.CH_ABOUNCE   ,MSG_HELP_CO_CH_ABOUNCE   );
      SetHelp(data->GUI.CH_AFORWARD  ,MSG_HELP_CO_CH_AFORWARD  );
      SetHelp(data->GUI.CH_ARESPONSE ,MSG_HELP_CO_CH_ARESPONSE );
      SetHelp(data->GUI.ST_ARESPONSE ,MSG_HELP_CO_ST_ARESPONSE );
      SetHelp(data->GUI.CH_AEXECUTE  ,MSG_HELP_CO_CH_AEXECUTE  );
      SetHelp(data->GUI.ST_AEXECUTE  ,MSG_HELP_CO_ST_AEXECUTE  );
      SetHelp(data->GUI.CH_APLAY     ,MSG_HELP_CO_CH_APLAY     );
      SetHelp(data->GUI.ST_APLAY     ,MSG_HELP_CO_ST_APLAY     );
      SetHelp(data->GUI.CH_AMOVE     ,MSG_HELP_CO_CH_AMOVE     );
      SetHelp(data->GUI.PO_MOVETO    ,MSG_HELP_CO_PO_MOVETO    );
      SetHelp(data->GUI.CH_ADELETE   ,MSG_HELP_CO_CH_ADELETE   );
      SetHelp(data->GUI.CH_ASKIP     ,MSG_HELP_CO_CH_ASKIP     );
      SetHelp(data->GUI.BT_RADD      ,MSG_HELP_CO_BT_RADD      );
      SetHelp(data->GUI.BT_RDEL      ,MSG_HELP_CO_BT_RDEL      );

      // set the cyclechain
      set(data->GUI.BT_APLAY, MUIA_CycleChain, TRUE);
      set(bt_moveto,MUIA_CycleChain, TRUE);
      set(data->GUI.BT_FILTERUP, MUIA_CycleChain, TRUE);
      set(data->GUI.BT_FILTERDOWN, MUIA_CycleChain, TRUE);

      GhostOutFilter(&(data->GUI), NULL);

      DoMethod(data->GUI.LV_RULES    ,MUIM_Notify,MUIA_NList_Active       ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&GetActiveFilterDataHook);
      DoMethod(data->GUI.ST_RNAME    ,MUIM_Notify,MUIA_String_Contents    ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_REMOTE   ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook ,&CO_RemoteToggleHook,MUIV_TriggerValue);
      DoMethod(data->GUI.CH_APPLYREQ ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_APPLYSENT,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_APPLYNEW ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_ABOUNCE  ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_AFORWARD ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_ARESPONSE,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_AEXECUTE ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_APLAY    ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_AMOVE    ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_ADELETE  ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.CH_ASKIP    ,MUIM_Notify,MUIA_Selected           ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.ST_ABOUNCE  ,MUIM_Notify,MUIA_String_BufferPos   ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.ST_AFORWARD ,MUIM_Notify,MUIA_String_BufferPos   ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.ST_ARESPONSE,MUIM_Notify,MUIA_String_Contents    ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.ST_AEXECUTE ,MUIM_Notify,MUIA_String_Contents    ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.ST_APLAY    ,MUIM_Notify,MUIA_String_Contents    ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.BT_APLAY    ,MUIM_Notify,MUIA_Pressed            ,FALSE         ,MUIV_Notify_Application,3,MUIM_CallHook, &CO_PlaySoundHook,data->GUI.ST_APLAY);
      DoMethod(data->GUI.TX_MOVETO   ,MUIM_Notify,MUIA_Text_Contents      ,MUIV_EveryTime,MUIV_Notify_Application,2,MUIM_CallHook ,&SetActiveFilterDataHook);
      DoMethod(data->GUI.LV_MOVETO   ,MUIM_Notify,MUIA_Listview_DoubleClick,TRUE         ,data->GUI.PO_MOVETO   ,2,MUIM_Popstring_Close,TRUE);
      DoMethod(data->GUI.LV_MOVETO   ,MUIM_Notify,MUIA_Listview_DoubleClick,TRUE         ,data->GUI.CH_AMOVE    ,3,MUIM_Set,MUIA_Selected,TRUE);
      DoMethod(data->GUI.BT_RADD,       MUIM_Notify, MUIA_Pressed,  FALSE,  MUIV_Notify_Application,  2,  MUIM_CallHook,    &AddNewFilterToListHook);
      DoMethod(data->GUI.BT_RDEL,       MUIM_Notify, MUIA_Pressed,  FALSE,  MUIV_Notify_Application,  2,  MUIM_CallHook,    &RemoveActiveFilterHook);
      DoMethod(data->GUI.BT_FILTERUP,   MUIM_Notify, MUIA_Pressed,  FALSE,  data->GUI.LV_RULES,       3,  MUIM_NList_Move,  MUIV_NList_Move_Selected, MUIV_NList_Move_Previous);
      DoMethod(data->GUI.BT_FILTERDOWN, MUIM_Notify, MUIA_Pressed,  FALSE,  data->GUI.LV_RULES,       3,  MUIM_NList_Move,  MUIV_NList_Move_Selected, MUIV_NList_Move_Next);
      DoMethod(data->GUI.CH_AMOVE,      MUIM_Notify, MUIA_Selected, TRUE,   data->GUI.CH_ADELETE,     3,  MUIM_Set,         MUIA_Selected,            FALSE);
      DoMethod(data->GUI.CH_ADELETE,    MUIM_Notify, MUIA_Selected, TRUE,   data->GUI.CH_AMOVE,       3,  MUIM_Set,         MUIA_Selected,            FALSE);
      DoMethod(data->GUI.BT_MORE,       MUIM_Notify, MUIA_Pressed,  FALSE,  MUIV_Notify_Application,  2,  MUIM_CallHook,    &AddNewRuleToListHook);
      DoMethod(data->GUI.BT_LESS,       MUIM_Notify, MUIA_Pressed,  FALSE,  MUIV_Notify_Application,  2,  MUIM_CallHook,    &RemoveLastRuleHook);
   }

   return grp;
}

///
/// CO_Page4  (Read)
Object *CO_Page4(struct CO_ClassData *data)
{
   Object *grp;
   static char *headopt[4], *siopt[5], *slopt[5];
   headopt[0] = GetStr(MSG_CO_HeadNone);
   headopt[1] = GetStr(MSG_CO_HeadShort);
   headopt[2] = GetStr(MSG_CO_HeadFull);
   headopt[3] = NULL;
   siopt[0] = GetStr(MSG_CO_SINone);
   siopt[1] = GetStr(MSG_CO_SIFields);
   siopt[2] = GetStr(MSG_CO_SIAll);
   siopt[3] = GetStr(MSG_CO_SImageOnly);
   siopt[4] = NULL;
   slopt[0] = GetStr(MSG_CO_SLBlank);
   slopt[1] = GetStr(MSG_CO_SLDash);
   slopt[2] = GetStr(MSG_CO_SLBar);
   slopt[3] = GetStr(MSG_CO_SLSkip);
   slopt[4] = NULL;
   if ((grp = VGroup,
         MUIA_HelpNode, "CO04",

         Child, HGroup,
            Child, MakeImageObject("config_read_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_READ_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_READ_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, ColGroup(3), GroupFrameT(GetStr(MSG_CO_HeaderLayout)),
            Child, Label2(GetStr(MSG_CO_Header)),
            Child, data->GUI.CY_HEADER = MakeCycle(headopt,GetStr(MSG_CO_Header)),
            Child, data->GUI.ST_HEADERS = MakeString(SIZE_PATTERN, ""),
            Child, Label1(GetStr(MSG_CO_SenderInfo)),
            Child, data->GUI.CY_SENDERINFO = MakeCycle(siopt,GetStr(MSG_CO_SenderInfo)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_WRAPHEAD, GetStr(MSG_CO_WrapHeader)),
         End,
         Child, ColGroup(3), GroupFrameT(GetStr(MSG_CO_BodyLayout)),
            Child, Label1(GetStr(MSG_CO_SignatureSep)),
            Child, data->GUI.CY_SIGSEPLINE = MakeCycle(slopt,GetStr(MSG_CO_SignatureSep)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_FIXFEDIT, GetStr(MSG_CO_FixedFontEdit)),
            Child, Label1(GetStr(MSG_CO_ColoredText)),
            Child, data->GUI.CA_COLTEXT = PoppenObject, MUIA_CycleChain, 1, End,
            Child, MakeCheckGroup((Object **)&data->GUI.CH_ALLTEXTS, GetStr(MSG_CO_DisplayAll)),
            Child, Label1(GetStr(MSG_CO_OldQuotes)),
            Child, HGroup,
              Child, data->GUI.CA_COL1QUOT = PoppenObject, MUIA_CycleChain, 1, End,
              Child, data->GUI.CA_COL2QUOT = PoppenObject, MUIA_CycleChain, 1, End,
              Child, data->GUI.CA_COL3QUOT = PoppenObject, MUIA_CycleChain, 1, End,
              Child, data->GUI.CA_COL4QUOT = PoppenObject, MUIA_CycleChain, 1, End,
            End,
            Child, MakeCheckGroup((Object **)&data->GUI.CH_TEXTSTYLES, GetStr(MSG_CO_UseTextstyles)),
            Child, Label1(GetStr(MSG_CO_URLCOLOR)),
            Child, data->GUI.CA_COLURL = PoppenObject, MUIA_CycleChain, 1, End,
            Child, HSpace(0),
         End,
         Child, VGroup, GroupFrameT(GetStr(MSG_CO_OtherOptions)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_MULTIWIN, GetStr(MSG_CO_MultiReadWin)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_EMBEDDEDREADPANE, GetStr(MSG_CO_SHOWEMBEDDEDREADPANE)),
            Child, HGroup,
               Child, data->GUI.CH_DELAYEDSTATUS = MakeCheck(GetStr(MSG_CO_ConfirmDelPart1)),
               Child, Label2(GetStr(MSG_CO_SETSTATUSDELAYED1)),
               Child, data->GUI.NB_DELAYEDSTATUS = NumericbuttonObject,
                 MUIA_CycleChain,  TRUE,
                 MUIA_Numeric_Min, 1,
                 MUIA_Numeric_Max, 10,
               End,
               Child, Label2(GetStr(MSG_CO_SETSTATUSDELAYED2)),
               Child, HSpace(0),
            End,
         End,
         Child, HVSpace,
      End))
   {
      set(data->GUI.ST_HEADERS, MUIA_Disabled, TRUE);

      SetHelp(data->GUI.CY_HEADER,          MSG_HELP_CO_CY_HEADER);
      SetHelp(data->GUI.ST_HEADERS,         MSG_HELP_CO_ST_HEADERS);
      SetHelp(data->GUI.CY_SENDERINFO,      MSG_HELP_CO_CY_SENDERINFO);
      SetHelp(data->GUI.CA_COLTEXT,         MSG_HELP_CO_CA_COLTEXT);
      SetHelp(data->GUI.CA_COL1QUOT,        MSG_HELP_CO_CA_COL1QUOT);
      SetHelp(data->GUI.CA_COL2QUOT,        MSG_HELP_CO_CA_COL2QUOT);
      SetHelp(data->GUI.CA_COL3QUOT,        MSG_HELP_CO_CA_COL3QUOT);
      SetHelp(data->GUI.CA_COL4QUOT,        MSG_HELP_CO_CA_COL4QUOT);
      SetHelp(data->GUI.CA_COLURL,          MSG_HELP_CO_CA_COLURL);
      SetHelp(data->GUI.CH_ALLTEXTS,        MSG_HELP_CO_CH_ALLTEXTS);
      SetHelp(data->GUI.CH_MULTIWIN,        MSG_HELP_CO_CH_MULTIWIN);
      SetHelp(data->GUI.CH_EMBEDDEDREADPANE,MSG_HELP_CO_CH_EMBEDDEDREADPANE);
      SetHelp(data->GUI.CY_SIGSEPLINE,      MSG_HELP_CO_CY_SIGSEPLINE);
      SetHelp(data->GUI.CH_FIXFEDIT,        MSG_HELP_CO_CH_FIXFEDIT);
      SetHelp(data->GUI.CH_WRAPHEAD,        MSG_HELP_CO_CH_WRAPHEAD);
      SetHelp(data->GUI.CH_TEXTSTYLES,      MSG_HELP_CO_CH_TEXTSTYLES);
      SetHelp(data->GUI.CH_DELAYEDSTATUS,   MSG_HELP_CO_SETSTATUSDELAYED);
      SetHelp(data->GUI.NB_DELAYEDSTATUS,   MSG_HELP_CO_SETSTATUSDELAYED);

      DoMethod(data->GUI.CY_HEADER   ,MUIM_Notify,MUIA_Cycle_Active   ,0             ,data->GUI.ST_HEADERS   ,3,MUIM_Set,MUIA_Disabled,TRUE);
      DoMethod(data->GUI.CY_HEADER   ,MUIM_Notify,MUIA_Cycle_Active   ,1             ,data->GUI.ST_HEADERS   ,3,MUIM_Set,MUIA_Disabled,FALSE);
      DoMethod(data->GUI.CY_HEADER   ,MUIM_Notify,MUIA_Cycle_Active   ,2             ,data->GUI.ST_HEADERS   ,3,MUIM_Set,MUIA_Disabled,TRUE);
   }

   return grp;
}

///
/// CO_Page5  (Write)
Object *CO_Page5(struct CO_ClassData *data)
{
   Object *grp;
   static char *wrapmode[4];
   wrapmode[0] = GetStr(MSG_CO_EWOff);
   wrapmode[1] = GetStr(MSG_CO_EWAsYouType);
   wrapmode[2] = GetStr(MSG_CO_EWBeforeSend);
   wrapmode[3] = NULL;
   if ((grp = VGroup,
         MUIA_HelpNode, "CO05",

         Child, HGroup,
            Child, MakeImageObject("config_write_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_WRITE_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_WRITE_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, ColGroup(2), GroupFrameT(GetStr(MSG_CO_MessageHeader)),
            Child, Label2(GetStr(MSG_CO_ReplyTo)),
            Child, MakeAddressField(&data->GUI.ST_REPLYTO, GetStr(MSG_CO_ReplyTo), MSG_HELP_CO_ST_REPLYTO, ABM_TO, -1, FALSE),
            Child, Label2(GetStr(MSG_CO_Organization)),
            Child, data->GUI.ST_ORGAN = MakeString(SIZE_DEFAULT,GetStr(MSG_CO_Organization)),
            Child, Label2(GetStr(MSG_CO_ExtraHeaders)),
            Child, data->GUI.ST_EXTHEADER = MakeString(SIZE_LARGE,GetStr(MSG_CO_ExtraHeaders)),
         End,
         Child, VGroup, GroupFrameT(GetStr(MSG_CO_MessageBody)),
            Child, ColGroup(2),
               Child, Label2(GetStr(MSG_CO_Welcome)),
               Child, data->GUI.ST_HELLOTEXT = MakeString(SIZE_INTRO,GetStr(MSG_CO_Welcome)),
               Child, Label2(GetStr(MSG_CO_Greetings)),
               Child, data->GUI.ST_BYETEXT = MakeString(SIZE_INTRO,GetStr(MSG_CO_Greetings)),
            End,
            Child, MakeCheckGroup((Object **)&data->GUI.CH_WARNSUBJECT, GetStr(MSG_CO_WARNSUBJECT)),
         End,
         Child, VGroup, GroupFrameT(GetStr(MSG_CO_Editor)),
            Child, ColGroup(3),
               Child, Label2(GetStr(MSG_CO_WordWrap)),
               Child, data->GUI.ST_EDWRAP = MakeInteger(3, GetStr(MSG_CO_WordWrap)),
               Child, data->GUI.CY_EDWRAP = MakeCycle(wrapmode, ""),
               Child, Label2(GetStr(MSG_CO_ExternalEditor)),
               Child, PopaslObject,
                  MUIA_Popasl_Type     ,ASL_FileRequest,
                  MUIA_Popstring_String,data->GUI.ST_EDITOR = MakeString(SIZE_PATHFILE,GetStr(MSG_CO_ExternalEditor)),
                  MUIA_Popstring_Button,PopButton(MUII_PopFile),
               End,
               Child, MakeCheckGroup((Object **)&data->GUI.CH_LAUNCH, GetStr(MSG_CO_Launch)),
               Child, Label2(GetStr(MSG_CO_NB_EMAILCACHE)),
               Child, HGroup,
                Child, data->GUI.NB_EMAILCACHE = NumericbuttonObject,
                  MUIA_CycleChain,      TRUE,
                  MUIA_Numeric_Min,     0,
                  MUIA_Numeric_Max,     100,
                  MUIA_Numeric_Format,  GetStr(MSG_CO_NB_EMAILCACHEFMT),
                End,
                Child, HSpace(0),
               End,
               Child, HSpace(0),
               Child, Label2(GetStr(MSG_CO_NB_AUTOSAVE)),
               Child, HGroup,
                Child, data->GUI.NB_AUTOSAVE = NumericbuttonObject,
                  MUIA_CycleChain,      TRUE,
                  MUIA_Numeric_Min,     0,
                  MUIA_Numeric_Max,     30,
                  MUIA_Numeric_Format,  GetStr(MSG_CO_NB_AUTOSAVEFMT),
                End,
                Child, HSpace(0),
               End,
               Child, HSpace(0),
            End,
         End,
         Child, HVSpace,
      End))
   {
      SetHelp(data->GUI.ST_REPLYTO      ,MSG_HELP_CO_ST_REPLYTO     );
      SetHelp(data->GUI.ST_ORGAN        ,MSG_HELP_CO_ST_ORGAN       );
      SetHelp(data->GUI.ST_EXTHEADER    ,MSG_HELP_CO_ST_EXTHEADER   );
      SetHelp(data->GUI.ST_HELLOTEXT    ,MSG_HELP_CO_ST_HELLOTEXT   );
      SetHelp(data->GUI.ST_BYETEXT      ,MSG_HELP_CO_ST_BYETEXT     );
      SetHelp(data->GUI.CH_WARNSUBJECT  ,MSG_HELP_CO_CH_WARNSUBJECT );
      SetHelp(data->GUI.ST_EDWRAP       ,MSG_HELP_CO_ST_EDWRAP      );
      SetHelp(data->GUI.CY_EDWRAP       ,MSG_HELP_CO_CY_EDWRAP      );
      SetHelp(data->GUI.ST_EDITOR       ,MSG_HELP_CO_ST_EDITOR      );
      SetHelp(data->GUI.CH_LAUNCH       ,MSG_HELP_CO_CH_LAUNCH      );
      SetHelp(data->GUI.NB_EMAILCACHE   ,MSG_HELP_CO_NB_EMAILCACHE  );
      SetHelp(data->GUI.NB_AUTOSAVE     ,MSG_HELP_CO_NB_AUTOSAVE    );
   }

   return grp;
}

///
/// CO_Page6  (Reply/Forward)
Object *CO_Page6(struct CO_ClassData *data)
{
   Object *grp;
   if ((grp = VGroup,
         MUIA_HelpNode, "CO06",

         Child, HGroup,
            Child, MakeImageObject("config_answer_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_REPLY_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_REPLY_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, ColGroup(2), GroupFrameT(GetStr(MSG_CO_Forwarding)),
            Child, Label2(GetStr(MSG_CO_FwdInit)),
            Child, MakeVarPop(&data->GUI.ST_FWDSTART, VPM_FORWARD, SIZE_INTRO, GetStr(MSG_CO_FwdInit)),
            Child, Label2(GetStr(MSG_CO_FwdFinish)),
            Child, MakeVarPop(&data->GUI.ST_FWDEND, VPM_FORWARD, SIZE_INTRO, GetStr(MSG_CO_FwdFinish)),
         End,
         Child, VGroup, GroupFrameT(GetStr(MSG_CO_Replying)),
            Child, ColGroup(2),
               Child, Label2(GetStr(MSG_CO_RepInit)),
               Child, MakePhraseGroup(&data->GUI.ST_REPLYHI, &data->GUI.ST_REPLYTEXT, &data->GUI.ST_REPLYBYE, GetStr(MSG_CO_RepInit), GetStr(MSG_HELP_CO_ST_REPLYTEXT)),
               Child, Label2(GetStr(MSG_CO_AltRepInit)),
               Child, MakePhraseGroup(&data->GUI.ST_AREPLYHI, &data->GUI.ST_AREPLYTEXT, &data->GUI.ST_AREPLYBYE, GetStr(MSG_CO_AltRepInit), GetStr(MSG_HELP_CO_ST_AREPLYTEXT)),
               Child, Label2(GetStr(MSG_CO_AltRepPat)),
               Child, data->GUI.ST_AREPLYPAT = MakeString(SIZE_PATTERN, GetStr(MSG_CO_AltRepPat)),
               Child, Label2(GetStr(MSG_CO_MLRepInit)),
               Child, MakePhraseGroup(&data->GUI.ST_MREPLYHI, &data->GUI.ST_MREPLYTEXT, &data->GUI.ST_MREPLYBYE, GetStr(MSG_CO_MLRepInit), GetStr(MSG_HELP_CO_ST_MREPLYTEXT)),
               Child, Label2(GetStr(MSG_CO_QuoteMail)),
               Child, MakeVarPop(&data->GUI.ST_REPLYCHAR, VPM_QUOTE, SIZE_SMALL, GetStr(MSG_CO_QuoteMail)),
               Child, Label2(GetStr(MSG_CO_AltQuote)),
               Child, data->GUI.ST_ALTQUOTECHAR = MakeString(SIZE_SMALL, GetStr(MSG_CO_AltQuote)),
            End,
            Child, ColGroup(2),
               Child, MakeCheckGroup((Object **)&data->GUI.CH_QUOTE, GetStr(MSG_CO_DoQuote)),
               Child, MakeCheckGroup((Object **)&data->GUI.CH_QUOTEEMPTY, GetStr(MSG_CO_QuoteEmpty)),
               Child, MakeCheckGroup((Object **)&data->GUI.CH_COMPADDR, GetStr(MSG_CO_VerifyAddress)),
               Child, MakeCheckGroup((Object **)&data->GUI.CH_STRIPSIG, GetStr(MSG_CO_StripSignature)),
            End,
         End,
         Child, HVSpace,
      End))
   {
      SetHelp(data->GUI.ST_FWDSTART    ,MSG_HELP_CO_ST_FWDSTART  );
      SetHelp(data->GUI.ST_FWDEND      ,MSG_HELP_CO_ST_FWDEND    );
      SetHelp(data->GUI.ST_AREPLYPAT   ,MSG_HELP_CO_ST_AREPLYPAT );
      SetHelp(data->GUI.CH_QUOTE       ,MSG_HELP_CO_CH_QUOTE     );
      SetHelp(data->GUI.ST_REPLYCHAR   ,MSG_HELP_CO_ST_REPLYCHAR );
      SetHelp(data->GUI.ST_ALTQUOTECHAR,MSG_HELP_CO_ST_ALTQUOTECHAR);
      SetHelp(data->GUI.CH_QUOTEEMPTY  ,MSG_HELP_CO_CH_QUOTEEMPTY);
      SetHelp(data->GUI.CH_COMPADDR    ,MSG_HELP_CO_CH_COMPADDR  );
      SetHelp(data->GUI.CH_STRIPSIG    ,MSG_HELP_CO_CH_STRIPSIG  );
      DoMethod(data->GUI.CH_QUOTE      ,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,data->GUI.ST_REPLYCHAR,3,MUIM_Set,MUIA_Disabled,MUIV_NotTriggerValue);
      DoMethod(data->GUI.CH_QUOTE      ,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,data->GUI.CH_QUOTEEMPTY,3,MUIM_Set,MUIA_Disabled,MUIV_NotTriggerValue);
      DoMethod(data->GUI.CH_QUOTE      ,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,data->GUI.CH_STRIPSIG,3,MUIM_Set,MUIA_Disabled,MUIV_NotTriggerValue);
   }
   return grp;
}

///
/// CO_Page7  (Signature)
Object *CO_Page7(struct CO_ClassData *data)
{
   static char *signat[4];
   Object *grp;
   Object *slider = ScrollbarObject, End;

   signat[0] = GetStr(MSG_CO_DefSig);
   signat[1] = GetStr(MSG_CO_AltSig1);
   signat[2] = GetStr(MSG_CO_AltSig2);
   signat[3] = NULL;

   if ((grp = VGroup,
         MUIA_HelpNode, "CO07",

         Child, HGroup,
            Child, MakeImageObject("config_signature_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_SIGNATURE_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_SIGNATURE_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, VGroup, GroupFrameT(GetStr(MSG_CO_Signature)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_USESIG, GetStr(MSG_CO_UseSig)),
            Child, HGroup,
               Child, data->GUI.CY_SIGNAT = MakeCycle(signat,""),
               Child, data->GUI.BT_SIGEDIT = MakeButton(GetStr(MSG_CO_EditSig)),
            End,
            Child, HGroup,
               MUIA_Group_Spacing, 0,
               Child, data->GUI.TE_SIGEDIT = MailTextEditObject,
                  InputListFrame,
                  MUIA_CycleChain,            TRUE,
                  MUIA_TextEditor_FixedFont,  TRUE,
                  MUIA_TextEditor_ExportHook, MUIV_TextEditor_ExportHook_EMail,
                  MUIA_TextEditor_Slider,     slider,
               End,
               Child, slider,
            End,
            Child, ColGroup(2),
               Child, data->GUI.BT_INSTAG = MakeButton(GetStr(MSG_CO_InsertTag)),
               Child, data->GUI.BT_INSENV = MakeButton(GetStr(MSG_CO_InsertENV)),
            End,
         End,
         Child, VGroup, GroupFrameT(GetStr(MSG_CO_Taglines)),
            Child, ColGroup(2),
               Child, Label2(GetStr(MSG_CO_TaglineFile)),
               Child, PopaslObject,
                  MUIA_Popasl_Type     ,ASL_FileRequest,
                  MUIA_Popstring_String,data->GUI.ST_TAGFILE = MakeString(SIZE_PATHFILE,GetStr(MSG_CO_TaglineFile)),
                  MUIA_Popstring_Button,PopButton(MUII_PopFile),
               End,
               Child, Label2(GetStr(MSG_CO_TaglineSep)),
               Child, data->GUI.ST_TAGSEP = MakeString(SIZE_SMALL,GetStr(MSG_CO_TaglineSep)),
            End,
         End,
      End))
   {
      SetHelp(data->GUI.CY_SIGNAT,  MSG_HELP_CO_CY_SIGNAT   );
      SetHelp(data->GUI.BT_SIGEDIT, MSG_HELP_CO_BT_EDITSIG  );
      SetHelp(data->GUI.BT_INSTAG,  MSG_HELP_CO_BT_INSTAG   );
      SetHelp(data->GUI.BT_INSENV,  MSG_HELP_CO_BT_INSENV   );
      SetHelp(data->GUI.ST_TAGFILE, MSG_HELP_CO_ST_TAGFILE  );
      SetHelp(data->GUI.ST_TAGSEP,  MSG_HELP_CO_ST_TAGSEP   );

      DoMethod(data->GUI.BT_INSTAG, MUIM_Notify, MUIA_Pressed,      FALSE         , data->GUI.TE_SIGEDIT   , 2, MUIM_TextEditor_InsertText, "%t\n");
      DoMethod(data->GUI.BT_INSENV, MUIM_Notify, MUIA_Pressed,      FALSE         , data->GUI.TE_SIGEDIT   , 2, MUIM_TextEditor_InsertText, "%e");
      DoMethod(data->GUI.CY_SIGNAT, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, MUIV_Notify_Application, 3, MUIM_CallHook, &CO_EditSignatHook, FALSE);
      DoMethod(data->GUI.BT_SIGEDIT,MUIM_Notify, MUIA_Pressed,      FALSE         , MUIV_Notify_Application, 3, MUIM_CallHook, &CO_EditSignatHook, TRUE);
      DoMethod(data->GUI.CH_USESIG, MUIM_Notify, MUIA_Selected,     MUIV_EveryTime, MUIV_Notify_Application, 3, MUIM_CallHook, &CO_SwitchSignatHook, MUIV_NotTriggerValue);
   }

   return grp;
}

///
/// CO_Page8  (Lists)
Object *CO_Page8(struct CO_ClassData *data)
{
   Object *grp;
   static char *sizef[6];
   static char *infob[5];

   sizef[0] = GetStr(MSG_CO_SIZEFORMAT01);
   sizef[1] = GetStr(MSG_CO_SIZEFORMAT02);
   sizef[2] = GetStr(MSG_CO_SIZEFORMAT03);
   sizef[3] = GetStr(MSG_CO_SIZEFORMAT04);
   sizef[4] = GetStr(MSG_CO_SIZEFORMAT05);
   sizef[5] = NULL;

   infob[0] = GetStr(MSG_CO_INFOBARPOS01);
   infob[1] = GetStr(MSG_CO_INFOBARPOS02);
   infob[2] = GetStr(MSG_CO_INFOBARPOS03);
   infob[3] = GetStr(MSG_CO_INFOBARPOS04);
   infob[4] = NULL;

   if ((grp =  VGroup,
         MUIA_HelpNode, "CO08",

         Child, HGroup,
            Child, MakeImageObject("config_lists_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_LISTS_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_LISTS_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, ScrollgroupObject,
           MUIA_Scrollgroup_FreeHoriz, FALSE,
           MUIA_Scrollgroup_Contents, VGroupV,

           Child, HGroup, GroupFrameT(GetStr(MSG_CO_FIELDLISTCFG)),
             Child, ColGroup(2), GroupFrame,
               MUIA_FrameTitle, GetStr(MSG_FolderList),
               MUIA_ShortHelp, GetStr(MSG_HELP_CO_CG_FO),
               Child, MakeStaticCheck(),
               Child, LLabel(GetStr(MSG_Folder)),
               Child, data->GUI.CH_FCOLS[1] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Total)),
               Child, data->GUI.CH_FCOLS[2] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Unread)),
               Child, data->GUI.CH_FCOLS[3] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_New)),
               Child, data->GUI.CH_FCOLS[4] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Size)),
               Child, data->GUI.CH_FCNTMENU = MakeCheck(""),
               Child, LLabel(GetStr(MSG_CO_CONTEXTMENU)),
             End,
             Child, HSpace(0),
             Child, ColGroup(2), GroupFrame,
               MUIA_FrameTitle, GetStr(MSG_MessageList),
               MUIA_ShortHelp, GetStr(MSG_HELP_CO_CG_MA),
               Child, MakeStaticCheck(),
               Child, LLabel(GetStr(MSG_Status)),
               Child, data->GUI.CH_MCOLS[1] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_SenderRecpt)),
               Child, data->GUI.CH_MCOLS[2] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_ReturnAddress)),
               Child, data->GUI.CH_MCOLS[3] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Subject)),
               Child, data->GUI.CH_MCOLS[4] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_MessageDate)),
               Child, data->GUI.CH_MCOLS[5] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Size)),
               Child, data->GUI.CH_MCOLS[6] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Filename)),
               Child, data->GUI.CH_MCOLS[7] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_CO_DATE_SNTRCVD)),
               Child, data->GUI.CH_MCNTMENU = MakeCheck(""),
               Child, LLabel(GetStr(MSG_CO_CONTEXTMENU)),
             End,
           End,
           Child, VGroup, GroupFrameT(GetStr(MSG_CO_GENLISTCFG)),
             Child, MakeCheckGroup((Object **)&data->GUI.CH_FIXFLIST,  GetStr(MSG_CO_FixedFontList)),
             Child, MakeCheckGroup((Object **)&data->GUI.CH_BEAT,      GetStr(MSG_CO_SwatchBeat)),
             Child, MakeCheckGroup((Object **)&data->GUI.CH_QUICKSEARCHBAR, GetStr(MSG_CO_QUICKSEARCHBAR)),
             Child, HGroup,
               Child, Label1(GetStr(MSG_CO_SIZEFORMAT)),
               Child, data->GUI.CY_SIZE = MakeCycle(sizef, GetStr(MSG_CO_SIZEFORMAT)),
             End,
           End,
           Child, VGroup,
             GroupFrameT(GetStr(MSG_CO_INFOBAR)),
             MUIA_Group_Columns,  2,
             Child, Label1(GetStr(MSG_CO_INFOBARPOS)),
             Child, data->GUI.CY_INFOBAR = MakeCycle(infob, GetStr(MSG_CO_INFOBARPOS)),
             Child, Label2(GetStr(MSG_CO_FOLDERLABEL)),
             Child, MakeVarPop(&data->GUI.ST_INFOBARTXT, VPM_MAILSTATS, SIZE_DEFAULT, GetStr(MSG_CO_FOLDERLABEL)),
           End,
           Child, HVSpace,
           End,
         End,
      End))
   {
      SetHelp(data->GUI.CH_FIXFLIST,MSG_HELP_CO_CH_FIXFLIST);
      SetHelp(data->GUI.CH_BEAT    ,MSG_HELP_CO_CH_BEAT);
      SetHelp(data->GUI.CH_QUICKSEARCHBAR,  MSG_HELP_CO_CH_QUICKSEARCHBAR);
      SetHelp(data->GUI.CY_INFOBAR ,MSG_HELP_CO_CH_INFOBAR);
      SetHelp(data->GUI.ST_INFOBARTXT,MSG_HELP_CO_ST_INFOBARTXT);
      SetHelp(data->GUI.CY_SIZE    ,MSG_HELP_CO_CY_SIZE);
      SetHelp(data->GUI.CH_FCNTMENU,MSG_HELP_CO_CONTEXTMENU);
      SetHelp(data->GUI.CH_MCNTMENU,MSG_HELP_CO_CONTEXTMENU);
   }
   return grp;
}

///
/// CO_Page9  (Security)
Object *CO_Page9(struct CO_ClassData *data)
{
   Object *grp;
   static char *logfmode[4];
   logfmode[0] = GetStr(MSG_CO_LogNone);
   logfmode[1] = GetStr(MSG_CO_LogNormal);
   logfmode[2] = GetStr(MSG_CO_LogVerbose);
   logfmode[3] = NULL;
   if ((grp = VGroup,
         MUIA_HelpNode, "CO09",

         Child, HGroup,
            Child, MakeImageObject("config_security_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_SECURITY_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_SECURITY_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, ColGroup(2), GroupFrameT("PGP"),
            Child, Label2(GetStr(MSG_CO_PGPExe)),
            Child, PopaslObject,
               MUIA_Popasl_Type     ,ASL_FileRequest,
               MUIA_Popstring_String,data->GUI.ST_PGPCMD= MakeString(SIZE_PATHFILE,GetStr(MSG_CO_PGPExe)),
               MUIA_Popstring_Button,PopButton(MUII_PopDrawer),
               ASLFR_DrawersOnly, TRUE,
            End,
            Child, Label2(GetStr(MSG_CO_PGPKey)),
            Child, HGroup,
               Child, MakePGPKeyList(&(data->GUI.ST_MYPGPID), TRUE, GetStr(MSG_CO_PGPKey)),
               Child, HSpace(8),
               Child, data->GUI.CH_ENCSELF = MakeCheck(GetStr(MSG_CO_EncryptToSelf)),
               Child, Label1(GetStr(MSG_CO_EncryptToSelf)),
            End,
         End,
         Child, ColGroup(2), GroupFrameT(GetStr(MSG_CO_AnonMail)),
            Child, Label2(GetStr(MSG_CO_ReMailer)),
            Child, data->GUI.ST_REMAILER = MakeString(SIZE_ADDRESS,GetStr(MSG_CO_ReMailer)),
            Child, Label2(GetStr(MSG_CO_ReMailerLine)),
            Child, data->GUI.ST_FIRSTLINE = MakeString(SIZE_INTRO,GetStr(MSG_CO_ReMailerLine)),
         End,
         Child, VGroup, GroupFrameT(GetStr(MSG_CO_Logfiles)),
            Child, HGroup,
               Child, Label2(GetStr(MSG_CO_LogPath)),
               Child, PopaslObject,
                  MUIA_Popasl_Type     ,ASL_FileRequest,
                  MUIA_Popstring_String,data->GUI.ST_LOGFILE = MakeString(SIZE_PATH,GetStr(MSG_CO_LogPath)),
                  MUIA_Popstring_Button,PopButton(MUII_PopDrawer),
                  ASLFR_DrawersOnly, TRUE,
               End,
            End,
            Child, ColGroup(5),
               Child, data->GUI.CH_SPLITLOG = MakeCheck(GetStr(MSG_CO_LogSplit)),
               Child, LLabel1(GetStr(MSG_CO_LogSplit)),
               Child, HSpace(0),
               Child, Label1(GetStr(MSG_CO_LogMode)),
               Child, data->GUI.CY_LOGMODE = MakeCycle(logfmode,GetStr(MSG_CO_LogMode)),
               Child, data->GUI.CH_LOGALL = MakeCheck(GetStr(MSG_CO_LogAllEvents)),
               Child, LLabel1(GetStr(MSG_CO_LogAllEvents)),
               Child, HSpace(0),
               Child, HSpace(0),
               Child, HSpace(0),
            End,
         End,
         Child, HVSpace,
      End))
   {
      SetHelp(data->GUI.ST_PGPCMD    ,MSG_HELP_CO_ST_PGPCMD   );
      SetHelp(data->GUI.ST_MYPGPID   ,MSG_HELP_CO_ST_MYPGPID  );
      SetHelp(data->GUI.CH_ENCSELF   ,MSG_HELP_CO_CH_ENCSELF  );
      SetHelp(data->GUI.ST_REMAILER  ,MSG_HELP_CO_ST_REMAILER );
      SetHelp(data->GUI.ST_FIRSTLINE ,MSG_HELP_CO_ST_FIRSTLINE);
      SetHelp(data->GUI.ST_LOGFILE   ,MSG_HELP_CO_ST_LOGFILE  );
      SetHelp(data->GUI.CH_SPLITLOG  ,MSG_HELP_CO_CH_SPLITLOG );
      SetHelp(data->GUI.CY_LOGMODE   ,MSG_HELP_CO_CY_LOGMODE  );
      SetHelp(data->GUI.CH_LOGALL    ,MSG_HELP_CO_CH_LOGALL   );
   }
   return grp;
}

///
/// CO_Page10 (Start/Quit)
Object *CO_Page10(struct CO_ClassData *data)
{
   Object *grp;
   if ((grp = VGroup,
         MUIA_HelpNode, "CO10",

         Child, HGroup,
            Child, MakeImageObject("config_start_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_STARTUP_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_STARTUP_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, VGroup, GroupFrameT(GetStr(MSG_CO_OnStartup)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_LOADALL, GetStr(MSG_CO_LoadAll)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_MARKNEW, GetStr(MSG_CO_MarkNew)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_DELETESTART, GetStr(MSG_CO_DeleteOld)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_REMOVESTART, GetStr(MSG_CO_RemoveDel)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_CHECKBD, GetStr(MSG_CO_CheckDOB)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_SENDSTART, GetStr(MSG_CO_SendStart)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_POPSTART, GetStr(MSG_CO_PopStart)),
         End,
         Child, VGroup, GroupFrameT(GetStr(MSG_CO_OnTermination)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_SENDQUIT, GetStr(MSG_CO_SendStart)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_DELETEQUIT, GetStr(MSG_CO_DeleteOld)),
            Child, MakeCheckGroup((Object **)&data->GUI.CH_REMOVEQUIT, GetStr(MSG_CO_RemoveDel)),
         End,
         Child, HVSpace,
      End))
   {
      SetHelp(data->GUI.CH_LOADALL    ,MSG_HELP_CO_CH_LOADALL   );
      SetHelp(data->GUI.CH_MARKNEW    ,MSG_HELP_CO_CH_MARKNEW   );
      SetHelp(data->GUI.CH_DELETESTART,MSG_HELP_CO_CH_DELETEOLD );
      SetHelp(data->GUI.CH_REMOVESTART,MSG_HELP_CO_CH_REMOVEDEL );
      SetHelp(data->GUI.CH_SENDSTART  ,MSG_HELP_CO_CH_SEND      );
      SetHelp(data->GUI.CH_POPSTART   ,MSG_HELP_CO_CH_POPSTART  );
      SetHelp(data->GUI.CH_CHECKBD    ,MSG_HELP_CO_CH_CHECKBD   );
      SetHelp(data->GUI.CH_SENDQUIT   ,MSG_HELP_CO_CH_SEND      );
      SetHelp(data->GUI.CH_DELETEQUIT ,MSG_HELP_CO_CH_DELETEOLD );
      SetHelp(data->GUI.CH_REMOVEQUIT ,MSG_HELP_CO_CH_REMOVEDEL );
   }
   return grp;
}

///
/// CO_Page11 (MIME)
Object *CO_Page11(struct CO_ClassData *data)
{
   Object *grp;

   if ((grp = VGroup,
         MUIA_HelpNode, "CO11",

         Child, HGroup,
            Child, MakeImageObject("config_mime_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_MIME_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_MIME_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, VGroup, GroupFrameT(GetStr(MSG_CO_MimeViewers)),
            Child, HGroup,
               Child, ListviewObject,
                  MUIA_Weight, 60,
                  MUIA_CycleChain, 1,
                  MUIA_Listview_List, data->GUI.LV_MIME = ListObject,
                    InputListFrame,
                    MUIA_List_DisplayHook, &MimeTypeDisplayHook,
                  End,
               End,
               Child, VGroup,
                  Child, data->GUI.GR_MIME = ColGroup(2),
                     Child, Label2(GetStr(MSG_CO_MimeType)),
                     Child, MakeMimeTypePop(&data->GUI.ST_CTYPE, GetStr(MSG_CO_MimeType)),
                     Child, Label2(GetStr(MSG_CO_Extension)),
                     Child, data->GUI.ST_EXTENS = BetterStringObject,
                        StringFrame,
                        MUIA_String_Accept,      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ",
                        MUIA_String_MaxLen,      SIZE_NAME,
                        MUIA_ControlChar,        ShortCut(GetStr(MSG_CO_Extension)),
                        MUIA_String_AdvanceOnCR, TRUE,
                        MUIA_CycleChain,         TRUE,
                     End,
                     Child, Label2(GetStr(MSG_CO_MIME_DESCRIPTION)),
                     Child, data->GUI.ST_DESCRIPTION = MakeString(SIZE_DEFAULT, GetStr(MSG_CO_MIME_DESCRIPTION)),
                     Child, Label2(GetStr(MSG_CO_MimeCmd)),
                     Child, PopaslObject,
                        MUIA_Popstring_String, data->GUI.ST_COMMAND = MakeString(SIZE_COMMAND,GetStr(MSG_CO_MimeCmd)),
                        MUIA_Popstring_Button, PopButton(MUII_PopFile),
                     End,
                  End,
                  Child, VSpace(0),
                  Child, ColGroup(2),
                     Child, data->GUI.BT_MADD = MakeButton(GetStr(MSG_Add)),
                     Child, data->GUI.BT_MDEL = MakeButton(GetStr(MSG_Del)),
                  End,
               End,
            End,
            Child, HGroup,
               Child, Label2(GetStr(MSG_CO_DefaultViewer)),
               Child, PopaslObject,
                  MUIA_Popstring_String, data->GUI.ST_DEFVIEWER = MakeString(SIZE_COMMAND,GetStr(MSG_CO_DefaultViewer)),
                  MUIA_Popstring_Button, PopButton(MUII_PopFile),
               End,
            End,
         End,
         Child, ColGroup(2), GroupFrameT(GetStr(MSG_CO_Paths)),
            Child, Label2(GetStr(MSG_CO_Detach)),
            Child, PopaslObject,
               MUIA_Popasl_Type     ,ASL_FileRequest,
               MUIA_Popstring_String,data->GUI.ST_DETACHDIR = MakeString(SIZE_PATH,GetStr(MSG_CO_Detach)),
               MUIA_Popstring_Button,PopButton(MUII_PopDrawer),
               ASLFR_DrawersOnly, TRUE,
            End,
            Child, Label2(GetStr(MSG_CO_Attach)),
            Child, PopaslObject,
               MUIA_Popasl_Type     ,ASL_FileRequest,
               MUIA_Popstring_String,data->GUI.ST_ATTACHDIR = MakeString(SIZE_PATH,GetStr(MSG_CO_Attach)),
               MUIA_Popstring_Button,PopButton(MUII_PopDrawer),
               ASLFR_DrawersOnly, TRUE,
            End,
         End,
      End))
   {
      SetHelp(data->GUI.ST_CTYPE     ,MSG_HELP_CO_ST_CTYPE     );
      SetHelp(data->GUI.ST_EXTENS    ,MSG_HELP_CO_ST_EXTENS    );
      SetHelp(data->GUI.ST_COMMAND   ,MSG_HELP_CO_ST_COMMAND   );
      SetHelp(data->GUI.BT_MADD      ,MSG_HELP_CO_BT_MADD      );
      SetHelp(data->GUI.BT_MDEL      ,MSG_HELP_CO_BT_MDEL      );
      SetHelp(data->GUI.ST_DEFVIEWER ,MSG_HELP_CO_ST_DEFVIEWER );
      SetHelp(data->GUI.ST_DETACHDIR ,MSG_HELP_CO_ST_DETACHDIR );
      SetHelp(data->GUI.ST_ATTACHDIR ,MSG_HELP_CO_ST_ATTACHDIR );

      DoMethod(grp,MUIM_MultiSet,MUIA_Disabled,TRUE,data->GUI.GR_MIME,data->GUI.BT_MDEL,NULL);
      DoMethod(data->GUI.LV_MIME     ,MUIM_Notify,MUIA_List_Active    ,MUIV_EveryTime, MUIV_Notify_Application, 2, MUIM_CallHook, &GetMimeTypeEntryHook);
      DoMethod(data->GUI.ST_CTYPE    ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime, MUIV_Notify_Application, 2, MUIM_CallHook, &PutMimeTypeEntryHook);
      DoMethod(data->GUI.ST_EXTENS   ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime, MUIV_Notify_Application, 2, MUIM_CallHook, &PutMimeTypeEntryHook);
      DoMethod(data->GUI.ST_COMMAND  ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime, MUIV_Notify_Application, 2, MUIM_CallHook, &PutMimeTypeEntryHook);
      DoMethod(data->GUI.ST_DESCRIPTION,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime, MUIV_Notify_Application, 2, MUIM_CallHook, &PutMimeTypeEntryHook);
      DoMethod(data->GUI.ST_DEFVIEWER,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime, MUIV_Notify_Application, 2, MUIM_CallHook, &PutMimeTypeEntryHook);
      DoMethod(data->GUI.BT_MADD     ,MUIM_Notify,MUIA_Pressed        ,FALSE         , MUIV_Notify_Application, 2, MUIM_CallHook, &AddMimeTypeHook);
      DoMethod(data->GUI.BT_MDEL     ,MUIM_Notify,MUIA_Pressed        ,FALSE         , MUIV_Notify_Application, 2, MUIM_CallHook, &DelMimeTypeHook);
   }
   return grp;
}

///
/// CO_Page12 (Address book)
Object *CO_Page12(struct CO_ClassData *data)
{
   Object *grp;
   static char *atab[6];

   atab[0] = GetStr(MSG_CO_ATABnever);
   atab[1] = GetStr(MSG_CO_ATABinfoask);
   atab[2] = GetStr(MSG_CO_ATABask);
   atab[3] = GetStr(MSG_CO_ATABinfo);
   atab[4] = GetStr(MSG_CO_ATABalways);
   atab[5] = NULL;

   if ((grp = VGroup,
         MUIA_HelpNode, "CO12",

         Child, HGroup,
            Child, MakeImageObject("config_abook_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_ABOOK_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_ABOOK_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, HGroup, GroupFrameT(GetStr(MSG_Columns)),
            Child, HVSpace,
            Child, ColGroup(4),
               MUIA_ShortHelp, GetStr(MSG_HELP_CO_CG_AB),
               Child, data->GUI.CH_ACOLS[1] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Realname)),
               Child, data->GUI.CH_ACOLS[2] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Description)),
               Child, data->GUI.CH_ACOLS[3] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Email)),
               Child, data->GUI.CH_ACOLS[4] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Street)),
               Child, data->GUI.CH_ACOLS[5] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_City)),
               Child, data->GUI.CH_ACOLS[6] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Country)),
               Child, data->GUI.CH_ACOLS[7] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_Phone)),
               Child, data->GUI.CH_ACOLS[8] = MakeCheck(""),
               Child, LLabel(GetStr(MSG_DOB)),
            End,
            Child, HVSpace,
         End,
         Child, ColGroup(2), GroupFrameT(GetStr(MSG_CO_InfoExc)),
            Child, Label1(GetStr(MSG_CO_AddInfo)),
            Child, HGroup,
               Child, data->GUI.CH_ADDINFO = MakeCheck(GetStr(MSG_CO_AddInfo)),
               Child, HSpace(0),
            End,
            Child, Label1(GetStr(MSG_CO_AddToAddrbook)),
            Child, data->GUI.CY_ATAB = MakeCycle(atab, GetStr(MSG_CO_AddToAddrbook)),
            Child, Label2(GetStr(MSG_CO_NewGroup)),
            Child, data->GUI.ST_NEWGROUP = MakeString(SIZE_NAME,GetStr(MSG_CO_NewGroup)),
            Child, Label2(GetStr(MSG_CO_Gallery)),
            Child, PopaslObject,
               MUIA_Popasl_Type     ,ASL_FileRequest,
               MUIA_Popstring_String,data->GUI.ST_GALLDIR = MakeString(SIZE_PATH,GetStr(MSG_CO_Gallery)),
               MUIA_Popstring_Button,PopButton(MUII_PopDrawer),
               ASLFR_DrawersOnly, TRUE,
            End,
            Child, Label2(GetStr(MSG_CO_MyURL)),
            Child, data->GUI.ST_PHOTOURL = MakeString(SIZE_URL,GetStr(MSG_CO_MyURL)),
            Child, Label2(GetStr(MSG_CO_ProxyServer)),
            Child, data->GUI.ST_PROXY = MakeString(SIZE_HOST,GetStr(MSG_CO_ProxyServer)),
         End,
         Child, HVSpace,
      End))
   {
      SetHelp(data->GUI.ST_GALLDIR   ,MSG_HELP_CO_ST_GALLDIR   );
      SetHelp(data->GUI.ST_PHOTOURL  ,MSG_HELP_CO_ST_PHOTOURL  );
      SetHelp(data->GUI.ST_PROXY     ,MSG_HELP_CO_ST_PROXY     );
      SetHelp(data->GUI.ST_NEWGROUP  ,MSG_HELP_CO_ST_NEWGROUP  );
      SetHelp(data->GUI.CY_ATAB      ,MSG_HELP_CO_CY_ATAB      );
      SetHelp(data->GUI.CH_ADDINFO   ,MSG_HELP_WR_CH_ADDINFO   );

      DoMethod(data->GUI.CY_ATAB, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, data->GUI.ST_NEWGROUP, 3, MUIM_Set, MUIA_Disabled, MUIV_NotTriggerValue);
   }
   return grp;
}

///
/// CO_Page13 (Scripts)
Object *CO_Page13(struct CO_ClassData *data)
{
   Object *grp;
   static char *stype[3] = {
     "ARexx", "AmigaDOS", NULL
   };

   if ((grp = VGroup,
         MUIA_HelpNode, "CO13",

         Child, HGroup,
            Child, MakeImageObject("config_scripts_big"),
            Child, VGroup,
              Child, TextObject,
                MUIA_Text_PreParse, "\033b",
                MUIA_Text_Contents, GetStr(MSG_CO_SCRIPTS_TITLE),
                MUIA_Weight,        100,
              End,
              Child, TextObject,
                MUIA_Text_PreParse, "",
                MUIA_Text_Contents, GetStr(MSG_CO_SCRIPTS_SUMMARY),
                MUIA_Font,          MUIV_Font_Tiny,
                MUIA_Weight,        100,
              End,
           End,
         End,

         Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
         End,

         Child, VGroup,
            Child, data->GUI.LV_REXX = ListviewObject,
               MUIA_CycleChain, 1,
               MUIA_Listview_List, ListObject,
                  InputListFrame,
                  MUIA_List_DisplayHook, &CO_LV_RxDspHook,
               End,
            End,
            Child, ColGroup(2),
               Child, Label2(GetStr(MSG_CO_Name)),
               Child, HGroup,
                  Child, data->GUI.ST_RXNAME = MakeString(SIZE_NAME,GetStr(MSG_CO_Name)),
                  Child, data->GUI.CY_ISADOS = MakeCycle(stype,""),
               End,
               Child, Label2(GetStr(MSG_CO_Script)),
               Child, PopaslObject,
                  MUIA_Popasl_Type     ,ASL_FileRequest,
                  MUIA_Popstring_String,data->GUI.ST_SCRIPT = MakeString(SIZE_PATHFILE,GetStr(MSG_CO_Script)),
                  MUIA_Popstring_Button,PopButton(MUII_PopFile),
               End,
            End,
            Child, HGroup,
               Child, MakeCheckGroup((Object **)&data->GUI.CH_CONSOLE, GetStr(MSG_CO_OpenConsole)),
               Child, MakeCheckGroup((Object **)&data->GUI.CH_WAITTERM, GetStr(MSG_CO_WaitTerm)),
            End,
         End,
      End))
   {
      int i;
      for (i = 1; i <= MAXRX; i++) DoMethod(data->GUI.LV_REXX, MUIM_List_InsertSingle, i, MUIV_List_Insert_Bottom);
      SetHelp(data->GUI.ST_RXNAME    ,MSG_HELP_CO_ST_RXNAME    );
      SetHelp(data->GUI.ST_SCRIPT    ,MSG_HELP_CO_ST_SCRIPT    );
      SetHelp(data->GUI.CY_ISADOS    ,MSG_HELP_CO_CY_ISADOS    );
      SetHelp(data->GUI.CH_CONSOLE   ,MSG_HELP_CO_CH_CONSOLE   );
      SetHelp(data->GUI.CH_WAITTERM  ,MSG_HELP_CO_CH_WAITTERM  );
      DoMethod(data->GUI.LV_REXX     ,MUIM_Notify,MUIA_List_Active    ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook,&CO_GetRXEntryHook,0);
      DoMethod(data->GUI.ST_RXNAME   ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook,&CO_PutRXEntryHook,0);
      DoMethod(data->GUI.ST_SCRIPT   ,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook,&CO_PutRXEntryHook,0);
      DoMethod(data->GUI.CY_ISADOS   ,MUIM_Notify,MUIA_Cycle_Active   ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook,&CO_PutRXEntryHook,0);
      DoMethod(data->GUI.CH_CONSOLE  ,MUIM_Notify,MUIA_Selected       ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook,&CO_PutRXEntryHook,0);
      DoMethod(data->GUI.CH_WAITTERM ,MUIM_Notify,MUIA_Selected       ,MUIV_EveryTime,MUIV_Notify_Application,3,MUIM_CallHook,&CO_PutRXEntryHook,0);
   }
   return grp;
}

///
/// CO_Page14 (Mixed)
Object *CO_Page14(struct CO_ClassData *data)
{
   Object *grp;
   static char *empty[5];

   empty[0] = empty[1] = empty[2] = empty[3] = "";
   empty[4] = NULL;

   if((grp = VGroup,
       MUIA_HelpNode, "CO14",

       Child, HGroup,
          Child, MakeImageObject("config_misc_big"),
          Child, VGroup,
            Child, TextObject,
              MUIA_Text_PreParse, "\033b",
              MUIA_Text_Contents, GetStr(MSG_CO_MIXED_TITLE),
              MUIA_Weight,        100,
            End,
            Child, TextObject,
              MUIA_Text_PreParse, "",
              MUIA_Text_Contents, GetStr(MSG_CO_MIXED_SUMMARY),
              MUIA_Font,          MUIV_Font_Tiny,
              MUIA_Weight,        100,
            End,
         End,
       End,

       Child, RectangleObject,
          MUIA_Rectangle_HBar, TRUE,
          MUIA_FixHeight,      4,
       End,

       Child, ScrollgroupObject,
         MUIA_Scrollgroup_FreeHoriz, FALSE,
         MUIA_Scrollgroup_Contents, VGroupV,
            Child, ColGroup(2), GroupFrameT(GetStr(MSG_CO_Paths)),
               Child, Label2(GetStr(MSG_CO_TempDir)),
               Child, PopaslObject,
                  MUIA_Popasl_Type     ,ASL_FileRequest,
                  MUIA_Popstring_String,data->GUI.ST_TEMPDIR = MakeString(SIZE_PATH,GetStr(MSG_CO_TempDir)),
                  MUIA_Popstring_Button,PopButton(MUII_PopDrawer),
                  ASLFR_DrawersOnly, TRUE,
               End,
            End,
            Child, VGroup, GroupFrameT(GetStr(MSG_CO_AppIcon)),
               Child, ColGroup(2),
                  Child, data->GUI.CH_WBAPPICON = MakeCheck(GetStr(MSG_CO_WBAPPICON)),
                  Child, LLabel1(GetStr(MSG_CO_WBAPPICON)),
                  Child, HSpace(0),
                  Child, ColGroup(2),
                    Child, Label2(GetStr(MSG_CO_PositionX)),
                    Child, HGroup,
                      Child, data->GUI.ST_APPX = MakeInteger(4, "_X"),
                      Child, Label2("_Y"),
                      Child, data->GUI.ST_APPY = MakeInteger(4, "_Y"),
                    End,
                    Child, Label2(GetStr(MSG_CO_APPICONTEXT)),
                    Child, MakeVarPop(&data->GUI.ST_APPICON, VPM_MAILSTATS, SIZE_DEFAULT/2, GetStr(MSG_CO_APPICONTEXT)),
                  End,
               End,
               Child, MakeCheckGroup((Object **)&data->GUI.CH_DOCKYICON, GetStr(MSG_CO_DOCKYICON)),
               Child, MakeCheckGroup((Object **)&data->GUI.CH_CLGADGET, GetStr(MSG_CO_CloseGadget)),
            End,
            Child, VGroup, GroupFrameT(GetStr(MSG_CO_SaveDelete)),
               Child, HGroup,
                  Child, data->GUI.CH_CONFIRM = MakeCheck(GetStr(MSG_CO_ConfirmDelPart1)),
                  Child, Label2(GetStr(MSG_CO_ConfirmDelPart1)),
                  Child, data->GUI.NB_CONFIRMDEL = MakeNumeric(1,50,FALSE),
                  Child, Label2(GetStr(MSG_CO_ConfirmDelPart2)),
                  Child, HSpace(0),
               End,
               Child, MakeCheckGroup((Object **)&data->GUI.CH_REMOVE, GetStr(MSG_CO_Remove)),
               Child, MakeCheckGroup((Object **)&data->GUI.CH_SAVESENT, GetStr(MSG_CO_SaveSent)),
            End,
           Child, VGroup, GroupFrameT(GetStr(MSG_CO_MDN)),
               Child, ColGroup(6),
                  Child, LLabel(GetStr(MSG_Display)),
                  Child, LLabel(GetStr(MSG_Process)),
                  Child, LLabel(GetStr(MSG_CO_Del)),
                  Child, LLabel(GetStr(MSG_Filter)),
                  Child, HSpace(0),
                  Child, HSpace(0),
                  Child, data->GUI.RA_MDN_DISP = RadioObject, MUIA_Radio_Entries, empty, MUIA_CycleChain, TRUE, End,
                  Child, data->GUI.RA_MDN_PROC = RadioObject, MUIA_Radio_Entries, empty, MUIA_CycleChain, TRUE, End,
                  Child, data->GUI.RA_MDN_DELE = RadioObject, MUIA_Radio_Entries, empty, MUIA_CycleChain, TRUE, End,
                  Child, data->GUI.RA_MDN_RULE = RadioObject, MUIA_Radio_Entries, empty, MUIA_CycleChain, TRUE, End,
                  Child, VGroup,
                    Child, LLabel(GetStr(MSG_CO_DispIgnore)),
                    Child, LLabel(GetStr(MSG_CO_DispDeny)),
                    Child, LLabel(GetStr(MSG_CO_DispAsk)),
                    Child, LLabel(GetStr(MSG_CO_DispAccept)),
                  End,
                  Child, HSpace(0),
               End,
               Child, MakeCheckGroup((Object **)&data->GUI.CH_SEND_MDN, GetStr(MSG_CO_SendAtOnce)),
            End,
            Child, HGroup, GroupFrameT(GetStr(MSG_CO_XPK)),
               Child, ColGroup(5),
                  Child, Label1(GetStr(MSG_CO_XPKPack)),
                  Child, MakeXPKPop(&data->GUI.TX_PACKER, TRUE, FALSE),
                  Child, data->GUI.NB_PACKER = MakeNumeric(0,100,TRUE),
                  Child, HSpace(8),
                  Child, HGroup,
                     Child, LLabel(GetStr(MSG_CO_Archiver)),
                     Child, HSpace(0),
                  End,
                  Child, Label1(GetStr(MSG_CO_XPKPackEnc)),
                  Child, MakeXPKPop(&data->GUI.TX_ENCPACK, TRUE, TRUE),
                  Child, data->GUI.NB_ENCPACK = MakeNumeric(0,100,TRUE),
                  Child, HSpace(8),
                  Child, MakeVarPop(&data->GUI.ST_ARCHIVER, VPM_ARCHIVE, SIZE_COMMAND, GetStr(MSG_CO_Archiver)),
               End,
            End,
            Child, HVSpace,
            End,
         End,
      End))
   {
      SetHelp(data->GUI.ST_TEMPDIR   ,MSG_HELP_CO_ST_TEMPDIR   );
      SetHelp(data->GUI.CH_WBAPPICON ,MSG_HELP_CO_CH_WBAPPICON );
      SetHelp(data->GUI.ST_APPX      ,MSG_HELP_CO_ST_APP       );
      SetHelp(data->GUI.ST_APPY      ,MSG_HELP_CO_ST_APP       );
      SetHelp(data->GUI.CH_DOCKYICON ,MSG_HELP_CO_CH_DOCKYICON );
      SetHelp(data->GUI.CH_CLGADGET  ,MSG_HELP_CO_CH_CLGADGET  );
      SetHelp(data->GUI.CH_CONFIRM   ,MSG_HELP_CO_CH_CONFIRM   );
      SetHelp(data->GUI.NB_CONFIRMDEL,MSG_HELP_CO_NB_CONFIRMDEL);
      SetHelp(data->GUI.CH_REMOVE    ,MSG_HELP_CO_CH_REMOVE    );
      SetHelp(data->GUI.CH_SAVESENT  ,MSG_HELP_CO_CH_SAVESENT  );
      SetHelp(data->GUI.RA_MDN_DISP  ,MSG_HELP_CO_RA_MDN_DISP  );
      SetHelp(data->GUI.RA_MDN_PROC  ,MSG_HELP_CO_RA_MDN_PROC  );
      SetHelp(data->GUI.RA_MDN_DELE  ,MSG_HELP_CO_RA_MDN_DELE  );
      SetHelp(data->GUI.RA_MDN_RULE  ,MSG_HELP_CO_RA_MDN_RULE  );
      SetHelp(data->GUI.CH_SEND_MDN  ,MSG_HELP_CO_CH_SEND_MDN  );
      SetHelp(data->GUI.TX_ENCPACK   ,MSG_HELP_CO_TX_ENCPACK   );
      SetHelp(data->GUI.TX_PACKER    ,MSG_HELP_CO_TX_PACKER    );
      SetHelp(data->GUI.NB_ENCPACK   ,MSG_HELP_CO_NB_ENCPACK   );
      SetHelp(data->GUI.NB_PACKER    ,MSG_HELP_CO_NB_ENCPACK   );
      SetHelp(data->GUI.ST_ARCHIVER  ,MSG_HELP_CO_ST_ARCHIVER  );
      SetHelp(data->GUI.ST_APPICON   ,MSG_HELP_CO_ST_APPICON   );

      DoMethod(grp, MUIM_MultiSet, MUIA_Disabled, TRUE, data->GUI.ST_APPX, data->GUI.ST_APPY, data->GUI.ST_APPICON, NULL);
      DoMethod(data->GUI.CH_WBAPPICON, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, MUIV_Notify_Application, 7, MUIM_MultiSet, MUIA_Disabled, MUIV_NotTriggerValue, data->GUI.ST_APPX, data->GUI.ST_APPY, data->GUI.ST_APPICON, NULL);

      #if defined(__amigaos4__)
      if(G->applicationID == 0)
      #endif
        set(data->GUI.CH_DOCKYICON, MUIA_Disabled, TRUE);
   }
   return grp;
}
///
/// CO_Page15 (Update)
Object *CO_Page15(struct CO_ClassData *data)
{
  Object *grp;
  static char *updateInterval[4];

  updateInterval[0] = GetStr(MSG_CO_UPDATE_DAILY);
  updateInterval[1] = GetStr(MSG_CO_UPDATE_WEEKLY);
  updateInterval[2] = GetStr(MSG_CO_UPDATE_MONTHLY);
  updateInterval[3] = NULL;

  if((grp = VGroup,
      MUIA_HelpNode, "CO15",

      Child, HGroup,
        Child, MakeImageObject("config_update_big"),
        Child, VGroup,
          Child, TextObject,
            MUIA_Text_PreParse, "\033b",
            MUIA_Text_Contents, GetStr(MSG_CO_UPDATE_TITLE),
            MUIA_Weight,        100,
          End,
          Child, TextObject,
            MUIA_Text_PreParse, "",
            MUIA_Text_Contents, GetStr(MSG_CO_UPDATE_SUMMARY),
            MUIA_Font,          MUIV_Font_Tiny,
            MUIA_Weight,        100,
          End,
        End,
      End,

      Child, RectangleObject,
        MUIA_Rectangle_HBar, TRUE,
        MUIA_FixHeight,      4,
      End,

      Child, VGroup,
        Child, VGroup, GroupFrameT(GetStr(MSG_CO_SOFTWAREUPDATE)),
          Child, ColGroup(2),
            Child, data->GUI.CH_UPDATECHECK = MakeCheck(GetStr(MSG_CO_SEARCHFORUPDATES)),
            Child, HGroup,
              Child, LLabel1(GetStr(MSG_CO_SEARCHFORUPDATES)),
              Child, data->GUI.CY_UPDATEINTERVAL = MakeCycle(updateInterval, ""),
              Child, HVSpace,
            End,
            Child, HVSpace,
            Child, TextObject,
              MUIA_Text_Contents, GetStr(MSG_CO_SEARCHFORUPDATESINFO),
              MUIA_Font,          MUIV_Font_Tiny,
            End,
            Child, VSpace(10),
            Child, VSpace(10),
            Child, HVSpace,
            Child, HGroup,
              Child, data->GUI.BT_UPDATENOW = MakeButton(GetStr(MSG_CO_SEARCHNOW)),
              Child, HVSpace,
              Child, HVSpace,
            End,
          End,
          Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_FixHeight,      4,
          End,
          Child, ColGroup(2),
            Child, LLabel1(GetStr(MSG_CO_LASTSEARCH)),
            Child, data->GUI.TX_UPDATESTATUS = TextObject,
              MUIA_Text_Contents, GetStr(MSG_CO_LASTSTATUS_NOCHECK),
            End,
            Child, HSpace(1),
            Child, data->GUI.TX_UPDATEDATE = TextObject,
              MUIA_Text_Contents, "",
            End,
          End,
        End,
        Child, HVSpace,
      End,
    End))
  {
    nnset(data->GUI.CY_UPDATEINTERVAL, MUIA_Disabled, TRUE);

    DoMethod(data->GUI.CH_UPDATECHECK, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, data->GUI.CY_UPDATEINTERVAL, 3, MUIM_Set, MUIA_Disabled, MUIV_NotTriggerValue);
    DoMethod(data->GUI.BT_UPDATENOW,   MUIM_Notify, MUIA_Pressed,  FALSE, MUIV_Notify_Application, 2, MUIM_CallHook, &UpdateCheckHook);
  }

  return grp;
}
///
