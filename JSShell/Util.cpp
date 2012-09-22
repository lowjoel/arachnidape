#include "stdafx.h"
#include <string>

#include "Util.h"

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

unsigned int __stdcall CopyOutput(void* arg)
{
	CopyOutputArguments& argument =
		*reinterpret_cast<CopyOutputArguments*>(arg);

	CopyOutput(argument);
	return 0;
}

void CopyOutput(const CopyOutputArguments& arg)
{
	std::vector<char> buffer;
	for ( ; ; )
	{
		buffer.resize(16384);
		DWORD read = 0;
		if (!ReadFile(arg.Source.get(), &buffer.front(), buffer.size(), &read, nullptr))
		{
			unsigned lastError = GetLastError();
			switch (lastError)
			{
			case ERROR_INVALID_HANDLE:
			case ERROR_BROKEN_PIPE:
				return;
			}

			break;
		}

		//Allow filtering
		buffer.resize(read);
		if (arg.Filter)
			arg.Filter(buffer);

		//Write the output to the destination.
		WriteFile(arg.Destination.get(), &buffer.front(), buffer.size(), &read, nullptr);
		FlushFileBuffers(arg.Destination.get());
	}
}

