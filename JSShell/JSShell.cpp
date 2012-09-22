// JSShell.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Handle.h"

int _tmain(int argc, _TCHAR* argv[])
{
	STARTUPINFO jsShellStartupInfo = { 0 };
	PROCESS_INFORMATION jsShellInfo = { 0 };

	//Create our command line.
	TCHAR cmdLine[MAX_PATH];
	_tcscpy_s(cmdLine, _T("js -f Stub.js -f -"));

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
		stdErrRead = KernelHandle(hStdOutRead);
		stdErrWrite = KernelHandle(hStdOutWrite);

		//Don't let the child process inherit the stdin write handle.
		SetHandleInformation(stdErrRead.get(), HANDLE_FLAG_INHERIT, 0);
	}
	
	jsShellStartupInfo.hStdInput = stdInRead.get();
	jsShellStartupInfo.hStdOutput = stdOutWrite.get();
	jsShellStartupInfo.hStdError = stdErrWrite.get();
	
	KernelHandle jsShellProcess;
	KernelHandle jsShellThread;
	if (!CreateProcess(nullptr, cmdLine, nullptr, nullptr, true, 0,
		nullptr, nullptr, &jsShellStartupInfo, &jsShellInfo))
	{
		unsigned error = GetLastError();
		_tprintf_s(_T("Could not start Mozilla JavaScript shell. Is js.exe in your path? [GetLastError()=%d]"),
			error);
		return error;
	}

	//Create threads to handle stdout and stderr
	_beginthreadex(

	//Assign the handle to auto-destruct
	jsShellProcess = KernelHandle(jsShellInfo.hProcess);
	jsShellThread = KernelHandle(jsShellInfo.hThread);


	return 0;
}

