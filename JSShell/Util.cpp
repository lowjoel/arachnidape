#include "stdafx.h"
#include <string>

std::string wcs2utf8(const wchar_t* buffer)
{
	std::vector<char> chars;
	chars.resize(_tcslen(buffer) * 2 + 1);

	int bytesWritten = 0;
	while (!(bytesWritten = WideCharToMultiByte(CP_UTF8, 0, buffer, -1,
		&chars.front(), chars.size(), nullptr, nullptr)))
	{
		unsigned lastError = GetLastError();
		switch (lastError)
		{
		case ERROR_INSUFFICIENT_BUFFER:
			chars.resize(chars.size() * 2);
			break;
		default:
			throw lastError;
		}
	}
	chars.resize(bytesWritten);
				
	//Replace all \ with \\ 
	for (size_t i = 0; i < chars.size(); ++i)
	{
		if (chars[i] == '\\')
		{
			chars.insert(chars.begin() + i, '\\');
			++i;
		}
	}

	return std::string(chars.begin(), chars.end() - 1);
}
