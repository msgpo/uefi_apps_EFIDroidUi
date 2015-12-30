#ifndef __LIBRARY_EFIDROID_H__
#define __LIBRARY_EFIDROID_H__

#include <Uefi/UefiBaseType.h>

#define MENU_SIGNATURE             SIGNATURE_32 ('m', 'e', 'n', 'u')
#define MENU_ENTRY_SIGNATURE       SIGNATURE_32 ('e', 'n', 't', 'r')

extern EFI_GUID gEFIDroidVariableGuid;

typedef struct _MENU_ENTRY MENU_ENTRY;
struct _MENU_ENTRY {
  UINTN           Signature;
  LIST_ENTRY      Link;

  CONST CHAR8* Description;
  EFI_STATUS (*Callback) (VOID*);
  VOID *Private;
  BOOLEAN ResetGop;
  BOOLEAN HideBootMessage;

  VOID (*FreeCallback)(struct _MENU_ENTRY* Entry);
};

typedef struct {
  UINTN           Signature;
  LIST_ENTRY      Head;
  UINTN           OptionNumber;
  UINTN           Selection;
} MENU_OPTION;

extern CONST CHAR8                 *gErrorStr;

VOID
EFIDroidEnterFrontPage (
  IN UINT16                 TimeoutDefault,
  IN BOOLEAN                ConnectAllHappened
  );

MENU_OPTION*
MenuCreate (
  VOID
);

VOID
MenuFree (
  MENU_OPTION *Menu
);

MENU_ENTRY*
MenuCreateEntry (
  VOID
);

VOID
MenuFreeEntry (
  MENU_ENTRY* Entry
);

VOID
MenuAddEntry (
  MENU_OPTION  *Menu,
  MENU_ENTRY   *Entry
);

VOID
MenuRemoveEntry (
  MENU_OPTION  *Menu,
  MENU_ENTRY   *Entry
);


MENU_ENTRY *
MenuGetEntryById (
  MENU_OPTION         *MenuOption,
  UINTN               MenuNumber
  );

VOID
SetActiveMenu(
  MENU_OPTION* Menu
);

MENU_OPTION*
GetActiveMenu(
  VOID
);

VOID
InvalidateActiveMenu(
  VOID
);

#endif
