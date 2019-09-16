#include "testing/testing.hpp"

#include "coding/internal/file_data.hpp"
#include "coding/writer.hpp"

#include "base/logging.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace std;

namespace
{
  string const name1 = "test1.file";
  string const name2 = "test2.file";

  void MakeFile(string const & name)
  {
    base::FileData f(name, base::FileData::OP_WRITE_TRUNCATE);
    f.Write(name.c_str(), name.size());
  }

  void MakeFile(string const & name, size_t const size, const char c)
  {
    base::FileData f(name, base::FileData::OP_WRITE_TRUNCATE);
    f.Write(string(size, c).c_str(), size);
  }
}

UNIT_TEST(FileData_ApiSmoke)
{
  MakeFile(name1);
  uint64_t const size = name1.size();

  uint64_t sz;
  TEST(base::GetFileSize(name1, sz), ());
  TEST_EQUAL(sz, size, ());

  TEST(base::RenameFileX(name1, name2), ());

  TEST(!base::GetFileSize(name1, sz), ());
  TEST(base::GetFileSize(name2, sz), ());
  TEST_EQUAL(sz, size, ());

  TEST(base::DeleteFileX(name2), ());

  TEST(!base::GetFileSize(name2, sz), ());
}

UNIT_TEST(Equal_Function_Test)
{
  MakeFile(name1);
  MakeFile(name2);
  TEST(base::IsEqualFiles(name1, name1), ());
  TEST(base::IsEqualFiles(name2, name2), ());
  TEST(!base::IsEqualFiles(name1, name2), ());

  TEST(base::DeleteFileX(name1), ());
  TEST(base::DeleteFileX(name2), ());
}

UNIT_TEST(Equal_Function_Test_For_Big_Files)
{
  {
    MakeFile(name1, 1024 * 1024, 'a');
    MakeFile(name2, 1024 * 1024, 'a');
    TEST(base::IsEqualFiles(name1, name2), ());
    TEST(base::DeleteFileX(name1), ());
    TEST(base::DeleteFileX(name2), ());
  }
  {
    MakeFile(name1, 1024 * 1024 + 512, 'a');
    MakeFile(name2, 1024 * 1024 + 512, 'a');
    TEST(base::IsEqualFiles(name1, name2), ());
    TEST(base::DeleteFileX(name1), ());
    TEST(base::DeleteFileX(name2), ());
  }
  {
    MakeFile(name1, 1024 * 1024 + 1, 'a');
    MakeFile(name2, 1024 * 1024 + 1, 'b');
    TEST(base::IsEqualFiles(name1, name1), ());
    TEST(base::IsEqualFiles(name2, name2), ());
    TEST(!base::IsEqualFiles(name1, name2), ());
    TEST(base::DeleteFileX(name1), ());
    TEST(base::DeleteFileX(name2), ());
  }
  {
    MakeFile(name1, 1024 * 1024, 'a');
    MakeFile(name2, 1024 * 1024, 'b');
    TEST(base::IsEqualFiles(name1, name1), ());
    TEST(base::IsEqualFiles(name2, name2), ());
    TEST(!base::IsEqualFiles(name1, name2), ());
    TEST(base::DeleteFileX(name1), ());
    TEST(base::DeleteFileX(name2), ());
  }
  {
    MakeFile(name1, 1024 * 1024, 'a');
    MakeFile(name2, 1024 * 1024 + 1, 'b');
    TEST(!base::IsEqualFiles(name1, name2), ());
    TEST(base::DeleteFileX(name1), ());
    TEST(base::DeleteFileX(name2), ());
  }
}
