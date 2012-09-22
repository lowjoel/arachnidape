// JSShell.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Handle.h"
#include "Util.h"

namespace {
	struct ExecutionArguments
	{
		bool RunInteractively;
		TCHAR BasePath[MAX_PATH];
		std::vector<TCHAR*> FilesToExecute;
		std::vector<TCHAR*> ShellArguments;
	};

	void StartJavaScriptShell(const ExecutionArguments& arguments);

	/// Sends a command to the Shell using the given pipe and executes it.
	void SendJavaScriptShellCommand(const std::string& command, HANDLE pipe);

	/// Sends a command to the Shell using the given pipe, but does not
	/// flush the buffer.
	void StreamJavaScriptShellCommand(const std::string& command, HANDLE pipe);

	void LoadJavaScriptSources(const std::vector<TCHAR*>& files, HANDLE pipe);

	void JavaScriptStdInFilter(std::vector<char>& buffer);
}

int _tmain(int argc, _TCHAR* argv[])
{
	ExecutionArguments arguments;
	arguments.RunInteractively = false;

	//First find our executable's directory. We will find Stub.js and
	//js.exe there
	{
		TCHAR drive[MAX_PATH];
		TCHAR directory[MAX_PATH];
		_tsplitpath_s(argv[0], drive, MAX_PATH, directory, MAX_PATH, nullptr, 0, nullptr, 0);

		_tcscpy_s(arguments.BasePath, drive);
		_tcscpy_s(arguments.BasePath + _tcslen(arguments.BasePath),
			MAX_PATH - _tcslen(arguments.BasePath), directory);
	}
	
	for (int i = 1; i < argc; ++i)
	{
		if (_tcscmp(_T("-f"), argv[i]) == 0 && i + 1 < argc)
		{
			arguments.FilesToExecute.push_back(argv[++i]);
		}
		else if (_tcscmp(_T("-i"), argv[i]) == 0)
		{
			arguments.RunInteractively = true;
		}
		else
		{
			arguments.ShellArguments.push_back(argv[i]);
		}
	}

	//One fix up: if we have no files to execute, we should run interactively.
	if (arguments.FilesToExecute.empty() && !arguments.RunInteractively)
		arguments.RunInteractively = true;

	StartJavaScriptShell(arguments);
}

namespace {

	void StartJavaScriptShell(const ExecutionArguments& arguments)
	{
		STARTUPINFO jsShellStartupInfo = { 0 };
		PROCESS_INFORMATION jsShellInfo = { 0 };

		//Create our command line.
		TCHAR cmdLine[MAX_PATH * 4];
		wsprintf(cmdLine, L"%sjs.exe -U -f %sStub.js -i", arguments.BasePath, arguments.BasePath);

		//Create our process creation information.
		jsShellStartupInfo.cb = sizeof(jsShellStartupInfo);
		jsShellStartupInfo.dwFlags = STARTF_USESTDHANDLES;

		//Create our redirected pipes
		KernelHandle stdInRead, stdInWrite;
		KernelHandle stdOutRead, stdOutWrite;
		KernelHandle stdErrRead, stdErrWrite;
		{
			HANDLE hStdInRead, hStdInWrite;
			HANDLE hStdOutRead, hStdOutWrite;
			HANDLE hStdErrRead, hStdErrWrite;

			//We need an inheritable security permission
			SECURITY_ATTRIBUTES saAttr; 
			saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
			saAttr.bInheritHandle = TRUE; 
			saAttr.lpSecurityDescriptor = NULL; 

			if (!CreatePipe(&hStdInRead, &hStdInWrite, &saAttr, 0))
				throw GetLastError();
			stdInRead = KernelHandle(hStdInRead);
			stdInWrite = KernelHandle(hStdInWrite);

			//Don't let the child process inherit the stdin write handle.
			SetHandleInformation(stdInWrite.get(), HANDLE_FLAG_INHERIT, 0);

			if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0))
				throw GetLastError();
			stdOutRead = KernelHandle(hStdOutRead);
			stdOutWrite = KernelHandle(hStdOutWrite);

			//Don't let the child process inherit the stdin write handle.
			SetHandleInformation(stdOutRead.get(), HANDLE_FLAG_INHERIT, 0);

