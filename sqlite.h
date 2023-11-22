#pragma once
#include <string>
#include "sqlite3430200/sqlite3.h";
#include "Handle.h"

#ifdef _DEBUG
	#include <atlbase.h>
	#define TRACE OutputDebugString
	#define VERIFY ASSERT
	#define VERIFY_(result, expression) ASSERT(result == expression)
#else
	#define TRACE(expression) (excpression)
	#define VERIFY(expression) (expression)
	#define VERIFY_(result, expression) (expression)
#endif

enum class SqlType
{
	Integer = SQLITE_INTEGER,
	Float = SQLITE_FLOAT,
	Text = SQLITE_TEXT,
	Blob = SQLITE_BLOB,
	Null = SQLITE_NULL
};

struct Exception
{
	int Result = 0;
	std::string Message;

	explicit Exception(sqlite3* connection) noexcept :
		Result(sqlite3_extended_errcode(connection)),
		Message(sqlite3_errmsg(connection))
	{
	}
};


class Connection
{
	struct ConnectionHandleTraits : HandleTraits<sqlite3*>
	{
		static void Close(Type value) noexcept
		{
			TRACE(L"Closing connection to database file\n");
			sqlite3_close(value);
		}
	};

	using ConnectionHandle = Handle<ConnectionHandleTraits>;
	ConnectionHandle m_handle;
	
	template <typename F, typename C>
	void InternalOpen(F open, C const* const filename)
	{
		Connection temp;
		if(SQLITE_OK != open(filename, temp.m_handle.Set()))
		{
			temp.ThrowLastError();
		}

		swap(m_handle, temp.m_handle);
	}

public:
	Connection() noexcept = default;

	template <typename C>
	Connection(C const * const filename)
	{
		Open(filename);
	}

	static Connection Memory()
	{
		return Connection(":memory:");
	}

	static Connection WideMemory()
	{
		return Connection(L":memory:");
	}

	explicit operator bool() const noexcept
	{
		return static_cast<bool>(m_handle);
	}

	sqlite3* GetAbi() const noexcept
	{
		return m_handle.Get();
	}

	__declspec(noreturn) void ThrowLastError() const
	{
		throw Exception(GetAbi());
	}
	
	void Open(char const * const filename)
	{
		InternalOpen(sqlite3_open, filename);
	}

	void Open(wchar_t const * const  filename)
	{
		InternalOpen(sqlite3_open16, filename);
	}

	long long RowId() const noexcept
	{
		return sqlite3_last_insert_rowid(GetAbi());
	}

	template <typename F>
	void Profile(F callback, void* const context = nullptr)
	{
		sqlite3_profile(GetAbi(), callback, context);
	}
};

class Backup
{
	struct BackupHandleTraits : HandleTraits<sqlite3_backup*>
	{
		static void Close(Type Value)
		{
			TRACE(L"Closing backup\n");
			sqlite3_backup_finish(Value);
		}
	};

	using BackupHandle = Handle<BackupHandleTraits>;
	BackupHandle m_handle;
	Connection const * m_destination = nullptr;

public:
	Backup(Connection const & destination, 
		Connection const & source, 
		char const* const destinationName = "main", 
		char const* const sourceName = "main") :
		m_handle(sqlite3_backup_init(destination.GetAbi(), destinationName, source.GetAbi(), sourceName)),
		m_destination(&destination)
	{
		if (!m_handle)
		{
			destination.ThrowLastError();
		}
	}

	sqlite3_backup* GetAbi() const noexcept
	{
		return m_handle.Get();
	}

	bool Step(int const pages = -1)
	{
		int const result = sqlite3_backup_step(GetAbi(), pages);

		if (result == SQLITE_OK) return true;
		if (result == SQLITE_DONE) return false;

		m_handle.Reset();
		m_destination->ThrowLastError();
	}
};

template <typename T>
struct Reader
{
	int GetInt(int const column = 0) const noexcept
	{
		return sqlite3_column_int(static_cast<T const*>(this)->GetAbi(), column);
	}

	char const * GetString(int const column = 0) const noexcept
	{
		return reinterpret_cast<char const *>(
			sqlite3_column_text(static_cast<T const*>(this)->GetAbi(), column));
	}

	wchar_t const* GetWideString(int const column = 0) const noexcept
	{
		return reinterpret_cast<wchar_t const*>(
			sqlite3_column_text16(static_cast<T const *>(this)->GetAbi(), column));
	}

	int GetStringLength(int const column = 0) const noexcept
	{
		return sqlite3_column_bytes(static_cast<T const*>(this)->GetAbi(), column);
	}

	int GetWideStringLength(int const column = 0) const noexcept
	{
		return sqlite3_column_bytes16(static_cast<T const*>(this)->GetAbi(), column) / sizeof(wchar_t);
	}

	SqlType GetType(int const column = 0) const noexcept
	{
		return static_cast<SqlType>(sqlite3_column_type(static_cast<T const*>(this)->GetAbi(), column));
	}
};

class Row : public Reader<Row>
{
	sqlite3_stmt* m_statement = nullptr;

public:
	sqlite3_stmt* GetAbi() const
	{
		return m_statement;
	}

	Row(sqlite3_stmt* const statement) noexcept :
		m_statement(statement)
	{}
};

