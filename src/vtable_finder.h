#pragma once

#include <cstddef>

void* FindVirtualTable(const char* moduleSuffix, const char* className);
void* FindFunctionSignature(const char* moduleSuffix, const char* sectionName, const char* idaSig);
