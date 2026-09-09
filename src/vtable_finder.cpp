#include "vtable_finder.h"

#include <cstring>
#include <cstdio>
#include <string>

#include <tier0/dbg.h>

#ifdef __linux__

#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>

struct SectionRange
{
	std::string name;
	uint8_t* base = nullptr;
	size_t size = 0;
};

struct ModuleSections
{
	uintptr_t loadBase = 0;
	std::vector<SectionRange> sections;
	const SectionRange* Get(const char* name) const
	{
		for (auto& s : sections)
			if (s.name == name)
				return &s;
		return nullptr;
	}
};

static bool ReadSections(const char* moduleSuffix, ModuleSections& out)
{
	struct CbData { const char* suffix; std::string path; uintptr_t base = 0; bool found = false; };
	CbData cb{moduleSuffix};

	dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void* dataPtr) -> int {
		CbData* d = reinterpret_cast<CbData*>(dataPtr);
		size_t nameLen = strlen(info->dlpi_name);
		size_t sufLen = strlen(d->suffix);
		if (nameLen >= sufLen && !strcmp(info->dlpi_name + nameLen - sufLen, d->suffix))
		{
			// Metamod's loader shim is also called libserver.so and lives
			// under addons/ — we want the real game binary.
			if (strstr(info->dlpi_name, "/addons/"))
				return 0;
			d->path = info->dlpi_name;
			d->base = info->dlpi_addr;
			d->found = true;
			return 1;
		}
		return 0;
	}, &cb);

	if (!cb.found)
	{
		Warning("[CR] vtable: module %s is not loaded\n", moduleSuffix);
		return false;
	}

	int fd = open(cb.path.c_str(), O_RDONLY);
	if (fd == -1)
		return false;

	struct stat st;
	if (fstat(fd, &st) != 0)
	{
		close(fd);
		return false;
	}

	void* map = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED)
		return false;

	auto* ehdr = static_cast<ElfW(Ehdr)*>(map);
	auto* shdrs = reinterpret_cast<ElfW(Shdr)*>(reinterpret_cast<uintptr_t>(ehdr) + ehdr->e_shoff);
	const char* strTab = reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(ehdr) + shdrs[ehdr->e_shstrndx].sh_offset);

	out.loadBase = cb.base;
	for (int i = 0; i < ehdr->e_shnum; i++)
	{
		ElfW(Shdr)* shdr = &shdrs[i];
		const char* name = strTab + shdr->sh_name;
		if (!*name || !(shdr->sh_flags & SHF_ALLOC))
			continue;
		SectionRange s;
		s.name = name;
		s.base = reinterpret_cast<uint8_t*>(cb.base + shdr->sh_addr);
		s.size = shdr->sh_size;
		out.sections.push_back(std::move(s));
	}

	munmap(map, st.st_size);
	return true;
}

static void* SearchBytes(const uint8_t* haystack, size_t haySize, const void* needle, size_t needleSize)
{
	if (!haystack || haySize < needleSize || !needleSize)
		return nullptr;
	const uint8_t* n = static_cast<const uint8_t*>(needle);
	for (size_t i = 0; i + needleSize <= haySize; i++)
	{
		if (haystack[i] == n[0] && !memcmp(haystack + i, n, needleSize))
			return const_cast<uint8_t*>(haystack + i);
	}
	return nullptr;
}

static void* FindBytes(const SectionRange* sec, const void* pattern, size_t len)
{
	if (!sec)
		return nullptr;
	return SearchBytes(sec->base, sec->size, pattern, len);
}

// Finds occurrences of an 8-byte pointer value in a section.
static void* FindPointer(const SectionRange* sec, void* value, void* startAfter = nullptr)
{
	if (!sec)
		return nullptr;
	uint8_t* from = sec->base;
	size_t size = sec->size;
	if (startAfter)
	{
		uint8_t* s = static_cast<uint8_t*>(startAfter) + sizeof(void*);
		if (s < sec->base || s >= sec->base + sec->size)
			return nullptr;
		size -= (s - sec->base);
		from = s;
	}
	return SearchBytes(from, size, &value, sizeof(void*));
}

