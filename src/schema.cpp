#include "schema.h"

#include <map>
#include <string>

#include <tier0/dbg.h>
#include <schemasystem/schemasystem.h>
#include <entity2/entityinstance.h>

#ifdef _WIN32
#define SERVER_MODULE "server.dll"
#else
#define SERVER_MODULE "libserver.so"
#endif

static int32_t ResolveOffset(const char* className, const char* fieldName)
{
	CSchemaSystemTypeScope* pScope = g_pSchemaSystem->FindTypeScopeForModule(SERVER_MODULE);
	if (!pScope)
	{
		Warning("[CR] schema: no type scope for %s\n", SERVER_MODULE);
		return -1;
	}

	SchemaClassInfoData_t* pClass = pScope->FindDeclaredClass(className).Get();
	if (!pClass)
	{
		Warning("[CR] schema: class %s not found\n", className);
		return -1;
	}

	for (SchemaClassInfoData_t* p = pClass; p; )
	{
		for (int i = 0; i < p->m_nFieldCount; i++)
		{
			SchemaClassFieldData_t& field = p->m_pFields[i];
			if (!strcmp(field.m_pszName, fieldName))
				return field.m_nSingleInheritanceOffset;
		}
		p = p->m_nBaseClassCount ? p->m_pBaseClasses[0].m_pClass : nullptr;
	}

	Warning("[CR] schema: field %s::%s not found\n", className, fieldName);
	return -1;
}

int32_t Schema_GetOffset(const char* className, const char* fieldName)
{
	static std::map<std::string, int32_t> s_cache;

	std::string key = std::string(className) + "::" + fieldName;
	auto it = s_cache.find(key);
	if (it != s_cache.end())
		return it->second;

	int32_t off = ResolveOffset(className, fieldName);
	s_cache[key] = off;
	return off;
}

void Schema_NetworkStateChanged(CEntityInstance* pEntity, int32_t offset)
{
	pEntity->NetworkStateChanged(NetworkStateChangedData((uint32)offset));
}
