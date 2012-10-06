#pragma once
#include <string>
#include "Handle.h"

std::string wcs2utf8(const wchar_t* buffer);

struct CopyOutputArguments
{
	const KernelHandle& Source;
	const KernelHandle& Destination;

	void (*Filter)(std::vector<char>& buffer);
	void (*PostFilter)();
};

void CopyOutput(const CopyOutputArguments& arg);
unsigned int __stdcall CopyOutput(void* arg);