void* FindVirtualTable(const char* moduleSuffix, const char* className)
{
	ModuleSections mod;
	if (!ReadSections(moduleSuffix, mod))
		return nullptr;

	// 1. Mangled type name string in .rodata: "<len><name>\0"
	char mangled[256];
	snprintf(mangled, sizeof(mangled), "%zu%s", strlen(className), className);

	void* typeNameStr = FindBytes(mod.Get(".rodata"), mangled, strlen(mangled) + 1);
	if (!typeNameStr)
	{
		Warning("[CR] vtable: type descriptor for %s not found\n", className);
		return nullptr;
	}

	// 2. type_info object references the name string (second field of typeinfo)
	void* typeNameSlot = FindPointer(mod.Get(".data.rel.ro"), typeNameStr);
	if (!typeNameSlot)
	{
		Warning("[CR] vtable: type_info for %s not found\n", className);
		return nullptr;
	}
	void* typeInfo = static_cast<uint8_t*>(typeNameSlot) - sizeof(void*);

	// 3. vtable layout: [offset-to-top][typeinfo*][vfunc0]...
	//    take the match whose offset-to-top is 0 (the primary vtable)
	for (const char* secName : {".data.rel.ro", ".data.rel.ro.local"})
	{
		const SectionRange* sec = mod.Get(secName);
		void* hit = nullptr;
		while ((hit = FindPointer(sec, typeInfo, hit)) != nullptr)
		{
			int64_t offsetToTop = *reinterpret_cast<int64_t*>(static_cast<uint8_t*>(hit) - sizeof(void*));
			if (offsetToTop == 0)
				return static_cast<uint8_t*>(hit) + sizeof(void*);
		}
	}

	Warning("[CR] vtable: vtable for %s not found\n", className);
	return nullptr;
}

static bool ParseHexNibble(char c, uint8_t& out)
{
	if (c >= '0' && c <= '9') { out = (uint8_t)(c - '0'); return true; }
	if (c >= 'A' && c <= 'F') { out = (uint8_t)(c - 'A' + 10); return true; }
	if (c >= 'a' && c <= 'f') { out = (uint8_t)(c - 'a' + 10); return true; }
	return false;
}

void* FindFunctionSignature(const char* moduleSuffix, const char* sectionName, const char* idaSig)
{
	if (!moduleSuffix || !sectionName || !idaSig || !*idaSig)
		return nullptr;

	std::vector<uint8_t> pattern;
	std::vector<uint8_t> mask; // 1 = must match, 0 = wildcard

	const char* p = idaSig;
	while (*p)
	{
		while (*p == ' ')
			p++;
		if (!*p)
			break;

		if (*p == '?')
		{
			pattern.push_back(0);
			mask.push_back(0);
			while (*p == '?')
				p++;
			continue;
		}

		if (!p[1])
			break;

		uint8_t hi = 0, lo = 0;
		if (!ParseHexNibble(p[0], hi) || !ParseHexNibble(p[1], lo))
			return nullptr;

		pattern.push_back((uint8_t)((hi << 4) | lo));
		mask.push_back(1);
		p += 2;
	}

	if (pattern.empty())
		return nullptr;

	ModuleSections mod;
	if (!ReadSections(moduleSuffix, mod))
		return nullptr;

	const SectionRange* sec = mod.Get(sectionName);
	if (!sec)
		return nullptr;

	for (size_t i = 0; i + pattern.size() <= sec->size; i++)
	{
		bool ok = true;
		for (size_t j = 0; j < pattern.size(); j++)
		{
			if (mask[j] && sec->base[i + j] != pattern[j])
			{
				ok = false;
				break;
			}
		}
		if (ok)
			return sec->base + i;
	}

	return nullptr;
}

#else // !__linux__

void* FindVirtualTable(const char*, const char* className)
{
	Warning("[CR] vtable lookup is only implemented for Linux (%s)\n", className);
	return nullptr;
}

void* FindFunctionSignature(const char*, const char*, const char*)
{
	return nullptr;
}

#endif