			if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &saAttr, 0))
				throw GetLastError();
			stdErrRead = KernelHandle(hStdErrRead);
			stdErrWrite = KernelHandle(hStdErrWrite);

			//Don't let the child process inherit the stdin write handle.
			SetHandleInformation(stdErrRead.get(), HANDLE_FLAG_INHERIT, 0);
		}
	
		jsShellStartupInfo.hStdInput = stdInRead.get();
		jsShellStartupInfo.hStdOutput = stdOutWrite.get();
		jsShellStartupInfo.hStdError = stdErrWrite.get();
	
		//Create the process.
		KernelHandle jsShellProcess;
		KernelHandle jsShellThread;
		if (!CreateProcess(nullptr, cmdLine, nullptr, nullptr, true, 0,
			nullptr, nullptr, &jsShellStartupInfo, &jsShellInfo))
		{
			unsigned error = GetLastError();
			_tprintf_s(_T("Could not start Mozilla JavaScript shell. Is js.exe in ")
				_T("your path? [GetLastError()=%d]"), error);
			throw error;
		}

		//Assign the handle to auto-destruct
		jsShellProcess = KernelHandle(jsShellInfo.hProcess);
		jsShellThread = KernelHandle(jsShellInfo.hThread);

		//Create threads to handle stdout and stderr
		KernelHandle thisStdOutWrite = GetStdHandle(STD_OUTPUT_HANDLE);
		CopyOutputArguments stdOutReaderArgs = { stdOutRead, thisStdOutWrite };
		_beginthreadex(nullptr, 0, &CopyOutput, &stdOutReaderArgs, 0, nullptr);

		KernelHandle thisStdErrWrite = GetStdHandle(STD_ERROR_HANDLE);
		CopyOutputArguments stdErrReaderArgs = { stdErrRead, thisStdErrWrite };
		_beginthreadex(nullptr, 0, &CopyOutput, &stdErrReaderArgs, 0, nullptr);

		//Before we do stdin (for interactivity), we need to load all the files
		//the user specified in order.
		LoadJavaScriptSources(arguments.FilesToExecute, stdInWrite.get());
		
		//Then we handle the situation where we may need interactivity, or not.
		KernelHandle thisStdInRead = GetStdHandle(STD_INPUT_HANDLE);
		if (arguments.RunInteractively)
		{
			CopyOutputArguments stdInReaderArgs = { thisStdInRead, stdInWrite, JavaScriptStdInFilter };
			_beginthreadex(nullptr, 0, &CopyOutput, &stdInReaderArgs, 0, nullptr);
		}
		else
		{
			SendJavaScriptShellCommand("quit();", stdInWrite.get());
		}

		//Wait for the process to terminate
		WaitForSingleObject(jsShellProcess.get(), INFINITE);
	}

	void SendJavaScriptShellCommand(const std::string& command, HANDLE pipe)
	{
		//Wrap the command with a with(window) {}
		const std::string commandText = "with (window) {" + command + "}";

		//Send the command.
		StreamJavaScriptShellCommand(commandText, pipe);
		StreamJavaScriptShellCommand("\r\n", pipe);
		FlushFileBuffers(pipe);
	}

	void StreamJavaScriptShellCommand(const std::string& command, HANDLE pipe)
	{
		DWORD read = 0;
		WriteFile(pipe, command.c_str(), command.length(), &read, nullptr);
	}

	void LoadJavaScriptSources(const std::vector<TCHAR*>& files, HANDLE pipe)
	{
		for (std::vector<TCHAR*>::const_iterator i = files.begin();
			i != files.end(); ++i)
		{
			//Send the opening command
			StreamJavaScriptShellCommand("evaluate('with (window) {", pipe);

			KernelHandle fileHandle(CreateFile(*i, GENERIC_READ, 0, nullptr,
				OPEN_ALWAYS, 0, nullptr));

			//Send the file to the Shell.
			char buffer[65536];
			DWORD read = 0;
			std::vector<char> sendBuffer;
			while (ReadFile(fileHandle.get(), buffer, sizeof(buffer) / sizeof(buffer[0]),
				&read, nullptr))
			{
				if (!read)
					break;

				sendBuffer.reserve(read);

				for (size_t i = 0; i < read; ++i)
				{
					//Inject a \ before all \'s and quotes
					if (buffer[i] == '\'' || buffer[i] == '\\')
						sendBuffer.push_back('\\');

					//If it is a carriage return/newline, translate it to the JavaScript
					//equivalent
					else if (buffer[i] == '\r')
					{
						sendBuffer.push_back('\\');
						sendBuffer.push_back('r');
						continue;
					}
					else if (buffer[i] == '\n')
					{
						sendBuffer.push_back('\\');
						sendBuffer.push_back('n');
						continue;
					}

					sendBuffer.push_back(buffer[i]);
				}

				//Stream the command.
				std::string partialCommand(sendBuffer.begin(), sendBuffer.end());
				StreamJavaScriptShellCommand(partialCommand, pipe);
				sendBuffer.clear();
			}

			switch (GetLastError())
			{
			default:
				;
			}

			//Send closing command.
			StreamJavaScriptShellCommand("}', { fileName: \"" + wcs2utf8(*i) +
				"\", newContext: true });\r\n", pipe);
		}
	}

	__declspec(thread) bool InWith = false;
	void JavaScriptStdInFilter(std::vector<char>& buffer)
	{
		if (!InWith)
		{
			InWith = true;
			const char const Header[] = "with (window) {";
			buffer.insert(buffer.begin(), Header, Header + sizeof(Header) - 1);
		}

		const char* newline = nullptr;
		{
			char* bufferPtr = &buffer.front();
			newline = strstr(bufferPtr, "\r\n");
			if (!newline)
				newline = strchr(bufferPtr, '\n');
		}
		if (InWith && newline)
		{
			InWith = false;
			const char const Footer[] = "}";
			buffer.insert(buffer.begin() + (newline - &buffer.front()),
				Footer, Footer + sizeof(Footer) - 1);
		}
	}
}