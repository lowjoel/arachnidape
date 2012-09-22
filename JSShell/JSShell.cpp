// JSShell.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Handle.h"

namespace {
	struct ExecutionArguments
	{
		bool RunInteractively;
		std::vector<TCHAR*> FilesToExecute;
		std::vector<TCHAR*> ShellArguments;
	};

	struct CopyOutputArguments
	{
		const KernelHandle& Source;
		const KernelHandle& Destination;
	};

	unsigned int __stdcall CopyOutput(void* arg);
	void CopyOutput(const CopyOutputArguments& arg);

	void StartJavaScriptShell(const ExecutionArguments& arguments);

	/// Sends a command to the Shell using the given pipe and executes it.
	void SendJavaScriptShellCommand(const std::string& command, HANDLE pipe);

	/// Sends a command to the Shell using the given pipe, but does not
	/// flush the buffer.
	void StreamJavaScriptShellCommand(const std::string& command, HANDLE pipe)
	void LoadJavaScriptSources(const std::vector<TCHAR*>& files, HANDLE pipe);
}

int _tmain(int argc, _TCHAR* argv[])
{
	ExecutionArguments arguments;
	arguments.RunInteractively = false;

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
		TCHAR cmdLine[MAX_PATH];
		_tcscpy_s(cmdLine, _T("js -U -f Stub.js -i"));

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
			CopyOutputArguments stdInReaderArgs = { thisStdInRead, stdInWrite };
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
			//Do character conversions first.
			std::string fileName;
			{
				size_t charsConverted = 0;
				std::vector<char> chars;
				chars.resize(_tcslen(*i) * 2 + 1);

				int bytesWritten = 0;
				while (!(bytesWritten = WideCharToMultiByte(CP_UTF8, 0, *i, -1,
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
				for (int i = 0; i < chars.size(); ++i)
				{
					if (chars[i] == '\\')
					{
						chars.insert(chars.begin() + i, '\\');
						++i;
					}
				}

				fileName.assign(chars.begin(), chars.end() - 1);
			}

			std::ostringstream stream;
			
			stream << "load(\"" << fileName << "\");";
			SendJavaScriptShellCommand(stream.str(), pipe);
		}
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
		for ( ; ; )
		{
			char buffer[16384];
			DWORD read = 0;
			if (!ReadFile(arg.Source.get(), buffer, sizeof(buffer) / sizeof(buffer[0]),
				&read, nullptr))
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

			//Write the output to the destination.
			WriteFile(arg.Destination.get(), buffer, read, &read, nullptr);
			FlushFileBuffers(arg.Destination.get());
		}
	}
}