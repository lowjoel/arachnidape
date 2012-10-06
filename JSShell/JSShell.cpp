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

	void JavaScriptStdOutFilter(std::vector<char>& buffer);
	void JavaScriptStdOutPostFilter();
	void JavaScriptStdErrFilter(std::vector<char>& buffer);
	void JavaScriptStdErrPostFilter();
	void JavaScriptStdInFilter(std::vector<char>& buffer);

	/// Filter state which will filter out all JavaScript shell prompts.
	bool SuppressShellPrompt = true;
	
	/// The input event object. This will be signaled only when the shell is ready
	/// to accept input.
	HANDLE InputEvent = nullptr;

	/// The output mutex. This will be signaled only when the shell is
	/// ready to accept output.
	CRITICAL_SECTION OutputEvent = { 0 };
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
		KernelHandle inputEvent(CreateEvent(nullptr, false, false, nullptr));
		InputEvent = inputEvent.get();
		InitializeCriticalSection(&OutputEvent);

		KernelHandle thisStdOutWrite = GetStdHandle(STD_OUTPUT_HANDLE);
		CopyOutputArguments stdOutReaderArgs = { stdOutRead, thisStdOutWrite, JavaScriptStdOutFilter, JavaScriptStdOutPostFilter };
		_beginthreadex(nullptr, 0, &CopyOutput, &stdOutReaderArgs, 0, nullptr);

		KernelHandle thisStdErrWrite = GetStdHandle(STD_ERROR_HANDLE);
		CopyOutputArguments stdErrReaderArgs = { stdErrRead, thisStdErrWrite, JavaScriptStdErrFilter, JavaScriptStdErrPostFilter };
		_beginthreadex(nullptr, 0, &CopyOutput, &stdErrReaderArgs, 0, nullptr);

		//Before we do stdin (for interactivity), we need to load all the files
		//the user specified in order.
		LoadJavaScriptSources(arguments.FilesToExecute, stdInWrite.get());

		//Then we handle the situation where we may need interactivity, or not.
		KernelHandle thisStdInRead = GetStdHandle(STD_INPUT_HANDLE);
		if (arguments.RunInteractively)
		{
			//We can now enable the js> prompt.
			WaitForSingleObject(InputEvent, INFINITE);
			SuppressShellPrompt = false;
			SendJavaScriptShellCommand("", stdInWrite.get());
	
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
			KernelHandle fileHandle(CreateFile(*i, GENERIC_READ, 0, nullptr,
				OPEN_EXISTING, 0, nullptr));
			if (!fileHandle)
			{
				unsigned lastError = GetLastError();
				switch (lastError)
				{
				case ERROR_FILE_NOT_FOUND:
					fwprintf(stderr, _T("can't open %s: No such file or directory\r\n"), *i);
					break;
				}

				continue;
			}

			//Wait for the shell to accept input.
			WaitForSingleObject(InputEvent, INFINITE);

			//Send the opening command
			StreamJavaScriptShellCommand("evaluate('with (window) {", pipe);

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
			StreamJavaScriptShellCommand("\\r\\n}', { fileName: \"" + wcs2utf8(*i) +
				"\", newContext: true }), undefined;\r\n", pipe);
		}
	}
	
	bool InCommandEntry = false;
	void JavaScriptStdOutFilter(std::vector<char>& buffer)
	{
		//Obtain a lock on stdout.
		EnterCriticalSection(&OutputEvent);

		char* bufferFront = &buffer.front();
		bool shellPromptOnly = buffer.size() == 4 && !memcmp(bufferFront, "js> ", 4);
		std::vector<char>::iterator shellPromptString = buffer.end();
		{
			std::vector<char> searchBuffer(buffer.begin(), buffer.end());
			searchBuffer.push_back('\0');
			char* str = &searchBuffer.front();

			//Try to find the last occurrance of \r\njs>
			while ((str = strstr(str, "\r\njs> ")) != nullptr)
			{
				size_t offset = str - &searchBuffer.front();
				if (offset > buffer.size())
					break;

				shellPromptString = buffer.begin() + offset;
				++str;
			}

			//See if it ends with the prompt.
			if (shellPromptString != buffer.end())
			{
				if (shellPromptString + 6 != buffer.end())
					shellPromptString = buffer.end();
			}
		}

		if (shellPromptOnly || shellPromptString != buffer.end())
		{
			InCommandEntry = true;
			SetEvent(InputEvent);

			if (SuppressShellPrompt)
			{
				if (shellPromptOnly)
					buffer.clear();
				else
					buffer.erase(shellPromptString, buffer.end());
			}
		}
		else
		{
			InCommandEntry = false;
		}
	}

	void JavaScriptStdOutPostFilter()
	{
		//Release the lock on stdout
		LeaveCriticalSection(&OutputEvent);
	}

	WORD consoleAttributes = 0;
	void JavaScriptStdErrFilter(std::vector<char>& buffer)
	{
		//Lock stdout
		EnterCriticalSection(&OutputEvent);

		CONSOLE_SCREEN_BUFFER_INFO info = {0};
		if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &info))
			consoleAttributes = info.wAttributes;

		SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
	}

	void JavaScriptStdErrPostFilter()
	{
		if (consoleAttributes)
		{
			SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE), consoleAttributes);
			consoleAttributes = 0;
		}

		//Release the lock on stdout
		LeaveCriticalSection(&OutputEvent);
	}

	void JavaScriptStdInFilter(std::vector<char>& buffer)
	{
		static bool InWith = false;
		if (!InWith && InCommandEntry)
		{
			InWith = true;
			const char Header[] = "with (window) {";
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
			InCommandEntry = false;
			const char Footer[] = "}";
			buffer.insert(buffer.begin() + (newline - &buffer.front()),
				Footer, Footer + sizeof(Footer) - 1);
		}
	}
}