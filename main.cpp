// Date: 23/10/2023
#include <stdio.h>
#include "sqlite.h"

static char const* SqlTypeName(SqlType const type)
{
	switch (type)
	{
	case SqlType::Integer:
		return "Integer";
	case SqlType::Float:
		return "Float";
	case SqlType::Text:
		return "Text";
	case SqlType::Blob:
		return "Blob";
	case SqlType::Null:
		return "Null";
	}

	ASSERT(false);
	return "Unknown";
}

static void SaveToDisk(Connection const& source, char const* const filename)
{
	Connection destination(filename);
	Backup backup(destination, source);
	backup.Step();
}

int main(int argc, char* argv[])
{

	try
	{
		Connection connection = Connection::Memory();

		connection.Profile([](void*, char const* const statement, unsigned long long const time)
		{
			unsigned long long const ms = time / 1000000;
			if (ms > 10)
				printf("Statement: %s, Time: %llums\n", statement, ms);
		});

		Execute(connection, "create table Shana (hola real)");
		Statement statement(connection, "insert into Shana values (?)");

		Execute(connection, "begin");
		for (int i = 0; i < 1000000; ++i)
		{
			statement.ResetAndBind(i);
			statement.Execute();
		}
		Execute(connection, "commit");

		Execute(connection, "begin");
		Execute(connection, "delete from Shana where hola > 15");
		Execute(connection, "commit");

		Execute(connection, "vacuum");

		SaveToDisk(connection, "test22.db");
	}
	catch (Exception const& e)
	{
		printf("Error: %s, Code: %d\n", e.Message.c_str(), e.Result);
	}
	
	return 0;
}