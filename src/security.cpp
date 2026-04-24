#include "security.h"
#include <aclapi.h>
#include <vector>

PSECURITY_DESCRIPTOR CreateProtectedSD() {
    PACL pACL = NULL;
    PSECURITY_DESCRIPTOR pSD = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!pSD || !InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION)) return NULL;
    EXPLICIT_ACCESS ea[3]; ZeroMemory(ea, sizeof(ea));

    ea[0].grfAccessPermissions = PROCESS_TERMINATE; ea[0].grfAccessMode = DENY_ACCESS;
    ea[0].grfInheritance = NO_INHERITANCE; ea[0].Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP; ea[0].Trustee.ptstrName = (LPTSTR)"INTERACTIVE";

    ea[1].grfAccessPermissions = PROCESS_ALL_ACCESS; ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance = NO_INHERITANCE; ea[1].Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP; ea[1].Trustee.ptstrName = (LPTSTR)"SYSTEM";

    ea[2].grfAccessPermissions = PROCESS_ALL_ACCESS; ea[2].grfAccessMode = SET_ACCESS;
    ea[2].grfInheritance = NO_INHERITANCE; ea[2].Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea[2].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP; ea[2].Trustee.ptstrName = (LPTSTR)"Administrators";

    if (SetEntriesInAcl(3, ea, NULL, &pACL) != ERROR_SUCCESS) return NULL;
    SetSecurityDescriptorDacl(pSD, TRUE, pACL, FALSE);
    return pSD;
}

bool SetDirectoryPermissionsAdminOnly(const std::string& dirPath) {
    PACL pOldDACL = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;

    DWORD result = GetNamedSecurityInfoA(dirPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD);
    if (result != ERROR_SUCCESS) {
        if (pSD) LocalFree(pSD);
        return false;
    }

    PSID pAdminSid = NULL;
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSid)) {
        if (pSD) LocalFree(pSD);
        return false;
    }

    PSID pSystemSid = NULL;
    if (!AllocateAndInitializeSid(&SIDAuthNT, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pSystemSid)) {
        FreeSid(pAdminSid);
        if (pSD) LocalFree(pSD);
        return false;
    }

    EXPLICIT_ACCESSA ea[2];
    ZeroMemory(ea, sizeof(ea));

    ea[0].grfAccessPermissions = GENERIC_ALL;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[0].Trustee.ptstrName = (LPSTR)pAdminSid;

    ea[1].grfAccessPermissions = GENERIC_ALL;
    ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[1].Trustee.ptstrName = (LPSTR)pSystemSid;

    PACL pNewDACL = NULL;
    result = SetEntriesInAclA(2, ea, NULL, &pNewDACL);

    FreeSid(pAdminSid);
    FreeSid(pSystemSid);

    if (result != ERROR_SUCCESS) {
        if (pSD) LocalFree(pSD);
        return false;
    }

    result = SetNamedSecurityInfoA((LPSTR)dirPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL);

    LocalFree(pNewDACL);
    if (pSD) LocalFree(pSD);

    return (result == ERROR_SUCCESS);
}

bool ResetDirectoryPermissionsToInherited(const std::string& dirPath) {
    PACL pOldDACL = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;

    DWORD result = GetNamedSecurityInfoA(dirPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD);
    if (result != ERROR_SUCCESS) {
        if (pSD) LocalFree(pSD);
        return false;
    }

    ACL_SIZE_INFORMATION asi = {};
    if (!GetAclInformation(pOldDACL, &asi, sizeof(asi), AclSizeInformation)) {
        LocalFree(pSD);
        return false;
    }

    std::vector<EXPLICIT_ACCESSA> eaList;
    for (DWORD i = 0; i < asi.AceCount; i++) {
        LPVOID pAce = NULL;
        if (!GetAce(pOldDACL, i, &pAce)) continue;
        ACE_HEADER* pAceHeader = (ACE_HEADER*)pAce;
        if (pAceHeader->AceFlags & INHERITED_ACE) continue;

        EXPLICIT_ACCESSA ea = {};
        ea.grfAccessMode = REVOKE_ACCESS;
        ea.grfInheritance = NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;

        if (pAceHeader->AceType == ACCESS_ALLOWED_ACE_TYPE) {
            ACCESS_ALLOWED_ACE* pAllowedAce = (ACCESS_ALLOWED_ACE*)pAce;
            ea.Trustee.ptstrName = (LPSTR)(&pAllowedAce->SidStart);
        } else if (pAceHeader->AceType == ACCESS_DENIED_ACE_TYPE) {
            ACCESS_DENIED_ACE* pDeniedAce = (ACCESS_DENIED_ACE*)pAce;
            ea.Trustee.ptstrName = (LPSTR)(&pDeniedAce->SidStart);
        } else {
            continue;
        }

        eaList.push_back(ea);
    }

    PACL pNewDACL = NULL;
    if (eaList.empty()) {
        pNewDACL = pOldDACL;
    } else {
        result = SetEntriesInAclA(static_cast<ULONG>(eaList.size()), eaList.data(), pOldDACL, &pNewDACL);
        if (result != ERROR_SUCCESS) {
            LocalFree(pSD);
            return false;
        }
    }

    result = SetNamedSecurityInfoA(
        (LPSTR)dirPath.c_str(), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
        NULL, NULL, pNewDACL, NULL
    );

    if (pNewDACL != pOldDACL) LocalFree(pNewDACL);
    LocalFree(pSD);

    return (result == ERROR_SUCCESS);
}
