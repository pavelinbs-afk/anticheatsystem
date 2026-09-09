#pragma once

#include <cstdint>

class CEntityInstance;

int32_t Schema_GetOffset(const char* className, const char* fieldName);

template <typename T>
T Schema_Get(void* pInstance, const char* className, const char* fieldName, T defaultValue = T())
{
	int32_t off = Schema_GetOffset(className, fieldName);
	if (off < 0 || !pInstance)
		return defaultValue;
	return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(pInstance) + off);
}

template <typename T>
bool Schema_SetNetworked(CEntityInstance* pEntity, const char* className, const char* fieldName, const T& value);

template <typename T>
bool Schema_Set(void* pInstance, const char* className, const char* fieldName, const T& value)
{
	int32_t off = Schema_GetOffset(className, fieldName);
	if (off < 0 || !pInstance)
		return false;
	*reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(pInstance) + off) = value;
	return true;
}

void Schema_NetworkStateChanged(CEntityInstance* pEntity, int32_t offset);

template <typename T>
bool Schema_SetNetworked(CEntityInstance* pEntity, const char* className, const char* fieldName, const T& value)
{
	int32_t off = Schema_GetOffset(className, fieldName);
	if (off < 0 || !pEntity)
		return false;
	T* pField = reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(pEntity) + off);
	if (*pField == value)
		return true;
	*pField = value;
	Schema_NetworkStateChanged(pEntity, off);
	return true;
}

struct SchemaField
{
	const char* cls;
	const char* field;
	int32_t off = -2;

	int32_t Offset()
	{
		if (off == -2)
			off = Schema_GetOffset(cls, field);
		return off;
	}

	template <typename T>
	T Get(void* pInstance, T defaultValue = T())
	{
		int32_t o = Offset();
		if (o < 0 || !pInstance)
			return defaultValue;
		return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(pInstance) + o);
	}

	template <typename T>
	bool SetNetworked(CEntityInstance* pEntity, const T& value)
	{
		int32_t o = Offset();
		if (o < 0 || !pEntity)
			return false;
		T* pField = reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(pEntity) + o);
		if (*pField == value)
			return true;
		*pField = value;
		Schema_NetworkStateChanged(pEntity, o);
		return true;
	}
};