class Statement : public Reader<Statement>
{
	struct StatementHandleTraits : HandleTraits<sqlite3_stmt*>
	{
		static void Close(Type value) noexcept
		{
			TRACE(L"Finalizing statement\n");
			sqlite3_finalize(value);
			//VERIFY_(SQLITE_OK, sqlite3_finalize(value));
		}
	};

	using StatementHandle = Handle<StatementHandleTraits>;
	StatementHandle m_handle;

	template <typename F, typename C, typename ... Values>
	void InternalPrepare(Connection const& connection, F prepare, C const* const text, Values&& ... values)
	{
		ASSERT(connection);

		if (SQLITE_OK != prepare(connection.GetAbi(), text, -1, m_handle.Set(), nullptr))
		{
			connection.ThrowLastError();
		}

		BindAll(std::forward<Values>(values) ...);
	}

	void InternalBind(int const index) const // terminating function for the 'variadic templated InternalBind' below
	{}

	template <typename First, typename ... Rest>
	void InternalBind(int const index, First&& first, Rest && ... rest) const
	{
		Bind(index, std::forward<First>(first));

		InternalBind(index + 1, std::forward<Rest>(rest) ...);
	}

public:
	Statement() noexcept = default;

	template <typename C, typename ... Values>
	Statement(Connection const& connection, C const* const text, Values&& ... values) noexcept
	{
		ASSERT(connection);
		Prepare(connection, text, std::forward<Values>(values) ...);
	}

	explicit operator bool() const
	{
		return static_cast<bool>(m_handle);
	}

	sqlite3_stmt* GetAbi() const noexcept
	{
		return m_handle.Get();
	}

	__declspec(noreturn) void ThrowLastError() const
	{
		throw Exception(sqlite3_db_handle(GetAbi()));
	}

	template <typename ... Values>
	void Prepare(Connection const& connection, char const* const text, Values&& ... values)
	{
		InternalPrepare(connection, sqlite3_prepare_v2, text, std::forward<Values>(values) ...);
	}

	template <typename ... Values>
	void Prepare(Connection const& connection, wchar_t const* const text, Values&& ... values)
	{
		InternalPrepare(connection, sqlite3_prepare16_v2, text, std::forward<Values>(values) ...);
	}

	bool Step() const
	{
		int const result = sqlite3_step(GetAbi());
		if (SQLITE_ROW == result) return true;
		if (SQLITE_DONE == result) return false;

		ThrowLastError();
	}

	const unsigned char* Text(int const column = 0) const noexcept
	{
		return sqlite3_column_text(GetAbi(), column);
	}

	void Execute() const
	{
		VERIFY(!Step());
	}

	void Bind(int const index, int const value) const
	{
		if (SQLITE_OK != sqlite3_bind_int(GetAbi(), index, value))
		{
			ThrowLastError();
		}
	}

	void Bind(int const index, char const* const value, int const size = -1) const
	{
		if (SQLITE_OK != sqlite3_bind_text(GetAbi(), index, value, size, SQLITE_STATIC))
		{
			ThrowLastError();
		}
	}

	void Bind(int const index, wchar_t const* const value, int const size = -1) const
	{
		if (SQLITE_OK != sqlite3_bind_text16(GetAbi(), index, value, size, SQLITE_STATIC))
		{
			ThrowLastError();
		}
	}

	void Bind(int const index, std::string const& value) const
	{
		Bind(index, value.c_str(), value.size());
	}

	void Bind(int const index, std::wstring const& value) const
	{
		Bind(index, value.c_str(), value.size() * sizeof(wchar_t));
	}

	void Bind(int const index, std::string&& value) const
	{
		if (SQLITE_OK != sqlite3_bind_text(GetAbi(), index, value.c_str(), value.size(), SQLITE_TRANSIENT))
		{
			ThrowLastError();
		}
	}

	void Bind(int const index, std::wstring&& value) const
	{
		if (SQLITE_OK != sqlite3_bind_text16(GetAbi(), index, value.c_str(), value.size() * sizeof(wchar_t), SQLITE_TRANSIENT))
		{
			ThrowLastError();
		}
	}

	template <typename ... Values>
	void BindAll(Values && ... values) const
	{
		InternalBind(1, std::forward<Values>(values) ...);
	}

	template<typename ... Values>
	void ResetAndBind(Values && ... values) const
	{
		if (SQLITE_OK != sqlite3_reset(GetAbi()))
		{
			ThrowLastError();
		}
		BindAll(std::forward<Values>(values) ...);
	}
};

class RowIterator
{
	Statement const* m_statement = nullptr;

public:
	RowIterator() noexcept = default;

	RowIterator(Statement  const& statement) noexcept
	{
		if (statement.Step())
		{
			m_statement = &statement;
		}
	}

	RowIterator& operator ++() noexcept
	{
		if (!m_statement->Step())
		{
			m_statement = nullptr;
		}
		return *this;
	}

	bool operator ==(RowIterator const& other) const noexcept
	{
		return m_statement == other.m_statement;
	}

	bool operator !=(RowIterator const & other) const noexcept
	{
		return m_statement != other.m_statement;
	}

	Row operator *() const noexcept
	{
		return Row(m_statement->GetAbi());
	}
};

inline  RowIterator begin(Statement const& statement) 
{
	return RowIterator(statement);
}

inline RowIterator end(Statement const&) 
{
	return RowIterator();
}

template <typename C, typename ... Values>
void Execute(Connection const& connection, C const* const text, Values&& ... values) 
{
	Statement(connection, text, std::forward<Values>(values) ...).Execute();
}