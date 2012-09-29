#pragma once

template<typename HandleType, typename HandleCloseFunctionType, HandleCloseFunctionType Close, HandleType InvalidHandle> class Handle
{
public:
	Handle()
		: Instance(nullptr)
	{
	}

	Handle(const HandleType& handle)
		: Instance(handle)
	{
	}

	~Handle()
	{
		Close(Instance);
	}

	Handle& operator=(Handle& rhs)
	{
		Instance = rhs.Instance;
		rhs.Instance = nullptr;
		return *this;
	}

	HandleType release()
	{
		HandleType handle = Instance;
		Instance = nullptr;
		return handle;
	}

	HandleType get() const
	{
		return Instance;
	}

	void reset(const HandleType& handle)
	{
		Close(Instance);
		Instance = handle;
	}

	/// Checks whether the current handle is valid.
	operator bool() const
	{
		return Instance != InvalidHandle;
	}

private:
	Handle(const Handle&);

private:
	HandleType Instance;
};

typedef Handle<HANDLE, BOOL (__stdcall*)(HANDLE), CloseHandle, INVALID_HANDLE_VALUE> KernelHandle;
