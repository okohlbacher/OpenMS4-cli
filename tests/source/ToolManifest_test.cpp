// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: OpenMS Team $
// $Authors: OpenMS Team $
// --------------------------------------------------------------------------

#include <OpenMS/APPLICATIONS/ToolHandler.h>
#include <OpenMS/CONCEPT/ClassTest.h>
#include <OpenMS/CONCEPT/Exception.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;

// Each test controls one temporary prefix and restores the caller's settings.
class RegistryFixture
{
public:
  explicit RegistryFixture(const std::string& directory): prefix_(fs::absolute(directory))
  {
    if (const char* value = std::getenv("OPENMS_TOOL_PREFIX_PATH"))
    {
      had_prefix_ = true;
      previous_prefix_ = value;
    }
    fs::create_directories(prefix_ / "share/openms4/tools");
    fs::create_directories(prefix_ / "bin");
    setPrefix_(prefix_.string());
  }

  RegistryFixture(const RegistryFixture&) = delete;
  RegistryFixture& operator=(const RegistryFixture&) = delete;

  ~RegistryFixture()
  {
    if (had_prefix_) { setPrefix_(previous_prefix_); }
    else
    {
#ifdef _WIN32
      _putenv_s("OPENMS_TOOL_PREFIX_PATH", "");
#else
      unsetenv("OPENMS_TOOL_PREFIX_PATH");
#endif
    }
    std::error_code error;
    fs::remove_all(prefix_, error);
  }

  void write(const std::string& contents) const
  {
    std::ofstream output(prefix_ / "share/openms4/tools/probe.tools.tsv");
    output << contents;
  }

  fs::path binary(const std::string& name) const
  { return prefix_ / "bin" / name; }

private:
  static void setPrefix_(const std::string& value)
  {
#ifdef _WIN32
    _putenv_s("OPENMS_TOOL_PREFIX_PATH", value.c_str());
#else
    setenv("OPENMS_TOOL_PREFIX_PATH", value.c_str(), 1);
#endif
  }

  fs::path prefix_;
  bool had_prefix_ = false;
  std::string previous_prefix_;
};
} // namespace

using namespace OpenMS;

START_TEST(ToolManifest, "$Id$")

START_SECTION(manifest parser rejects duplicate and unsafe entries)
{
  std::string temporary;
  NEW_TMP_FILE(temporary)
  RegistryFixture fixture(temporary);
  fixture.write("__PackageProbe\tExperimental\t1.0\tbin/Probe\n"
                "__PackageProbe\tExperimental\t2.0\tbin/Other\n");
  TEST_EXCEPTION(Exception::InvalidValue, ToolHandler::getToolVersion("__PackageProbe"))
  fixture.write("__PackageProbe\tExperimental\t1.0\t../Probe\n");
  TEST_EXCEPTION(Exception::InvalidValue, ToolHandler::getToolVersion("__PackageProbe"))
  fixture.write("__PackageProbe\tExperimental\t1.0\t/bin/Probe\n");
  TEST_EXCEPTION(Exception::InvalidValue, ToolHandler::getToolVersion("__PackageProbe"))
  fixture.write("__PackageProbe\tExperimental\t1.0\tbin/Probe\t\n");
  TEST_EXCEPTION(Exception::InvalidValue, ToolHandler::getToolVersion("__PackageProbe"))
#ifdef _WIN32
  fixture.write("__PackageProbe\tExperimental\t1.0\tC:Probe.exe\n");
  TEST_EXCEPTION(Exception::InvalidValue, ToolHandler::getToolVersion("__PackageProbe"))
#endif
}
END_SECTION

START_SECTION(declared executables must exist and be executable files)
{
  std::string temporary;
  NEW_TMP_FILE(temporary)
  RegistryFixture fixture(temporary);
  fixture.write("__PackageProbe\tExperimental\t1.0\tbin/Probe\n");
  TEST_EXCEPTION(Exception::FileNotFound, ToolHandler::findExecutable("__PackageProbe"))
  std::filesystem::create_directory(fixture.binary("Probe"));
  TEST_EXCEPTION(Exception::FileNotFound, ToolHandler::findExecutable("__PackageProbe"))
  std::filesystem::remove(fixture.binary("Probe"));
#ifndef _WIN32
  std::ofstream(fixture.binary("Probe")) << "This file has no execute permission.\n";
  std::filesystem::permissions(fixture.binary("Probe"), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  TEST_EXCEPTION(Exception::FileNotFound, ToolHandler::findExecutable("__PackageProbe"))
  TEST_EXCEPTION(Exception::FileNotFound, ToolHandler::findExecutable(fixture.binary("Probe").string()))
#endif
}
END_SECTION

START_SECTION(interactive desktop tools remain resolvable without CLI parameter discovery)
{
  std::string temporary;
  NEW_TMP_FILE(temporary)
  RegistryFixture fixture(temporary);
#ifdef _WIN32
  const std::string binary_name = "Probe.exe";
#else
  const std::string binary_name = "Probe";
#endif
  std::filesystem::copy_file(std::filesystem::absolute(argv[0]), fixture.binary(binary_name));
  fixture.write("# A comment followed by CRLF-terminated package records\r\n"
                "__PackageViewer\tDesktopViewer\t1.2.3\tbin/"
                + binary_name
                + "\r\n"
                  "__PackageWorkflow\tDesktopWorkflow\t1.2.3\tbin/"
                + binary_name
                + "\r\n"
                  "__PackageCLI\tWorkflows\t1.2.3\tbin/"
                + binary_name + "\r\n");
  const ToolListType tools = ToolHandler::getTOPPToolList();
  TEST_EQUAL(tools.contains("__PackageViewer"), false)
  TEST_EQUAL(tools.contains("__PackageWorkflow"), false)
  TEST_EQUAL(tools.contains("__PackageCLI"), true)
  TEST_STRING_EQUAL(ToolHandler::getToolVersion("__PackageViewer"), "1.2.3")
  TEST_STRING_EQUAL(ToolHandler::findExecutable("__PackageViewer"), fixture.binary(binary_name).string())
}
END_SECTION

END_TEST
